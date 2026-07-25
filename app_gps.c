#include "simcom_api.h"
#include "simcom_common.h"
#include "app_config.h"
#include "app_cfg.h"
#include "app_utils.h"
#include "app_gps.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NASA_PI 3.14159265358979323846
#define RAD_PER_DEG (NASA_PI / 180.0)
#define EARTH_RADIUS_KM 6371.0

static double s_lastLat = 0.0;
static double s_lastLon = 0.0;
static double s_lastSpeedKph = 0.0;
static double s_lastHeadingDeg = 0.0;
static int s_lastSatellites = 0;
static double s_totalKm = 0.0;
static uint32_t s_lastFixTick = 0;
static sMutexRef s_gpsDataMutex = NULL;

static double s_prevLat = 0.0;
static double s_prevLon = 0.0;
static uint32_t s_prevOdomTick = 0;
static uint32_t s_gpsLossStartTick = 0;

static SCsysTime_t s_lastGnssTime = {0};
static uint32_t s_lastGnssTimeTick = 0;
static int s_hasGnssTime = 0;

/**
 * @brief Tính số ngày trong tháng (hỗ trợ năm nhuận).
 */
static int DaysInMonth(int year, int mon)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d;
    if (mon < 1 || mon > 12) return 30;
    d = days[mon - 1];
    if (mon == 2) {
        int leap = ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0));
        if (leap) d = 29;
    }
    return d;
}

/**
 * @brief Cộng/trừ số giây vào cấu trúc thời gian SCsysTime_t.
 * Tự động quy đổi qua các ranh giới giây, phút, giờ, ngày, tháng và năm.
 */
static void AddSecondsToSysTime(SCsysTime_t *t, int32_t sec)
{
    int64_t total;
    int dim;
    if (t == NULL) return;
    total = (int64_t)t->tm_sec + (int64_t)sec;
    while (total < 0) {
        total += 60;
        t->tm_min--;
        if (t->tm_min < 0) {
            t->tm_min += 60;
            t->tm_hour--;
            if (t->tm_hour < 0) {
                t->tm_hour += 24;
                t->tm_mday--;
                if (t->tm_mday < 1) {
                    t->tm_mon--;
                    if (t->tm_mon < 1) {
                        t->tm_mon = 12;
                        t->tm_year--;
                    }
                    t->tm_mday = DaysInMonth(t->tm_year, t->tm_mon);
                }
            }
        }
    }
    t->tm_sec = (int)(total % 60);
    t->tm_min += (int)(total / 60);
    while (t->tm_min >= 60) {
        t->tm_min -= 60;
        t->tm_hour++;
    }
    while (t->tm_hour >= 24) {
        t->tm_hour -= 24;
        t->tm_mday++;
        dim = DaysInMonth(t->tm_year, t->tm_mon);
        if (t->tm_mday > dim) {
            t->tm_mday = 1;
            t->tm_mon++;
            if (t->tm_mon > 12) {
                t->tm_mon = 1;
                t->tm_year++;
            }
        }
    }
}

/**
 * @brief Đồng bộ RTC phần cứng từ thời gian GNSS UTC (chỉ thực hiện khi RTC bị mất nguồn / chưa sync).
 */
static void MaybeSyncRtcFromGnss(const SCsysTime_t *utc)
{
    t_rtc rtc;
    SCsysTime_t local;
    if (utc == NULL || utc->tm_year < 2020 || utc->tm_year > 2100) return;

    sAPI_GetRealTimeClock(&rtc);
    if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100) return;

    local = *utc;
    AddSecondsToSysTime(&local, (int32_t)NASA_GNSS_UTC_OFFSET_HOURS * 3600);
    memset(&rtc, 0, sizeof(rtc));
    rtc.tm_year = local.tm_year;
    rtc.tm_mon = local.tm_mon;
    rtc.tm_mday = local.tm_mday;
    rtc.tm_hour = local.tm_hour;
    rtc.tm_min = local.tm_min;
    rtc.tm_sec = local.tm_sec;
    (void)sAPI_SetRealTimeClock(&rtc);
    sAPI_Debug("[GNSS] RTC synced from GNSS: %04d-%02d-%02d %02d:%02d:%02d",
               rtc.tm_year, rtc.tm_mon, rtc.tm_mday, rtc.tm_hour, rtc.tm_min, rtc.tm_sec);
}

