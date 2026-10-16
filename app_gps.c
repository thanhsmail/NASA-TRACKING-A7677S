/**
 * @file app_gps.c
 * @brief Xử lý GPS / GNSS -- NASA Tracking
 *
 * gnss_task (hal_impl_simcom.c) đưa mỗi dòng URC GNSS vào GPS_OnUrcString().
 * Dòng được đọc thành một mẫu (GnssSample_t) rồi ApplySample() cập nhật theo
 * thứ tự: lọc chạy / dừng / đỗ -> dữ liệu mới nhất -> odometer -> điểm neo khi
 * đứng yên -> log debug. Task khác đọc qua GPS_Snapshot().
 *
 * File này không include SDK: giờ / RTC qua HAL_GNSS_*, mutex qua HAL_OS_*.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_gnss.h"
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
#define GPS_MIN_SATS_FOR_FIX 5

/* ============================== Trạng thái ================================ */

/* Dữ liệu GPS mới nhất -- bảo vệ bằng s_gpsDataMutex */
static HalMutexRef_t s_gpsDataMutex = NULL;
static double   s_lastLat        = 0.0;  /* 0,0 = chưa có fix; khi đứng yên là điểm neo */
static double   s_lastLon        = 0.0;
static double   s_lastSpeedKph   = 0.0;  /* 0 khi không chạy */
static double   s_lastHeadingDeg = 0.0;  /* chỉ cập nhật khi đang chạy */
static int      s_lastSatellites = 0;    /* tổng mọi hệ vệ tinh */
static double   s_lastHdop       = 0.0;  /* 0 = URC không báo HDOP */
static int      s_satSys[4]      = {0};  /* số vệ tinh theo hệ GPS / GLONASS / GALILEO / BEIDOU */
static uint32_t s_lastFixTick    = 0;
static double   s_holdLat        = 0.0;  /* vị trí hợp lệ gần nhất, dùng khi mất GPS */
static double   s_holdLon        = 0.0;

/* Giờ UTC từ GNSS */
static HalDateTime_t s_lastGnssTime     = {0};
static uint32_t      s_lastGnssTimeTick = 0;
static int           s_hasGnssTime      = 0;

/* Trạng thái chạy / dừng / đỗ (FilterMovingStatus) */
static int          s_isMovingNow      = 0;
static int          s_isParkedNow      = 0;
static int          s_isCoastingToPark = 0;  /* ACC đã tắt nhưng xe còn trôi */
/* Dây ACC, từ task gpio_status. -1 = chưa đọc được chân lần nào: chưa biết thì không coi là ACC OFF */
static volatile int s_accWireOn        = -1;
/* Dây ACC từng báo ON từ lúc khởi động = xe có đấu dây (dùng cho accMode 2) */
static volatile int s_accWireSeenOn    = 0;

/* Điểm neo khi đứng yên (ApplyStationaryAnchor) */
static double s_anchorLat        = 0.0;  /* 0,0 = chưa neo */
static double s_anchorLon        = 0.0;
static double s_anchorSumLat     = 0.0;  /* tổng các mẫu đã gộp vào điểm neo */
static double s_anchorSumLon     = 0.0;
static int    s_anchorN          = 0;    /* số mẫu trong điểm neo */
static int    s_anchorGood       = 0;    /* 1 = điểm neo dựa trên mẫu HDOP tốt */
static int    s_anchorRejectRun  = 0;    /* số mẫu tốt liên tiếp bị loại vì xa trung bình */

/* Odometer (UpdateOdometer) */
static double   s_totalKm          = 0.0;
static double   s_prevLat          = 0.0;  /* điểm tham chiếu, 0,0 = chưa có */
static double   s_prevLon          = 0.0;
static uint32_t s_prevOdomTick     = 0;
static uint32_t s_gpsLossStartTick = 0;    /* 0 = đang có GPS */
static int      s_odomRefDropped   = 0;    /* đã bỏ điểm tham chiếu trong lần mất GPS này */

/* Debug: tick lúc yêu cầu AGPS, để đo thời gian có fix sau AGPS; 0 = không đo */
static uint32_t s_agpsReqTick = 0;

/** Một mẫu GNSS đã đọc từ URC, chưa áp vào trạng thái */
typedef struct {
    double lat;          /* độ thập phân, + = Bắc */
    double lon;          /* độ thập phân, + = Đông */
    double speedKph;
    double headingDeg;
    double hdop;         /* 0 = không có */
    int    satellites;   /* tổng mọi hệ */
    int    satSys[4];    /* theo hệ, 0 nếu URC không tách */
} GnssSample_t;

/* ============================== Giờ GNSS ================================== */