/**
 * @brief Lưu thời gian UTC nhận từ URC GNSS vào RAM và kích hoạt đồng bộ RTC.
 */
static void StoreGnssUtcTime(const SCsysTime_t *utc)
{
    if (utc == NULL || utc->tm_year < 2020 || utc->tm_year > 2100) return;
    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
    s_lastGnssTime = *utc;
    s_hasGnssTime = 1;
    s_lastGnssTimeTick = GetTickNow();
    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
    MaybeSyncRtcFromGnss(utc);
}

/**
 * @brief Định dạng chuỗi ngày giờ địa phương (UTC+7) từ bản tin GNSS mới nhất.
 * @return 1 nếu định dạng thành công, 0 nếu chưa có thời gian GNSS hợp lệ.
 */
int GPS_FormatLocalDateTime(char *dateTime, uint32_t dateTimeSize)
{
    SCsysTime_t local;
    uint32_t ageSec;
    uint32_t now;
    if ((dateTime == NULL) || (dateTimeSize == 0)) return 0;
    if (!s_hasGnssTime) return 0;

    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
    local = s_lastGnssTime;
    now = GetTickNow();
    ageSec = (now >= s_lastGnssTimeTick)
                 ? ((now - s_lastGnssTimeTick) / (uint32_t)SC_TICKS_PER_SECOND)
                 : 0;
    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);

    AddSecondsToSysTime(&local, (int32_t)NASA_GNSS_UTC_OFFSET_HOURS * 3600 + (int32_t)ageSec);
    if (local.tm_year < 2020 || local.tm_year > 2100) return 0;

    (void)snprintf(dateTime, dateTimeSize, "%04d-%02d-%02d %02d:%02d:%02d",
                   local.tm_year, local.tm_mon, local.tm_mday,
                   local.tm_hour, local.tm_min, local.tm_sec);
    return 1;
}

/* Chống nhiễu vị trí khi xe đỗ: chỉ coi là di chuyển khi tốc độ vượt ngưỡng
 * đủ lâu (FilterMovingStatus); khi đứng yên thì neo toạ độ tại điểm dừng và
 * không cộng odometer. */
static int    s_isMovingNow = 0;
static double s_anchorLat = 0.0;
static double s_anchorLon = 0.0;
static volatile int s_accWireOn = 0; /* dây ACC (đã debounce), cập nhật từ GPIO task */

/**
 * @brief Cập nhật trạng thái đầu vào dây ACC (đã qua lọc debounce ở GPIO Task).
 * @param on 1 nếu ACC ON (khóa điện bật), 0 nếu ACC OFF.
 */
void GPS_SetAccOn(int on)
{
    s_accWireOn = on ? 1 : 0;
}

static sMsgQRef s_gnssUrcMsgQ = NULL;
static sTaskRef s_gnssUrcTaskRef = NULL;
static UINT8 s_gnssUrcTaskStack[1024 * 3];

/**
 * @brief Khởi tạo các tài nguyên hệ thống dùng chung cho module GPS (Mutex bảo vệ data).
 */
void GPS_Init(void)
{
    if (s_gpsDataMutex == NULL)
    {
        sAPI_MutexCreate(&s_gpsDataMutex, SC_FIFO);
    }
}

/**
 * @brief Trích xuất bản sao Snapshot dữ liệu GPS an toàn luồng (Thread-safe).
 * Tự động vô hiệu hóa cờ valid nếu số vệ tinh < 5 hoặc vị trí lat/lon = 0.
 * @param out Con trỏ tới cấu trúc GpsSnapshot_t nhận dữ liệu.
 */
void GPS_Snapshot(GpsSnapshot_t *out)
{
    if (out == NULL) return;
    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
    out->lat        = s_lastLat;
    out->lon        = s_lastLon;
    out->speedKph   = s_lastSpeedKph;
    out->headingDeg = s_lastHeadingDeg;
    out->satellites = s_lastSatellites;
    out->totalKm    = s_totalKm;
    out->fixTick    = s_lastFixTick;
    out->valid      = (out->satellites >= 5 && out->lat != 0.0 && out->lon != 0.0);
    if (!out->valid)
    {
        out->lat = 0.0;
        out->lon = 0.0;
    }
    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
}

/**
 * @brief Đặt lại (reset) tổng số km Odometer và điểm tọa độ tham chiếu về 0.
 */
void GPS_ResetOdometer(void)
{
    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
    s_totalKm = 0.0;
    s_prevLat = 0.0;
    s_prevLon = 0.0;
    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
}

/**
 * @brief Lấy số lượng vệ tinh GPS/GNSS đang thu nhận hiện tại.
 */
int GPS_GetSatellitesCount(void)
{
    int val = 0;
    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
    val = s_lastSatellites;
    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
    return val;
}

/**
 * @brief Lấy vận tốc xe hiện tại (km/h).
 */
double GPS_GetLastSpeedKph(void)
{
    double val = 0.0;
    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
    val = s_lastSpeedKph;
    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
    return val;
}

/**
 * @brief Lấy tổng số km tích lũy Odometer trong ngày (km).
 */
double GPS_GetTotalKm(void)
{
    double val = 0.0;
    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
    val = s_totalKm;
    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
    return val;
}

/**
 * @brief Kiểm tra trạng thái xe di chuyển thực tế (1 = Moving, 0 = Stopped).
 */
int GPS_IsMoving(void)
{
    return s_isMovingNow;
}

/**
 * @brief Tính khoảng cách giữa 2 tọa độ địa lý (Lat, Lon) theo công thức Haversine (đơn vị km).
 */
static double CalculateDistanceKm(double lat1, double lon1, double lat2, double lon2)
{
    double dLat = (lat2 - lat1) * RAD_PER_DEG;
    double dLon = (lon2 - lon1) * RAD_PER_DEG;
    double rlat1 = lat1 * RAD_PER_DEG;
    double rlat2 = lat2 * RAD_PER_DEG;

    double sinDLat = sin(dLat * 0.5);
    double sinDLon = sin(dLon * 0.5);
    double a = sinDLat * sinDLat
             + cos(rlat1) * cos(rlat2) * sinDLon * sinDLon;

    if (a > 1.0) a = 1.0;
    return EARTH_RADIUS_KM * 2.0 * asin(sqrt(a));
}

/**
 * @brief Bộ lọc trạng thái Chạy / Dừng chống nhiễu GPS nâng cao.
 * Logic tối ưu:
 *  - accMode == 1: ACC OFF -> Khóa cứng trạng thái đứng yên 0, bỏ qua nhiễu tốc độ GPS.
 *  - Tốc độ > ngưỡng (CFG_GetSpeedThresh) liên tục 3s VÀ dịch chuyển thực khỏi điểm neo >= 50m mới xác nhận di chuyển.
 *  - Tốc độ <= ngưỡng liên tục 10s mới xác nhận dừng đỗ.
 */
static int FilterMovingStatus(double speed, double lat, double lon)
{
    static uint32_t s_speedAboveThresholdTick = 0;
    static uint32_t s_speedBelowThresholdTick = 0;
    static int s_isMoving = 0;
    uint32_t now = GetTickNow();
    double thresh = (double)CFG_GetSpeedThresh();
    if (thresh <= 0.0) thresh = 3.0;

    if (CFG_GetAccMode() == 1 && !s_accWireOn) {
        s_speedAboveThresholdTick = 0;
        s_speedBelowThresholdTick = 0;
        s_isMoving = 0;
        s_isMovingNow = 0;
        return 0;
    }

    if (speed > thresh) {
        s_speedBelowThresholdTick = 0;
        if (s_speedAboveThresholdTick == 0) s_speedAboveThresholdTick = now;
        else if ((now - s_speedAboveThresholdTick) >= (MOVING_CONFIRM_SEC * SC_TICKS_PER_SECOND)) {
            /* Đủ thời gian vượt ngưỡng — kiểm tra thêm dịch chuyển thực khỏi neo */
            if (s_anchorLat == 0.0 && s_anchorLon == 0.0) {
                s_isMoving = 1; /* chưa có neo (mới boot / đang chạy sẵn) */
            } else if (lat != 0.0 && lon != 0.0 &&
                       CalculateDistanceKm(s_anchorLat, s_anchorLon, lat, lon) >=
                           MOVING_MIN_DISPLACEMENT_KM) {
                s_isMoving = 1;
            }
        }
    } else {
        s_speedAboveThresholdTick = 0;
        if (s_speedBelowThresholdTick == 0) s_speedBelowThresholdTick = now;
        else if ((now - s_speedBelowThresholdTick) >= (STOPPED_CONFIRM_SEC * SC_TICKS_PER_SECOND)) s_isMoving = 0;
    }
    s_isMovingNow = s_isMoving;
    return s_isMoving;
}