/** Đặt RTC từ giờ GNSS (giờ Việt Nam) khi RTC chưa có giờ hợp lệ */
static void MaybeSyncRtcFromGnss(const HalDateTime_t *utc)
{
    HalDateTime_t rtc;
    HalDateTime_t local;

    if (utc == NULL || utc->tm_year < 2020 || utc->tm_year > 2100) return;

    HAL_GNSS_GetRtc(&rtc);
    if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100) return;

    local = *utc;
    UTIL_AddSecondsToDateTime(&local, (int32_t)NASA_GNSS_UTC_OFFSET_HOURS * 3600);
    HAL_GNSS_SetRtc(&local);
    HAL_LOG("[GNSS] RTC synced: %04d-%02d-%02d %02d:%02d:%02d",
            local.tm_year, local.tm_mon, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

/** Lưu giờ UTC từ URC và đồng bộ RTC nếu cần */
static void StoreGnssUtcTime(const HalDateTime_t *utc)
{
    if (utc == NULL || utc->tm_year < 2020 || utc->tm_year > 2100) return;
    HAL_OS_MutexLock(s_gpsDataMutex);
    s_lastGnssTime     = *utc;
    s_hasGnssTime      = 1;
    s_lastGnssTimeTick = GetTickNow();
    HAL_OS_MutexUnlock(s_gpsDataMutex);
    MaybeSyncRtcFromGnss(utc);
}

/**
 * @brief Ngày giờ địa phương (UTC+7) từ giờ GNSS mới nhất, cộng thời gian đã trôi.
 * @return 1 nếu thành công, 0 nếu chưa có giờ GNSS hợp lệ.
 */
int GPS_FormatLocalDateTime(char *dateTime, uint32_t dateTimeSize)
{
    HalDateTime_t local;
    uint32_t ageSec;
    uint32_t now;

    if ((dateTime == NULL) || (dateTimeSize == 0)) return 0;
    if (!s_hasGnssTime) return 0;

    HAL_OS_MutexLock(s_gpsDataMutex);
    local  = s_lastGnssTime;
    now    = GetTickNow();
    ageSec = (now >= s_lastGnssTimeTick)
                 ? ((now - s_lastGnssTimeTick) / (uint32_t)HAL_TICKS_PER_SEC)
                 : 0;
    HAL_OS_MutexUnlock(s_gpsDataMutex);

    UTIL_AddSecondsToDateTime(&local, (int32_t)NASA_GNSS_UTC_OFFSET_HOURS * 3600 + (int32_t)ageSec);
    if (local.tm_year < 2020 || local.tm_year > 2100) return 0;

    (void)snprintf(dateTime, dateTimeSize, "%04d-%02d-%02d %02d:%02d:%02d",
                   local.tm_year, local.tm_mon, local.tm_mday,
                   local.tm_hour, local.tm_min, local.tm_sec);
    return 1;
}

/* ============================== API công khai ============================= */

/** Tạo mutex dữ liệu GPS. Gọi trước khi bật gnss_task. */
void GPS_Init(void)
{
    if (s_gpsDataMutex == NULL) {
        HAL_OS_MutexCreate(&s_gpsDataMutex);
    }
}

void GPS_SetAccOn(int on)
{
    s_accWireOn = on ? 1 : 0;
    if (on) s_accWireSeenOn = 1;
}

/**
 * @brief Bản sao dữ liệu GPS an toàn luồng.
 * valid = 0 khi dưới 5 vệ tinh hoặc chưa có vị trí; lúc đó lat / lon là vị trí
 * hợp lệ gần nhất (chưa từng có fix từ lúc bật thì là 0,0).
 */
void GPS_Snapshot(GpsSnapshot_t *out)
{
    if (out == NULL) return;
    HAL_OS_MutexLock(s_gpsDataMutex);
    out->lat        = s_lastLat;
    out->lon        = s_lastLon;
    out->speedKph   = s_lastSpeedKph;
    out->headingDeg = s_lastHeadingDeg;
    out->satellites = s_lastSatellites;
    out->totalKm    = s_totalKm;
    out->fixTick    = s_lastFixTick;
    out->valid      = (out->satellites >= GPS_MIN_SATS_FOR_FIX && out->lat != 0.0 && out->lon != 0.0);
    if (out->valid) {
        s_holdLat = out->lat;
        s_holdLon = out->lon;
    } else {
        out->lat = s_holdLat;
        out->lon = s_holdLon;
    }
    HAL_OS_MutexUnlock(s_gpsDataMutex);
}

/** Đặt km trong ngày về 0 (gọi khi đổi ngày) */
void GPS_ResetOdometer(void)
{
    HAL_OS_MutexLock(s_gpsDataMutex);
    s_totalKm = 0.0;
    s_prevLat = 0.0;
    s_prevLon = 0.0;
    HAL_OS_MutexUnlock(s_gpsDataMutex);
}

int GPS_GetSatellitesCount(void)
{
    int val;
    HAL_OS_MutexLock(s_gpsDataMutex);
    val = s_lastSatellites;
    HAL_OS_MutexUnlock(s_gpsDataMutex);
    return val;
}

double GPS_GetTotalKm(void)
{
    double val;
    HAL_OS_MutexLock(s_gpsDataMutex);
    val = s_totalKm;
    HAL_OS_MutexUnlock(s_gpsDataMutex);
    return val;
}

/** 1 = đang chạy (đã lọc nhiễu), 0 = dừng / đỗ */
int GPS_IsMoving(void)
{
    return s_isMovingNow;
}

/** 1 = đang đỗ */
int GPS_IsParked(void)
{
    return s_isParkedNow;
}

/* ============================== Tính toán ================================= */

/** Khoảng cách Haversine (km) */
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
 * @brief Lọc trạng thái chạy / dừng / trôi sau tắt ACC / đỗ, chống nhiễu tốc độ GPS.
 * @return 1 = đang chạy.
 */
static int FilterMovingStatus(double speed, double lat, double lon)
{
    static uint32_t s_speedAboveThresholdTick = 0;
    static uint32_t s_speedBelowThresholdTick = 0;
    static uint32_t s_parkTimeoutStartTick = 0;
    uint32_t now = GetTickNow();
    double thresh = (double)CFG_GetSpeedThresh();
    int accMode;
    int wireAccOff;

    if (thresh <= 0.0) thresh = 3.0;
    accMode = CFG_GetAccMode();
    /* Chỉ tin ACC OFF từ dây khi đã đọc được chân. accMode 2 (có đấu + không
     * đấu): xe không đấu dây thì chân luôn báo OFF (kéo lên), nên chỉ dùng
     * ACC OFF khi dây đã từng báo ON; nếu không thì chạy theo vận tốc như accMode 0. */
    wireAccOff = 0;
    if (s_accWireOn == 0) {
        if (accMode == 1) wireAccOff = 1;
        else if (accMode == 2) wireAccOff = s_accWireSeenOn;
    }

    /* 1. Dây ACC tắt (accMode 1 hoặc 2) */
    if (wireAccOff) {
        /* Ngừng cộng odometer ngay */
        s_isMovingNow = 0;
        s_speedAboveThresholdTick = 0;
        /* Bộ đếm dừng / đỗ của bước 2 phải bắt đầu lại khi ACC ON trở lại */
        s_speedBelowThresholdTick = 0;
        s_parkTimeoutStartTick = 0;

        if (s_isParkedNow) {
            /* Đang đỗ, ACC tắt: tốc độ nhiễu không làm mất điểm neo. Chỉ thoát
             * khi xe bị dời thật xa điểm neo (kéo / chở xe). */
            if (speed > thresh && lat != 0.0 && lon != 0.0 &&
                !(s_anchorLat == 0.0 && s_anchorLon == 0.0) &&
                CalculateDistanceKm(s_anchorLat, s_anchorLon, lat, lon) >= PARKED_ACC_OFF_MIN_DISPLACEMENT_KM) {
                s_isParkedNow = 0;
                s_isCoastingToPark = 1;
            }
        } else if (speed <= thresh) {
            /* Xe đã dừng hẳn -> đỗ */
            s_isCoastingToPark = 0;
            s_isParkedNow = 1;
        } else {
            /* Xe còn trôi theo quán tính */
            s_isCoastingToPark = 1;
            s_isParkedNow = 0;
        }
    } else if (s_isCoastingToPark) {
        /* ACC bật lại khi đang trôi (sụt áp / chập chờn) -> hủy đỗ */
        s_isCoastingToPark = 0;
        s_isMovingNow = (speed > thresh) ? 1 : 0;
        s_isParkedNow = 0;
    }

    /* 2. Chưa đỗ: xác nhận chạy / dừng theo thời gian, dự phòng đỗ sau 600 s */
    if (!s_isParkedNow && !s_isCoastingToPark) {
        if (speed > thresh) {
            s_speedBelowThresholdTick = 0;
            s_parkTimeoutStartTick = 0;
            if (s_speedAboveThresholdTick == 0) {
                s_speedAboveThresholdTick = now;
            } else if ((now - s_speedAboveThresholdTick) >= (MOVING_CONFIRM_SEC * HAL_TICKS_PER_SEC)) {
                s_isMovingNow = 1;
            }
        } else {
            s_speedAboveThresholdTick = 0;
            if (s_speedBelowThresholdTick == 0) {
                s_speedBelowThresholdTick = now;
            } else if ((now - s_speedBelowThresholdTick) >= (STOPPED_CONFIRM_SEC * HAL_TICKS_PER_SEC)) {
                s_isMovingNow = 0;
            }

            if (s_parkTimeoutStartTick == 0) {
                s_parkTimeoutStartTick = now;
            } else if ((now - s_parkTimeoutStartTick) >= (PARKED_CONFIRM_SEC * HAL_TICKS_PER_SEC)) {
                s_isParkedNow = 1;
                s_isMovingNow = 0;
                /* Không để mốc cũ lại: nếu không, mẫu chậm đầu tiên sau khi
                 * thoát đỗ sẽ đưa xe về ĐỖ ngay lập tức */
                s_speedBelowThresholdTick = 0;
                s_parkTimeoutStartTick = 0;
            }
        }
    }

    /* 3. Đang đỗ -> chạy: vượt ngưỡng đủ lâu và đã rời điểm neo >= 15 m */
    if (s_isParkedNow) {
        s_isMovingNow = 0;
        if (speed > thresh) {
            if (s_speedAboveThresholdTick == 0) {
                s_speedAboveThresholdTick = now;
            } else if ((now - s_speedAboveThresholdTick) >= (MOVING_CONFIRM_SEC * HAL_TICKS_PER_SEC)) {
                if (s_anchorLat == 0.0 && s_anchorLon == 0.0) {
                    s_isParkedNow = 0;
                    s_isMovingNow = 1;
                } else if (lat != 0.0 && lon != 0.0 &&
                           CalculateDistanceKm(s_anchorLat, s_anchorLon, lat, lon) >= MOVING_MIN_DISPLACEMENT_KM) {
                    s_isParkedNow = 0;
                    s_isMovingNow = 1;
                }
            }
        } else {
            s_speedAboveThresholdTick = 0;
        }
    }

    return s_isMovingNow;
}

/**
 * @brief Điểm neo khi xe dừng / đỗ. Gọi khi giữ s_gpsDataMutex, sau khi đã ghi
 *        s_lastLat / s_lastLon của mẫu mới.
 *
 * Điểm neo là trung bình các mẫu tốt trong lúc đứng yên, không phải mẫu đầu
 * tiên: một mẫu đơn lẻ ở đô thị lệch 30-40 m và trước đây giữ nguyên độ lệch
 * đó suốt lần đỗ. Mẫu HDOP cao hoặc nhảy xa khỏi trung bình bị bỏ. Neo cả khi
 * xe dừng với ACC bật (đèn đỏ, nổ máy chờ), trừ lúc đang trôi sau khi tắt ACC.
 */
static void ApplyStationaryAnchor(int isMoving)
{
    double rawLat = s_lastLat;
    double rawLon = s_lastLon;
    int good;

    if (s_lastSatellites < GPS_MIN_SATS_FOR_FIX || (rawLat == 0.0 && rawLon == 0.0)) {
        return; /* chưa fix -- không đụng điểm neo */
    }
    if (!(s_isParkedNow || (!isMoving && !s_isCoastingToPark))) {
        if (s_anchorN > 0) {
            HAL_LOG("[GNSS/Anchor] release (n=%d)", s_anchorN);
        }
        s_anchorLat = 0.0;
        s_anchorLon = 0.0;
        s_anchorN = 0;
        s_anchorGood = 0;
        s_anchorRejectRun = 0;
        return;
    }

    /* HDOP 0 = không biết -> coi là mẫu tốt */
    good = (s_lastHdop <= 0.0 || s_lastHdop <= ANCHOR_MAX_HDOP);

    if (s_anchorN == 0 || (!s_anchorGood && good)) {
        /* Bắt đầu neo; neo đang dựa trên mẫu xấu thì thay bằng mẫu tốt đầu tiên */
        s_anchorSumLat = rawLat;
        s_anchorSumLon = rawLon;
        s_anchorN = 1;
        s_anchorGood = good;
        s_anchorRejectRun = 0;
        HAL_LOG("[GNSS/Anchor] start lat=%.6f lon=%.6f hdop=%.1f", rawLat, rawLon, s_lastHdop);
    } else if (good && s_anchorN < ANCHOR_MAX_SAMPLES) {
        double meanLat = s_anchorSumLat / s_anchorN;
        double meanLon = s_anchorSumLon / s_anchorN;
        /* Vài mẫu đầu chưa đủ tin để loại; sau đó bỏ mẫu nhảy xa (đa đường) */
        if (s_anchorN >= ANCHOR_MIN_SAMPLES_FOR_REJECT &&
            CalculateDistanceKm(meanLat, meanLon, rawLat, rawLon) > ANCHOR_OUTLIER_KM) {
            /* Bị loại liên tục = vài mẫu đầu (lúc máy thu chưa ổn định) kéo
             * trung bình lệch: làm lại điểm neo từ mẫu hiện tại */
            if (++s_anchorRejectRun >= ANCHOR_RESTART_REJECTS) {
                HAL_LOG("[GNSS/Anchor] %d samples off mean, restart (n=%d)",
                        s_anchorRejectRun, s_anchorN);
                s_anchorSumLat = rawLat;
                s_anchorSumLon = rawLon;
                s_anchorN = 1;
                s_anchorRejectRun = 0;
            }
        } else {
            s_anchorRejectRun = 0;
            s_anchorSumLat += rawLat;
            s_anchorSumLon += rawLon;
            s_anchorN++;
            if ((s_anchorN % 60) == 0) {
                HAL_LOG("[GNSS/Anchor] n=%d lat=%.6f lon=%.6f",
                        s_anchorN, s_anchorSumLat / s_anchorN, s_anchorSumLon / s_anchorN);
            }
        }
    }

    s_anchorLat = s_anchorSumLat / s_anchorN;
    s_anchorLon = s_anchorSumLon / s_anchorN;
    s_lastLat = s_anchorLat;
    s_lastLon = s_anchorLon;
}

/**
 * @brief Cộng km trong ngày. Gọi khi giữ s_gpsDataMutex, sau khi đã ghi số vệ tinh.
 *
 *  - Đứng yên: không cộng (chống km ảo do nhiễu), chỉ dời điểm tham chiếu.
 *  - Bỏ bước nhảy dài hơn tốc độ cho phép trong thời gian đã trôi.
 *  - Mất GPS ngắn: khi có lại thì nối từ điểm tham chiếu cũ.
 *  - Mất GPS quá ODOM_GPS_LOSS_TIMEOUT_SEC: bỏ điểm tham chiếu, không nối.
 */
static void UpdateOdometer(double newLat, double newLon)
{
    uint32_t now = GetTickNow();
    int wasGpsLoss;

    if (s_lastSatellites < GPS_MIN_SATS_FOR_FIX || newLat == 0.0 || newLon == 0.0) {
        if (s_gpsLossStartTick == 0) {
            s_gpsLossStartTick = (now != 0) ? now : 1;
        } else if (!s_odomRefDropped &&
                   (now - s_gpsLossStartTick) >= (uint32_t)ODOM_GPS_LOSS_TIMEOUT_SEC * HAL_TICKS_PER_SEC) {
            /* Trước đây thiếu phép so sánh này: bỏ điểm tham chiếu ngay từ mẫu
             * mất GPS thứ hai (mất km qua cầu / hầm ngắn) và in log mỗi giây */
            HAL_LOG("[Odom] Mat GPS qua %ds -> bo diem tham chieu odometer", ODOM_GPS_LOSS_TIMEOUT_SEC);
            s_prevLat = 0.0;
            s_prevLon = 0.0;
            s_odomRefDropped = 1;
        }
        return;
    }

    wasGpsLoss = (s_gpsLossStartTick != 0);
    s_gpsLossStartTick = 0;
    s_odomRefDropped = 0;

    if (!s_isMovingNow) {
        s_prevLat = newLat;
        s_prevLon = newLon;
        s_prevOdomTick = now;
        return;
    }

    if (s_prevLat != 0.0 && s_prevLon != 0.0) {
        double dist = CalculateDistanceKm(s_prevLat, s_prevLon, newLat, newLon);
        double elapsedSec = (s_prevOdomTick != 0 && now > s_prevOdomTick)
                                ? (double)(now - s_prevOdomTick) / HAL_TICKS_PER_SEC
                                : 1.0;
        double calcSpeed;
        double maxDist;

        if (elapsedSec < 1.0) elapsedSec = 1.0;
        calcSpeed = (s_lastSpeedKph < (double)ODOM_JUMP_MIN_CALC_SPEED_KPH)
                        ? (double)ODOM_JUMP_MIN_CALC_SPEED_KPH
                        : s_lastSpeedKph;
        maxDist = (calcSpeed / 3.6) * elapsedSec * ODOM_MAX_JUMP_FACTOR / 1000.0;
        if (maxDist < ODOM_MAX_STATIONARY_KM) maxDist = ODOM_MAX_STATIONARY_KM;

        if (dist >= ODOM_MIN_DIST_KM && dist <= maxDist) {
            s_totalKm += dist;
            if (wasGpsLoss) {
                HAL_LOG("[Odom] Noi lai sau mat GPS ngan (%.1fs): +%.1fm",
                        elapsedSec, dist * 1000.0);
            }
        } else if (dist > maxDist) {
            HAL_LOG("[Odom] Jump filtered: %.1fm > max %.1fm (speed=%.1fkph, elapsed=%.1fs)",
                    dist * 1000.0, maxDist * 1000.0, s_lastSpeedKph, elapsedSec);
        }
    }

    s_prevLat = newLat;
    s_prevLon = newLon;
    s_prevOdomTick = now;
}

/**
 * @brief Log debug: thời gian có fix sau khi bật, mất / có lại fix, có fix sau
 *        AGPS, trạng thái định kỳ ([GNSS/Stat]). Gọi sau mỗi mẫu, ngoài khóa.
 */
static void GpsDebugOnSample(void)
{
#if GNSS_DEBUG
    static int      s_hadFix = -1;      /* -1 = chưa có mẫu nào */
    static int      s_everFix = 0;
    static uint32_t s_fixChangeTick = 0;
    static uint32_t s_statTick = 0;
    uint32_t now = GetTickNow();
    int hasFix, sats, anchorN;
    int satSys[4];
    double hdop, lat, lon, spd;

    HAL_OS_MutexLock(s_gpsDataMutex);
    sats = s_lastSatellites;
    memcpy(satSys, s_satSys, sizeof(satSys));
    hdop = s_lastHdop;
    lat = s_lastLat;
    lon = s_lastLon;
    spd = s_lastSpeedKph;
    anchorN = s_anchorN;
    HAL_OS_MutexUnlock(s_gpsDataMutex);
    hasFix = (sats >= GPS_MIN_SATS_FOR_FIX && !(lat == 0.0 && lon == 0.0));

    if (hasFix != s_hadFix) {
        if (hasFix) {
            if (!s_everFix) {
                HAL_LOG("[GNSS/Dbg] first fix %lus after boot (sats=%d hdop=%.1f)",
                        (unsigned long)(now / HAL_TICKS_PER_SEC), sats, hdop);
                s_everFix = 1;
            } else {
                HAL_LOG("[GNSS/Dbg] fix regained after %lus without fix (sats=%d hdop=%.1f)",
                        (unsigned long)((now - s_fixChangeTick) / HAL_TICKS_PER_SEC), sats, hdop);
            }
            if (s_agpsReqTick != 0) {
                HAL_LOG("[GNSS/AGPS] fix %lus after AGPS request",
                        (unsigned long)((now - s_agpsReqTick) / HAL_TICKS_PER_SEC));
                s_agpsReqTick = 0;
            }
        } else if (s_hadFix == 1) {
            HAL_LOG("[GNSS/Dbg] fix lost (sats=%d hdop=%.1f)", sats, hdop);
        }
        s_hadFix = hasFix;
        s_fixChangeTick = now;
    }

    if (s_statTick == 0 || (now - s_statTick) >= (uint32_t)GNSS_DEBUG_STATUS_SEC * HAL_TICKS_PER_SEC) {
        s_statTick = now;
        HAL_LOG("[GNSS/Stat] fix=%d sats=%d (GPS %d GLO %d GAL %d BDS %d) hdop=%.1f spd=%.1f "
                "moving=%d parked=%d coast=%d accWire=%d anchorN=%d lat=%.6f lon=%.6f",
                hasFix, sats, satSys[0], satSys[1], satSys[2], satSys[3], hdop, spd,
                s_isMovingNow, s_isParkedNow, s_isCoastingToPark, s_accWireOn, anchorN, lat, lon);
    }
#endif
}

/* ============================== Áp mẫu GNSS =============================== */

/** Áp một mẫu có vị trí (có thể dưới 5 vệ tinh) -- đường duy nhất ghi dữ liệu GPS */
static void ApplySample(const GnssSample_t *s)
{
    /* Lọc chạy / dừng trước, ngoài khóa (đọc cấu hình) */
    int isMoving = FilterMovingStatus(s->speedKph, s->lat, s->lon);

    HAL_OS_MutexLock(s_gpsDataMutex);
    s_lastSatellites = s->satellites;
    memcpy(s_satSys, s->satSys, sizeof(s_satSys));
    s_lastHdop = s->hdop;
    s_lastLat = s->lat;
    s_lastLon = s->lon;
    if (isMoving) {
        s_lastSpeedKph = s->speedKph;
        s_lastHeadingDeg = s->headingDeg;
    } else {
        s_lastSpeedKph = 0.0;
    }
    s_lastFixTick = GetTickNow();
    if (s_lastSatellites < GPS_MIN_SATS_FOR_FIX) {
        s_lastLat = 0.0;
        s_lastLon = 0.0;
    }

    UpdateOdometer(s->lat, s->lon);
    ApplyStationaryAnchor(isMoving);
    HAL_OS_MutexUnlock(s_gpsDataMutex);

    GpsDebugOnSample();
}

/** Áp dòng "không có fix" (+CGNSSINFO rỗng) */
static void ApplyNoFix(void)
{
    HAL_OS_MutexLock(s_gpsDataMutex);
    s_lastSatellites = 0;
    memset(s_satSys, 0, sizeof(s_satSys));
    s_lastHdop = 0.0;
    s_lastLat = 0.0;
    s_lastLon = 0.0;
    s_lastSpeedKph = 0.0;
    UpdateOdometer(0.0, 0.0); /* đếm thời gian mất GPS cho odometer */
    HAL_OS_MutexUnlock(s_gpsDataMutex);

    GpsDebugOnSample();
}

/* ============================== Đọc URC =================================== */

/** Tách chuỗi theo dấu phẩy ngay trên buffer. @return số trường */
static int SplitCommaInPlace(char *line, char **fields, int maxFields)
{
    int n = 0;
    char *ptr = line;

    if (!line || !fields || maxFields <= 0) return 0;
    while (n < maxFields) {
        fields[n++] = ptr;
        ptr = strchr(ptr, ',');
        if (!ptr) break;
        *ptr++ = '\0';
    }
    return n;
}

/** ddmm.mmmm (NMEA) -> độ thập phân */
static double NmeaToDegrees(const char *field)
{
    double raw = strtod(field, NULL);
    return (int)(raw / 100.0) + (fmod(raw, 100.0) / 60.0);
}

/**
 * @brief +CGNSSINFO: <mode>,<GPS-SVs>,[<GLONASS-SVs>],[<GALILEO-SVs>],[<BEIDOU-SVs>],
 *        <lat>,<N/S>,<lon>,<E/W>,<date>,<UTC>,<alt>,<speed>,<course>,<PDOP>,<HDOP>,<VDOP>
 *        Số trường vệ tinh thay đổi theo bản firmware nên tìm vị trí N/S.
 * @return 1 nếu là dòng +CGNSSINFO (kể cả không có fix), 0 nếu không phải.
 */
static int TryParseCgnssInfoUrc(const char *gpsUrc)
{
    const char *hdr = strstr(gpsUrc, "+CGNSSINFO:");
    char buf[256];
    char *fields[20];
    GnssSample_t s;
    size_t len;
    int nf;
    int ns_idx = -1;
    int i;

    if (!hdr) return 0;

    len = strlen(hdr + 11);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, hdr + 11, len);
    buf[len] = '\0';

    nf = SplitCommaInPlace(buf, fields, 20);
    if (nf < 13) {
        static uint32_t lastNoFixLogTick = 0;
        uint32_t now = GetTickNow();
        ApplyNoFix();
        if (lastNoFixLogTick == 0 || (now - lastNoFixLogTick) >= (uint32_t)(5 * HAL_TICKS_PER_SEC)) {
            lastNoFixLogTick = now;
            HAL_LOG("[GNSS] No fix (nf=%d)", nf);
        }
        return 1;
    }

    for (i = 3; i < 7 && i < nf; i++) {
        if (fields[i][0] == 'N' || fields[i][0] == 'S') { ns_idx = i; break; }
    }
    if (ns_idx == -1 || ns_idx + 4 >= nf) return 0;

    memset(&s, 0, sizeof(s));
    /* Cộng vệ tinh mọi hệ (các trường giữa <mode> và <lat>) */
    for (i = 1; i < ns_idx - 1; i++) {
        int n = (fields[i][0] != '\0') ? atoi(fields[i]) : 0;
        s.satellites += n;
        if (i - 1 < 4) s.satSys[i - 1] = n;
    }

    s.lat = NmeaToDegrees(fields[ns_idx - 1]);
    s.lon = NmeaToDegrees(fields[ns_idx + 1]);
    if (fields[ns_idx][0] == 'S') s.lat = -s.lat;
    if (fields[ns_idx + 2][0] == 'W') s.lon = -s.lon;
    s.speedKph   = (ns_idx + 6 < nf) ? strtod(fields[ns_idx + 6], NULL) * KNOTS_TO_KMH : 0.0;
    s.headingDeg = (ns_idx + 7 < nf) ? strtod(fields[ns_idx + 7], NULL) : 0.0;
    s.hdop       = (ns_idx + 9 < nf && fields[ns_idx + 9][0] != '\0')
                       ? strtod(fields[ns_idx + 9], NULL) : 0.0;

    ApplySample(&s);

    if (fields[ns_idx + 3][0] != '\0' && fields[ns_idx + 4][0] != '\0') {
        HalDateTime_t utc = {0};
        if (ParseDateTimeFromUrcTokens(fields[ns_idx + 3], fields[ns_idx + 4], &utc))
            StoreGnssUtcTime(&utc);
    }
    return 1;
}