/**
 * @brief Neo giữ vị trí đứng yên khi xe đỗ (Stationary Anchor).
 * Triệt tiêu 100% hiện tượng trôi vệt GPS xung quanh điểm dừng khi xe không di chuyển.
 */
static void ApplyStationaryAnchor(int isMoving)
{
    if (s_lastSatellites < 5 || (s_lastLat == 0.0 && s_lastLon == 0.0)) {
        return; /* chưa fix — không đụng anchor */
    }
    if (isMoving) {
        s_anchorLat = 0.0;
        s_anchorLon = 0.0;
        return;
    }
    if (s_anchorLat == 0.0 && s_anchorLon == 0.0) {
        s_anchorLat = s_lastLat;
        s_anchorLon = s_lastLon;
    } else {
        s_lastLat = s_anchorLat;
        s_lastLon = s_anchorLon;
    }
}

/**
 * @brief Tính toán và cộng dồn quãng đường di chuyển Odometer (km).
 * Logic tối ưu:
 *  - Khi đứng yên (!s_isMovingNow): Không cộng dồn km (ngăn ngừa odometer ảo do nhiễu trôi GPS).
 *  - Bộ lọc Haversine & Jump Filter: Bỏ qua các bước nhảy khoảng cách lớn bất thường vượt quá tốc độ tối đa cho phép.
 *  - Tự động reset điểm tham chiếu khi mất tín hiệu GPS kéo dài (> ODOM_GPS_LOSS_TIMEOUT_SEC).
 */
static void UpdateOdometer(double newLat, double newLon)
{
    uint32_t now = GetTickNow();

    if (s_lastSatellites < 5 || newLat == 0.0 || newLon == 0.0)
    {
        if (s_gpsLossStartTick == 0)
        {
            s_gpsLossStartTick = (now != 0) ? now : 1;
        }
        else if ((now - s_gpsLossStartTick) >= (uint32_t)(ODOM_GPS_LOSS_TIMEOUT_SEC * SC_TICKS_PER_SECOND))
        {
            sAPI_Debug("[Odom] Mat GPS qua lau (>%ds) -> reset diem tham chieu odometer",
                       ODOM_GPS_LOSS_TIMEOUT_SEC);
            s_prevLat = 0.0;
            s_prevLon = 0.0;
        }
        return;
    }

    int wasGpsLoss = (s_gpsLossStartTick != 0);
    s_gpsLossStartTick = 0;

    if (!s_isMovingNow)
    {
        /* Đứng yên: không cộng quãng đường (chống odometer ảo do nhiễu GPS),
         * chỉ dời điểm tham chiếu theo vị trí hiện tại */
        s_prevLat = newLat;
        s_prevLon = newLon;
        s_prevOdomTick = now;
        return;
    }

    if (s_prevLat != 0.0 && s_prevLon != 0.0)
    {
        double dist = CalculateDistanceKm(s_prevLat, s_prevLon, newLat, newLon);
        double elapsedSec = (s_prevOdomTick != 0 && now > s_prevOdomTick)
                                  ? (double)(now - s_prevOdomTick) / SC_TICKS_PER_SECOND
                                  : 1.0;
        if (elapsedSec < 1.0) elapsedSec = 1.0;

        double maxDist;
        if (s_lastSpeedKph < (double)CFG_GetSpeedThresh())
        {
            maxDist = ODOM_MAX_STATIONARY_KM;
        }
        else
        {
            double calcSpeed = (s_lastSpeedKph < (double)ODOM_JUMP_MIN_CALC_SPEED_KPH)
                                   ? (double)ODOM_JUMP_MIN_CALC_SPEED_KPH
                                   : s_lastSpeedKph;
            maxDist = (calcSpeed / 3.6) * elapsedSec * ODOM_MAX_JUMP_FACTOR / 1000.0;
        }

        if (dist >= ODOM_MIN_DIST_KM && dist <= maxDist)
        {
            s_totalKm += dist;
            if (wasGpsLoss)
            {
                sAPI_Debug("[Odom] Noi lai sau mat GPS ngan (%.1fs): +%.1fm",
                           elapsedSec, dist * 1000.0);
            }
        }
        else if (dist > maxDist)
        {
            sAPI_Debug("[Odom] Jump filtered: %.1fm > max %.1fm (speed=%.1fkph, elapsed=%.1fs)",
                       dist * 1000.0, maxDist * 1000.0, s_lastSpeedKph, elapsedSec);
        }
    }

    s_prevLat = newLat;
    s_prevLon = newLon;
    s_prevOdomTick = now;
}

/**
 * @brief Bóc tách chuỗi CSV theo dấu phẩy trực tiếp trên buffer (Zero-copy string tokenizer).
 * @param line Chuỗi dữ liệu cần phân tách.
 * @param fields Mảng con trỏ lưu trữ địa chỉ các trường dữ liệu.
 * @param maxFields Số lượng trường tối đa có thể chứa.
 * @return Số lượng trường đã bóc tách được.
 */
static int SplitCommaInPlace(char *line, char **fields, int maxFields)
{
    if (!line || !fields || maxFields <= 0) return 0;
    int n = 0;
    char *ptr = line;
    while (n < maxFields)
    {
        fields[n++] = ptr;
        ptr = strchr(ptr, ',');
        if (ptr) {
            *ptr = '\0';
            ptr++;
        } else {
            break;
        }
    }
    return n;
}

/**
 * @brief Bóc tách và cập nhật dữ liệu GPS từ chuỗi URC chuẩn +CGPSINFO.
 * @return 1 nếu parse đúng định dạng chuỗi +CGPSINFO, 0 nếu không khớp.
 */
static int TryParseCgpsInfoUrc(const char *gpsUrc)
{
    const char *hdr = strstr(gpsUrc, "+CGPSINFO:");
    if (!hdr) return 0;
    
    char buf[256];
    char *fields[16];
    
    strncpy(buf, hdr + 10, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    int nf = SplitCommaInPlace(buf, fields, 16);
    if (nf < 8 || fields[0][0] == '\0' || fields[2][0] == '\0') return 0;

    double latRaw = strtod(fields[0], NULL);
    double lonRaw = strtod(fields[2], NULL);
    
    double latDec = (int)(latRaw / 100.0) + (fmod(latRaw, 100.0) / 60.0);
    double lonDec = (int)(lonRaw / 100.0) + (fmod(lonRaw, 100.0) / 60.0);

    if (fields[1][0] == 'S' || fields[1][0] == 's') latDec = -latDec;
    if (fields[3][0] == 'W' || fields[3][0] == 'w') lonDec = -lonDec;

    /* Lọc nhiễu tốc độ TRƯỚC khi dùng — GPS đứng yên vẫn báo 4–26 km/h */
    double speedRaw = strtod(fields[7], NULL) * KNOTS_TO_KMH;
    int isMoving = FilterMovingStatus(speedRaw, latDec, lonDec);

    {
        SCsysTime_t utc = {0};
        if (!ParseDateTimeFromUrcTokens(fields[4], fields[5], &utc))
            return 0;

        if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
        s_lastLat = latDec;
        s_lastLon = lonDec;
        if (isMoving) {
            s_lastSpeedKph = speedRaw;
            s_lastHeadingDeg = (nf > 8) ? strtod(fields[8], NULL) : 0.0;
        } else {
            s_lastSpeedKph = 0.0;
        }
        s_lastSatellites = (nf > 9) ? atoi(fields[9]) : 0;

        if (s_lastSatellites < 5) {
            s_lastLat = 0.0;
            s_lastLon = 0.0;
        }

        ApplyStationaryAnchor(isMoving);
        UpdateOdometer(s_lastLat, s_lastLon);
        s_lastFixTick = GetTickNow();
        if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);

        StoreGnssUtcTime(&utc);
        static uint32_t lastGnssLogTick = 0;
        uint32_t nowGnssTick = GetTickNow();
        if (lastGnssLogTick == 0 || (nowGnssTick - lastGnssLogTick) >= (uint32_t)(10 * SC_TICKS_PER_SECOND)) {
            lastGnssLogTick = nowGnssTick;
            sAPI_Debug("[GNSS] lat=%.6f lon=%.6f spd=%.2f sats=%d", s_lastLat, s_lastLon, s_lastSpeedKph, s_lastSatellites);
        }
    }
    return 1;
}