/**
 * @brief +CGPSINFO: <lat>,<N/S>,<lon>,<E/W>,<date>,<UTC>,<alt>,<speed>,<course>,<sats>
 *        (dạng URC chỉ GPS, khi bật bằng sAPI_GpsInfoGet; không có HDOP).
 * @return 1 nếu đọc được, 0 nếu không phải / sai định dạng.
 */
static int TryParseCgpsInfoUrc(const char *gpsUrc)
{
    const char *hdr = strstr(gpsUrc, "+CGPSINFO:");
    char buf[256];
    char *fields[16];
    GnssSample_t s;
    HalDateTime_t utc = {0};
    int nf;

    if (!hdr) return 0;

    strncpy(buf, hdr + 10, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    nf = SplitCommaInPlace(buf, fields, 16);
    if (nf < 8 || fields[0][0] == '\0' || fields[2][0] == '\0') return 0;
    if (!ParseDateTimeFromUrcTokens(fields[4], fields[5], &utc)) return 0;

    memset(&s, 0, sizeof(s));
    s.lat = NmeaToDegrees(fields[0]);
    s.lon = NmeaToDegrees(fields[2]);
    if (fields[1][0] == 'S' || fields[1][0] == 's') s.lat = -s.lat;
    if (fields[3][0] == 'W' || fields[3][0] == 'w') s.lon = -s.lon;
    s.speedKph   = strtod(fields[7], NULL) * KNOTS_TO_KMH;
    s.headingDeg = (nf > 8) ? strtod(fields[8], NULL) : 0.0;
    s.satellites = (nf > 9) ? atoi(fields[9]) : 0;
    s.satSys[0]  = s.satellites; /* chỉ GPS */

    ApplySample(&s);
    StoreGnssUtcTime(&utc);
    return 1;
}

/** Tìm cặp ",ddmmyy,hhmmss" trong dòng URC bất kỳ để lấy giờ UTC */
static int TryParseGnssDateTimeFromUrc(const char *gpsUrc, HalDateTime_t *out)
{
    const char *p;
    size_t len;
    size_t i;

    if ((gpsUrc == NULL) || (out == NULL)) return 0;
    len = strlen(gpsUrc);
    for (i = 0; i + 13 < len; i++) {
        p = gpsUrc + i;
        if ((p[0] == ',') &&
            IsDigitChar(p[1]) && IsDigitChar(p[2]) && IsDigitChar(p[3]) &&
            IsDigitChar(p[4]) && IsDigitChar(p[5]) && IsDigitChar(p[6]) &&
            (p[7] == ',') &&
            IsDigitChar(p[8]) && IsDigitChar(p[9]) && IsDigitChar(p[10]) &&
            IsDigitChar(p[11]) && IsDigitChar(p[12]) && IsDigitChar(p[13])) {
            if (ParseDateTimeFromUrcTokens(p + 1, p + 8, out)) return 1;
        }
    }
    return 0;
}

/**
 * @brief Nhận một dòng URC GNSS từ gnss_task.
 *
 * Chỉ +CGNSSINFO và +CGPSINFO được dùng làm vị trí. Dòng khác chỉ được dò giờ
 * UTC: bộ đọc tọa độ dự phòng cũ coi hai số bất kỳ trong khoảng ±90 / ±180 là
 * vĩ độ / kinh độ và giữ nguyên số vệ tinh cũ, nên có thể nhận vị trí sai.
 */
void GPS_OnUrcString(const char *gpsUrc)
{
    HalDateTime_t utc = {0};

    if (gpsUrc == NULL) return;

    if (TryParseCgnssInfoUrc(gpsUrc) || TryParseCgpsInfoUrc(gpsUrc)) {
        if (!s_hasGnssTime && TryParseGnssDateTimeFromUrc(gpsUrc, &utc))
            StoreGnssUtcTime(&utc);
        return;
    }

    if (TryParseGnssDateTimeFromUrc(gpsUrc, &utc))
        StoreGnssUtcTime(&utc);
}

/* ============================== AGPS ====================================== */

/**
 * @brief Tải dữ liệu AGPS khi có mạng mà chưa bắt được vệ tinh. Gọi mỗi vòng
 *        khi đang có phiên với Server.
 *
 * Theo A76xx GNSS Application Note: cần giờ hệ thống hợp lệ trước khi tải, và
 * không gọi quá 12 lần / ngày (gọi dày có thể làm treo hệ thống), nên mỗi lần
 * cách nhau tối thiểu AGPS_MIN_INTERVAL_SEC.
 */
void GPS_AgpsPoll(void)
{
#if AGPS_ENABLE
    static int      s_agpsDone = 0;
    static uint32_t s_agpsLastTick = 0;
    static const char *s_lastReason = NULL;
    static uint32_t s_reasonLogTick = 0;
    const char *reason = NULL;
    uint32_t now = GetTickNow();
    HalDateTime_t rtc;
    int hasFix;
    int sats;
    char why[64];

    HAL_OS_MutexLock(s_gpsDataMutex);
    sats = s_lastSatellites;
    hasFix = (sats >= GPS_MIN_SATS_FOR_FIX && !(s_lastLat == 0.0 && s_lastLon == 0.0));
    HAL_OS_MutexUnlock(s_gpsDataMutex);
    if (hasFix) {
        s_lastReason = NULL;
        return;
    }

    /* Lý do chưa tải AGPS (chỉ để debug, in khi đổi lý do hoặc mỗi 30 s) */
    if (now < (uint32_t)AGPS_NO_FIX_WAIT_SEC * HAL_TICKS_PER_SEC) {
        reason = "wait";
        snprintf(why, sizeof(why), "uptime %lus < %ds",
                 (unsigned long)(now / HAL_TICKS_PER_SEC), AGPS_NO_FIX_WAIT_SEC);
    } else if (s_agpsDone &&
               (now - s_agpsLastTick) < (uint32_t)AGPS_MIN_INTERVAL_SEC * HAL_TICKS_PER_SEC) {
        reason = "interval";
        snprintf(why, sizeof(why), "next AGPS in %lus",
                 (unsigned long)(AGPS_MIN_INTERVAL_SEC - (now - s_agpsLastTick) / HAL_TICKS_PER_SEC));
    } else if (HAL_GNSS_GetRtc(&rtc) != 0 || rtc.tm_year < 2020 || rtc.tm_year > 2100) {
        reason = "time";
        snprintf(why, sizeof(why), "system time invalid (year %d), no NITZ/NTP yet", (int)rtc.tm_year);
    } else if (HAL_GNSS_AgpsRequest() == 0) {
        s_agpsDone = 1;
        s_agpsLastTick = now;
        s_agpsReqTick = (now == 0) ? 1 : now;
        s_lastReason = NULL;
        HAL_LOG("[GNSS/AGPS] no fix after %lus, request AGPS (sats=%d)",
                (unsigned long)(now / HAL_TICKS_PER_SEC), sats);
        return;
    } else {
        reason = "busy";
        snprintf(why, sizeof(why), "GNSS off or AGPS still running");
    }

#if GNSS_DEBUG
    if (reason != s_lastReason ||
        (now - s_reasonLogTick) >= (uint32_t)GNSS_DEBUG_STATUS_SEC * HAL_TICKS_PER_SEC) {
        HAL_LOG("[GNSS/AGPS] skip: %s", why);
        s_reasonLogTick = now;
    }
#endif
    s_lastReason = reason;
#endif
}