/**
 * @brief Bóc tách và cập nhật dữ liệu GPS từ chuỗi URC chuẩn +CGNSSINFO (hỗ trợ đa hệ vệ tinh GPS/GLONASS/Galileo/BDS).
 * @return 1 nếu parse đúng định dạng chuỗi +CGNSSINFO, 0 nếu không khớp.
 */
static int TryParseCgnssInfoUrc(const char *gpsUrc)
{
    const char *hdr = strstr(gpsUrc, "+CGNSSINFO:");
    if (!hdr) return 0;

    char buf[256];
    char *fields[20];
    
    size_t len = strlen(hdr + 11);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, hdr + 11, len);
    buf[len] = '\0';

    int nf = SplitCommaInPlace(buf, fields, 20);
    if (nf < 13)
    {
        if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
        s_lastSatellites = 0;
        s_lastLat = 0.0;
        s_lastLon = 0.0;
        s_lastSpeedKph = 0.0;
        if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
        sAPI_Debug("[GNSS] No fix (nf=%d)", nf);
        return 1;
    }

    int satellites = (fields[1][0] != '\0') ? atoi(fields[1]) : 0;

    int ns_idx = -1;
    for (int i = 3; i < 7 && i < nf; i++) {
        if (fields[i][0] == 'N' || fields[i][0] == 'S') { ns_idx = i; break; }
    }
    if (ns_idx == -1) return 0;

    double latRaw = (ns_idx - 1 < nf) ? strtod(fields[ns_idx - 1], NULL) : 0.0;
    double lonRaw = (ns_idx + 1 < nf) ? strtod(fields[ns_idx + 1], NULL) : 0.0;
    
    double latDec = (int)(latRaw / 100) + (fmod(latRaw, 100.0) / 60.0);
    double lonDec = (int)(lonRaw / 100) + (fmod(lonRaw, 100.0) / 60.0);
    
    if (ns_idx < nf && fields[ns_idx][0] == 'S') latDec = -latDec;
    if (ns_idx + 2 < nf && fields[ns_idx + 2][0] == 'W') lonDec = -lonDec;

    double speedKnots = (ns_idx + 6 < nf) ? strtod(fields[ns_idx + 6], NULL) : 0.0;
    double speed = speedKnots * KNOTS_TO_KMH;
    int isMoving = FilterMovingStatus(speed, latDec, lonDec);

    if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);

    s_lastSatellites = satellites;
    s_lastLat = latDec;
    s_lastLon = lonDec;
    if (isMoving) {
        s_lastHeadingDeg = (ns_idx + 7 < nf) ? strtod(fields[ns_idx + 7], NULL) : 0.0;
        s_lastSpeedKph = speed;
    } else {
        s_lastSpeedKph = 0.0;
    }

    s_lastFixTick = GetTickNow();
    if (s_lastSatellites < 5) s_lastLat = s_lastLon = 0.0;

    ApplyStationaryAnchor(isMoving);
    UpdateOdometer(s_lastLat, s_lastLon);

    if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);

    /* date/UTC time nằm ngay sau E/W: ns_idx+3, ns_idx+4 */
    if (ns_idx + 4 < nf && fields[ns_idx + 3][0] != '\0' && fields[ns_idx + 4][0] != '\0') {
        SCsysTime_t utc = {0};
        if (ParseDateTimeFromUrcTokens(fields[ns_idx + 3], fields[ns_idx + 4], &utc))
            StoreGnssUtcTime(&utc);
    }
    return 1;
}

/**
 * @brief Bóc tách thời gian UTC từ các trường date/time của bản tin NMEA/URC.
 */
static int TryParseGnssDateTimeFromUrc(const char *gpsUrc, SCsysTime_t *out)
{
    const char *p;
    if ((gpsUrc == NULL) || (out == NULL))
    {
        return 0;
    }
    p = gpsUrc;
    while ((*p != '\0') && (*(p + 13) != '\0'))
    {
        if ((p[0] == ',') &&
            IsDigitChar(p[1]) && IsDigitChar(p[2]) && IsDigitChar(p[3]) &&
            IsDigitChar(p[4]) && IsDigitChar(p[5]) && IsDigitChar(p[6]) &&
            (p[7] == ',') &&
            IsDigitChar(p[8]) && IsDigitChar(p[9]) && IsDigitChar(p[10]) &&
            IsDigitChar(p[11]) && IsDigitChar(p[12]) && IsDigitChar(p[13]))
        {
            if (ParseDateTimeFromUrcTokens(p + 1, p + 8, out))
            {
                return 1;
            }
        }
        p++;
    }
    return 0;
}

/**
 * @brief Hàm helper trích xuất giá trị số thực double tiếp theo từ chuỗi URC.
 */
static int ExtractNextDouble(const char **p, double *out)
{
    const char *s = *p;
    char *end = NULL;
    double v;
    if ((p == NULL) || (out == NULL) || (s == NULL))
    {
        return 0;
    }
    while (*s != '\0')
    {
        if ((*s == '+') || (*s == '-') || (*s == '.') || ((*s >= '0') && (*s <= '9')))
        {
            break;
        }
        s++;
    }
    if (*s == '\0')
    {
        *p = s;
        return 0;
    }
    v = strtod(s, &end);
    if (end == s)
    {
        *p = s + 1;
        return 0;
    }
    *out = v;
    *p = end;
    return 1;
}

/**
 * @brief Hàm điều phối cập nhật GPS từ chuỗi URC bất kỳ.
 * Thử parse theo +CGNSSINFO, +CGPSINFO và Fallback Parser có kiểm tra ranh giới tọa độ [-90,90] & [-180,180].
 */
static void TryUpdateGpsFromUrcString(const char *gpsUrc)
{
    if (gpsUrc == NULL) return;

    if (TryParseCgnssInfoUrc(gpsUrc) || TryParseCgpsInfoUrc(gpsUrc)) {
        SCsysTime_t utc = {0};
        /* Bổ sung parse date/time nếu parser fix chưa lấy được */
        if (!s_hasGnssTime && TryParseGnssDateTimeFromUrc(gpsUrc, &utc))
            StoreGnssUtcTime(&utc);
        return;
    }

    {
        SCsysTime_t utc = {0};
        if (TryParseGnssDateTimeFromUrc(gpsUrc, &utc))
            StoreGnssUtcTime(&utc);
    }

    double nums[5];
    int n = 0;
    const char *p = gpsUrc;

    while (n < 5 && ExtractNextDouble(&p, &nums[n]))
    {
        n++;
    }

    if (n >= 2 && (nums[0] >= -90.0 && nums[0] <= 90.0) && (nums[1] >= -180.0 && nums[1] <= 180.0))
    {
        double speedRaw = (n >= 3) ? nums[2] * KNOTS_TO_KMH : 0.0;
        int isMoving = FilterMovingStatus(speedRaw, nums[0], nums[1]);

        sAPI_Debug("[GPS-Fallback] Dung fallback parser, raw=%s", gpsUrc);
        if (s_gpsDataMutex) sAPI_MutexLock(s_gpsDataMutex, SC_SUSPEND);
        s_lastLat = nums[0];
        s_lastLon = nums[1];

        if (isMoving) {
            s_lastSpeedKph = speedRaw;
            if (n >= 4) s_lastHeadingDeg = nums[3];
        } else {
            s_lastSpeedKph = 0.0;
        }
        if (n >= 5) s_lastSatellites = (int)nums[4];

        if (s_lastSatellites < 5)
        {
            s_lastLat = 0.0;
            s_lastLon = 0.0;
        }

        ApplyStationaryAnchor(isMoving);
        UpdateOdometer(s_lastLat, s_lastLon);
        s_lastFixTick = GetTickNow();
        if (s_gpsDataMutex) sAPI_MutexUnLock(s_gpsDataMutex);
    }
}

/**
 * @brief Task RTOS lắng nghe và xử lý sự kiện URC GNSS từ modem theo cơ chế Queue Event-Driven.
 * Rate-limit log URC 30s để giảm dung lượng UART log.
 */
static void sTask_GnssUrcListener(void *argv) 
{
    (void)argv;
    for (;;)
    {
        SIM_MSG_T msg = {0};
        if (s_gnssUrcMsgQ == NULL)
        {
            sAPI_TaskSleep(200);
            continue;
        }
        if (sAPI_MsgQRecv(s_gnssUrcMsgQ, &msg, SC_SUSPEND) != SC_SUCCESS)
        {
            continue;
        }
        if ((msg.msg_id == SRV_URC) && (msg.arg1 == SC_URC_GNSS_MASK) && (msg.arg3 != NULL))
        {
            static uint32_t lastUrcLogTick = 0;
            uint32_t nowUrcTick = GetTickNow();
            if (lastUrcLogTick == 0 || (nowUrcTick - lastUrcLogTick) >= (uint32_t)(30 * SC_TICKS_PER_SECOND)) {
                lastUrcLogTick = nowUrcTick;
                sAPI_Debug("[GPS URC] msg_id=%u arg1=%d arg2=%d raw=%s",
                           (unsigned)msg.msg_id,
                           (int)msg.arg1,
                           (int)msg.arg2,
                           (const char *)msg.arg3);
            }
            if ((msg.arg2 == SC_URC_GPS_INFO) || (msg.arg2 == SC_URC_GNSS_INFO))
            {
                TryUpdateGpsFromUrcString((const char *)msg.arg3);
            }
        }
        if (msg.arg3 != NULL)
        {
            sAPI_Free(msg.arg3);
            msg.arg3 = NULL;
        }
    }
}

/**
 * @brief Khởi tạo và khởi chạy GNSS Hardware (Hot Start) cùng Task URC Listener.
 */
void GnssUrcListenerEnsureStarted(void)
{
    if (s_gnssUrcTaskRef != NULL) return;

    static int is_initialized = 0;
    if (!is_initialized)
    {
        GPS_Init();

        if (sAPI_MsgQCreate(&s_gnssUrcMsgQ, "nasa_gnss_q", sizeof(SIM_MSG_T), 10, SC_FIFO) != SC_SUCCESS)
            return;
        
        sAPI_UrcRefRegister(s_gnssUrcMsgQ, SC_URC_GNSS_MASK);
        is_initialized = 1;
    }

    if (sAPI_GnssPowerStatusSet(SC_GNSS_POWER_ON) == SC_GNSS_RETURN_CODE_OK) 
    {
        static int first_boot = 1;
        if (first_boot) {
            /* Hot start: dùng ephemeris/almanac đã lưu, TTFF nhanh hơn nhiều
             * so với cold start; cold start chỉ dành cho chẩn đoán */
            sAPI_GnssStartMode(SC_GNSS_START_HOT);
            first_boot = 0;
        }
        sAPI_GnssInfoGet(1);
    }

    if (sAPI_TaskCreate(&s_gnssUrcTaskRef, s_gnssUrcTaskStack, sizeof(s_gnssUrcTaskStack), 
                        125, (char *)"nasa_gnss_urc", sTask_GnssUrcListener, NULL) == SC_SUCCESS)
    {
        sAPI_Debug("[GPS API] GNSS URC Task started.");
    }
}
