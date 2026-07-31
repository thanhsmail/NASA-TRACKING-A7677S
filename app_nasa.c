/**
 * @file app_nasa.c
 * @brief NASA Tracking application logic
 *
 * Luu y: File nay KHONG #include simcom_api.h / simcom_common.h truc tiep.
 * Moi tuong tac phan cung di qua HAL layer.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_gnss.h"
#include "hal/hal_net.h"
#include "hal/hal_gpio.h"
#include "hal/hal_adc.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include "app_cfg.h"
#include "app_cmd.h"
#include "app_network.h"
#include "app_backup.h"
#include "drv_en25qh64a.h"
#include "app_nasa.h"
#include "app_park_rules.h"
#include "app_lxlt_rules.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    STATE_INIT = 1,
    STATE_CONNECT = 2,
    STATE_LOGIN = 3,
    STATE_TRACKING = 4,
    STATE_ERROR_RETRY = 5
} NasaState_t;

static NasaState_t s_nasaState = STATE_INIT;
static uint32_t s_messageIdNum = 1;
static int s_accRaw = 0;          /* 1 = ACC ON (chân ACC active-low, đã quy đổi ở GPIO task) */
static int s_loginAcked = 0;
static char s_resetReason[16] = "R-0.0";
static char s_loginCode[16] = "0";
static char s_devImei[32] = {0};
static char s_devIccid[32] = {0};
static char s_serverLastPacket[40] = {0};
static uint32_t s_lastWatchdogFeedTick = 0;
static uint32_t s_disconnStartTick = 0;

/* Bộ Quy tắc đỗ xe & LXLT */
static ParkState_t s_parkState;
static LxltState_t s_lxltState;

static uint32_t    s_lastTrackingTick = 0;
static int         s_prevAccOn = -1;
static volatile int s_trackingImmediateSend = 0;
static volatile int s_immediateMsgType = 2; /* 2 or 7 */
static volatile int s_requestReplayAll = 0;
static double      s_prevSentHeadingDeg = -1.0;

static int NetworkSendFrame(const char *frame);

float NASA_GetVoltage(void)
{
    /* Đọc giá trị điện áp thực tế tại chân ADC1 (ADC.PWR) qua HAL (đơn vị: mV) */
    uint32_t adc_mv = HAL_ADC_ReadMv(1);

    /* Mạch phân áp R306 (220K) & R307 (10K): Tỷ lệ phân áp (220+10)/10 = 23.
     * Điện áp nguồn POWER = V_ADC1 * 23
     */
    if (adc_mv == 0) {
        /* Fallback 12V nếu ADC chưa đo được hoặc lỗi kênh */
        return 12.0f;
    }
    return ((float)adc_mv * 23.0f) / 1000.0f;
}

static int IsAccOn(void)
{
    int mode = CFG_GetAccMode();
    int bySpeed = GPS_IsMoving();
    int byWire = s_accRaw ? 1 : 0; /* Chân ACC active-low; s_accRaw đã là trạng thái logic ON/OFF */

    if (mode == 0) return bySpeed;
    if (mode == 2) return (byWire || bySpeed) ? 1 : 0;
    return byWire; /* mode 1: chỉ theo dây ACC */
}

static uint32_t BuildDeviceStatus(const GpsSnapshot_t *gps, float voltage)
{
    uint32_t st = 0;
    if (EN25_IsReady()) st |= ST_FLASH_OK;
    if (gps && gps->valid) st |= ST_GPS;
    if (IsAccOn()) st |= ST_ACC;
    if (CFG_IsDriverLoggedIn()) st |= ST_DRIVER;
    if (gps && gps->speedKph >= (double)NASA_SPEED_LIMIT_KPH) st |= ST_SPEED_VIOLATION;
    if (s_lxltState.workActive && s_lxltState.lxltCounted4h) st |= ST_LXLT_VIOLATION;
    if (voltage >= NASA_BATTERY_LOW_VOLT) st |= ST_BATTERY_OK;
    return st;
}

/* Checksum theo firmware cũ: payload*checksum\r (login dùng \r\n) */
static void AppendChecksumAndEnd(char *out, size_t size, const char *payload, int loginStyle)
{
    uint32_t checksum = Buffer_GetChecksum((const uint8_t *)payload, (uint32_t)strlen(payload));
    if (loginStyle) {
        (void)snprintf(out, size, "%s*%lu\r\n", payload, (unsigned long)checksum);
    } else {
        (void)snprintf(out, size, "%s*%lu\r", payload, (unsigned long)checksum);
    }
}

static void BuildLoginFrame(char *out, size_t size, const char *msgId, const char *dateTime)
{
    static char payload[320];
    (void)snprintf(payload, sizeof(payload),
                   "!NASA,1,%s,%s,%s,%s,%s,%s,%s,%s,%s",
                   msgId, dateTime, s_devImei, s_devIccid,
                   CFG_GetPlate(), NASA_HW_CODE, NASA_FW_CODE,
                   s_loginCode, s_resetReason);
    AppendChecksumAndEnd(out, size, payload, 1);
}

static void BuildTrackPayload(char *payload, size_t size, int msgCode,
                              const char *msgId, const char *dateTime,
                              const GpsSnapshot_t *gps, uint8_t csq,
                              uint32_t status, float voltage)
{
    /* Km tích lũy trong ngày là biến 2 byte (uint16_t), mỗi đơn vị tương ứng 10 mét (0.01 km) */
    uint32_t odom10mCalc = (uint32_t)(gps->totalKm * 100.0 + 0.5);
    uint16_t totalKm10m = (odom10mCalc > 65535) ? 65535 : (uint16_t)odom10mCalc;

    /* Nhiên liệu RS232 / xung / nhiệt độ: không lắp → 0 */
    (void)snprintf(payload, size,
                   "!NASA,%d,%s,%s,%.6f,%.6f,%.0f,%lu,%u,%d,%u,%.0f,%.1f,0,0,0,",
                   msgCode, msgId, dateTime,
                   gps->lat, gps->lon, gps->speedKph,
                   (unsigned long)status, (unsigned)csq, gps->satellites,
                   (unsigned int)totalKm10m, gps->headingDeg, (double)voltage);
}

static void NASA_WorkSendEvent(const LxltEvent_t *evt)
{
    static char frame[480];
    char msgIdStr[16] = {0};

    if (!evt || evt->type == LXLT_EVENT_NONE) return;
    if (!Network_IsConnected() || !s_loginAcked) return;

    frame[0] = '\0';
    (void)snprintf(msgIdStr, sizeof(msgIdStr), "%lu", (unsigned long)s_messageIdNum);
    BuildWorkFramePayload(frame, sizeof(frame), msgIdStr, evt->dateTime, (int)evt->type,
                          CFG_GetDriverName(), CFG_GetDriverLicense(),
                          &s_lxltState, GetTickNow(), (uint32_t)HAL_TICKS_PER_SEC,
                          GPS_GetTotalKm(), evt->dateTime, evt->lat, evt->lon);
    HAL_LOG("[NASA/Work] type=%d %s", (int)evt->type, frame);
    if (NetworkSendFrame(frame) == 0) s_messageIdNum++;
}
    
static void NASA_WorkUpdate(const char *dateTime, double lat, double lon, double speed)
{
    (void)speed;
    (void)lat;
    (void)lon;
    GpsSnapshot_t gps = {0};
    LxltEvent_t evt = {0};
    uint32_t nowTick = GetTickNow();

    GPS_Snapshot(&gps);
    if (LxltRules_Process(&s_lxltState, &gps, GPS_IsMoving(), nowTick,
                          (uint32_t)HAL_TICKS_PER_SEC, dateTime, &evt)) {
        if (evt.type == LXLT_EVENT_SEND_TYPE1) {
            CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 1);
        } else if (evt.type == LXLT_EVENT_SEND_TYPE3) {
            CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 0);
        }
        NASA_WorkSendEvent(&evt);
    }
}

// Xử lý phát hiện lý do Reset
static void DetectResetReason(void)
{
    HalPowerUpReason_t r = HAL_OS_GetPowerUpReason();
    if (r == HAL_POWER_UP_SOFTWARE_RESET) {
        strncpy(s_resetReason, "R-3.0", sizeof(s_resetReason) - 1);
    } else if (r == HAL_POWER_UP_RESET_KEY || r == HAL_POWER_UP_POWER_KEY) {
        strncpy(s_resetReason, "R-2.0", sizeof(s_resetReason) - 1);
    } else {
        strncpy(s_resetReason, "R-0.0", sizeof(s_resetReason) - 1);
    }
}

// Xử lý Tạo mã đăng nhập
static void MakeLoginCode(void)
{
    uint32_t t = GetTickNow();
    (void)snprintf(s_loginCode, sizeof(s_loginCode), "%06lu", (unsigned long)(t % 1000000u));
}
// Xử lý Reset Bộ đếm hàng ngày
static void ResetDailyCountersIfNeeded(int today_mday)
{
    if (today_mday < 1 || today_mday > 31) return;
    AppConfig_t *cfg = CFG_Get();
    if (cfg->lastMday == 0) {
        cfg->lastMday = today_mday;
        CFG_Save();
        return;
    }
    if (today_mday != cfg->lastMday) {
        GPS_ResetOdometer();
        ParkRules_ResetDailyCount(&s_parkState);
        LxltRules_ResetDailyCount(&s_lxltState);
        s_messageIdNum = 1;
        cfg->operateDays++;
        cfg->lastMday = today_mday;
        CFG_Save();
    }
}
// Xử lý gửi Frame mạng
static int NetworkSendFrame(const char *frame)
{
    int ret = Network_Send(frame, (uint32_t)strlen(frame));
    return (ret > 0) ? 0 : -1;
}

// Xử lý gửi sự kiện đỗ xe
static void NASA_ParkSendEvent(const ParkEvent_t *evt)
{
    static char frame[400];
    char msgIdStr[16] = {0};

    if (!evt || evt->type == PARK_EVENT_NONE) return;
    if (!Network_IsConnected() || !s_loginAcked) return;

    (void)snprintf(msgIdStr, sizeof(msgIdStr), "%lu", (unsigned long)s_messageIdNum);
    BuildParkingFramePayload(frame, sizeof(frame), msgIdStr, evt->dateTime, (int)evt->type,
                              (evt->type == PARK_EVENT_SEND_TYPE1) ? "" : evt->dateTime,
                              CFG_GetDriverName(), CFG_GetDriverLicense(),
                              s_parkState.parkStartDateTime, evt->lat, evt->lon,
                              evt->stopSec, evt->dailyCount);
    HAL_LOG("[NASA/Park] type=%d %s", (int)evt->type, frame);
    if (NetworkSendFrame(frame) == 0) s_messageIdNum++;
}

static void NASA_ParkUpdate(const char *dateTime, double lat, double lon, double speed)
{
    (void)speed;
    (void)lat;
    (void)lon;
    GpsSnapshot_t gps = {0};
    ParkEvent_t evt = {0};
    uint32_t nowTick = GetTickNow();

    GPS_Snapshot(&gps);
    if (ParkRules_Process(&s_parkState, &gps, GPS_IsParked(), GPS_IsMoving(),
                          nowTick, (uint32_t)HAL_TICKS_PER_SEC,
                          CFG_GetParkConfirmSec(), dateTime, &evt)) {
        if (evt.type == PARK_EVENT_SEND_TYPE3) {
            s_trackingImmediateSend = 1;
            s_immediateMsgType = 2;
        }
        NASA_ParkSendEvent(&evt);
    }
}

// thay đổi tốc độ
static int GetAdaptivePeriodSec(double speedKph, double headingDeg)
{
    if (speedKph < HEADING_MIN_SPEED_KPH) return CFG_GetPeriodMoving();
    if (s_prevSentHeadingDeg < 0.0) return CFG_GetPeriodMoving();

    {
        double delta = headingDeg - s_prevSentHeadingDeg;
        if (delta > 180.0) delta -= 360.0;
        if (delta < -180.0) delta += 360.0;
        if (delta < 0.0) delta = -delta;

        if (delta >= HEADING_THRESHOLD_SHARP_DEG) return HEADING_PERIOD_SHARP_SEC;
        if (delta >= HEADING_THRESHOLD_TURN_DEG) {
            int turnPeriod = (int)((double)CFG_GetPeriodMoving() * HEADING_PERIOD_TURN_FACTOR);
            if (turnPeriod < TRACKING_PERIOD_MIN_SEC) turnPeriod = TRACKING_PERIOD_MIN_SEC;
            return turnPeriod;
        }
    }
    return CFG_GetPeriodMoving();
}
// Xử lý điền thông tin Backup từ GPS
static void FillBackupFromGps(BackupRecord_t *rec, const GpsSnapshot_t *gps,
                              const char *dateTime, uint8_t csq, uint32_t status, float voltage)
{
    char dtFixed[40];
    memset(rec, 0, sizeof(*rec));
    if (dateTime == NULL || dateTime[0] == '\0' ||
        strncmp(dateTime, "0000-00-00", 10) == 0) {
        BuildDateTimeAutoFallback(dtFixed, sizeof(dtFixed));
        dateTime = dtFixed;
    }
    strncpy(rec->datetime, dateTime, sizeof(rec->datetime) - 1);
    rec->lat = gps->lat;
    rec->lon = gps->lon;
    rec->speedKph = gps->speedKph;
    rec->deviceStatus = status;
    rec->csq = csq;
    rec->sats = gps->satellites;
    rec->totalKm = gps->totalKm;
    rec->headingDeg = gps->headingDeg;
    rec->voltage = voltage;
}

// Xử lý gửi dữ liệu Track hoặc Backup
static int SendTrackOrBackup(int msgCode, const char *dateTime, uint8_t csq)
{
    static char frame[360];
    static char payload[320];
    char msgId[16] = {0};
    GpsSnapshot_t gps = {0};
    float voltage = NASA_GetVoltage();
    uint32_t status;
    BackupRecord_t bak;

    GPS_Snapshot(&gps);
    status = BuildDeviceStatus(&gps, voltage);
    (void)snprintf(msgId, sizeof(msgId), "%lu", (unsigned long)s_messageIdNum);
    BuildTrackPayload(payload, sizeof(payload), msgCode, msgId, dateTime, &gps, csq, status, voltage);
    AppendChecksumAndEnd(frame, sizeof(frame), payload, 0);

    FillBackupFromGps(&bak, &gps, dateTime, csq, status, voltage);

    if (!Network_IsConnected() || !s_loginAcked) {
        Backup_Push(&bak);
        return -1;
    }

    HAL_LOG("[NASA] msg=%d %s", msgCode, frame);
    if (NetworkSendFrame(frame) < 0) {
        Backup_Push(&bak);
        return -1;
    }
    s_prevSentHeadingDeg = gps.headingDeg;
    s_messageIdNum++;
    return 0;
}

// Xử lý phát lại bản tin
static int ReplayOneBackup(void)
{
    BackupRecord_t rec;
    char frame[360] = {0};
    char payload[320] = {0};
    char msgId[16] = {0};
    GpsSnapshot_t gps;

    /* Peek trước — chỉ CommitPop sau khi gửi TCP thành công. */
    if (!Backup_Peek(&rec)) return 0;

    memset(&gps, 0, sizeof(gps));
    gps.lat = rec.lat;
    gps.lon = rec.lon;
    gps.speedKph = rec.speedKph;
    gps.satellites = rec.sats;
    gps.totalKm = rec.totalKm;
    gps.headingDeg = rec.headingDeg;
    gps.valid = (rec.lat != 0.0 || rec.lon != 0.0);

    if (rec.datetime[0] == '\0' || strncmp(rec.datetime, "0000-00-00", 10) == 0) {
        BuildDateTimeAutoFallback(rec.datetime, sizeof(rec.datetime));
    }

    (void)snprintf(msgId, sizeof(msgId), "%lu", (unsigned long)s_messageIdNum);
    BuildTrackPayload(payload, sizeof(payload), 7, msgId, rec.datetime, &gps,
                      rec.csq, rec.deviceStatus, rec.voltage);
    AppendChecksumAndEnd(frame, sizeof(frame), payload, 0);

    HAL_LOG("[NASA/Replay] %s", frame);
    if (NetworkSendFrame(frame) < 0) {
        return -1; /* giu nguyen hang doi, thu lai sau */
    }
    Backup_CommitPop();
    s_messageIdNum++;
    return 1;
}
// Xử lý phát lại chuỗi dữ liệu
static void ReplayBurst(void)
{
    int i;
    for (i = 0; i < NASA_BACKUP_REPLAY_BURST; i++) {
        int r = ReplayOneBackup();
        if (r <= 0) break;
    }
    Backup_Flush();
}
// Xử lý chuỗi dữ liệu nhận từ server
void NASA_OnServerProtocolLine(const char *line)
{
    if (!line) return;
    if (strncmp(line, "!NASA,1,", 8) == 0) {
        char tmp[128];
        char *fields[6];
        int n = 0;
        char *p;
        strncpy(tmp, line, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        p = tmp;
        while (n < 6) {
            fields[n++] = p;
            p = strchr(p, ',');
            if (!p) break;
            *p++ = '\0';
        }
        if (n >= 4) {
            strncpy(s_serverLastPacket, fields[3], sizeof(s_serverLastPacket) - 1);
            s_serverLastPacket[sizeof(s_serverLastPacket) - 1] = '\0';
        }
        s_loginAcked = 1;
        HAL_LOG("[NASA] Login ACK OK lastPacket=%s", s_serverLastPacket);
    } else {
        HAL_LOG("[NASA] Unhandled protocol line: %s", line);
    }
}
// Xử lý trạng thái ban đầu
static NasaState_t Handle_StateInit(int pdp_id)
{
    if (!CFG_IsDeviceEnabled()) {
        HAL_LOG("[NASA] Device disabled (cmd38), sleeping...");
        HAL_OS_TaskSleep(10 * HAL_TICKS_PER_SEC);
        return STATE_INIT;
    }
    if (Network_ActivatePdp(pdp_id) == 0) {
        TriggerNtpSyncIfNeeded();
        return STATE_CONNECT;
    }
    return STATE_ERROR_RETRY;
}

// Xử lý trạng thái kết nối
static NasaState_t Handle_StateConnect(void)
{
    if (Network_Connect(CFG_GetServerHost(), CFG_GetServerPort()) == 0)
        return STATE_LOGIN;
    return STATE_ERROR_RETRY;
}
// Xử lý trạng thái đăng nhập
static NasaState_t Handle_StateLogin(void)
{
    char dateTime[40] = {0};
    char msgId[16] = {0};
    char frame[360] = {0};
    uint32_t startTick;
    uint32_t timeoutTicks = NASA_LOGIN_ACK_TIMEOUT_SEC * HAL_TICKS_PER_SEC;

    s_loginAcked = 0;
    MakeLoginCode();
    BuildDateTimeAutoFallback(dateTime, sizeof(dateTime));
    (void)snprintf(msgId, sizeof(msgId), "%lu", (unsigned long)s_messageIdNum);
    BuildLoginFrame(frame, sizeof(frame), msgId, dateTime);

    HAL_LOG("[NASA/Login] %s", frame);
    HAL_LOG("[NASA/Login] wait ACK...");
    if (NetworkSendFrame(frame) < 0) return STATE_ERROR_RETRY;
    s_messageIdNum++;

    startTick = GetTickNow();
    while (!s_loginAcked) {
        NASA_FeedWatchdog();
        if (Network_RecvPoll() != 0) return STATE_ERROR_RETRY;
        if ((GetTickNow() - startTick) >= timeoutTicks) {
            HAL_LOG("[NASA/Login] ACK timeout");
            return STATE_ERROR_RETRY;
        }
        HAL_OS_TaskSleep(HAL_TICKS_PER_SEC / 5);
    }

    HAL_LOG("[NASA/Login] ACK ok -> TRACKING");
    s_lastTrackingTick = 0;
    s_trackingImmediateSend = 1;
    s_immediateMsgType = 2;
    s_prevSentHeadingDeg = -1.0;
    s_requestReplayAll = 1;
    return STATE_TRACKING;
}

// Xử lý trạng thái định vị và gửi dữ liệu
static NasaState_t Handle_StateTracking(void)
{
    HalDateTime_t rtc;
    char dtBuf[40];

    HAL_OS_TaskSleep(TASK_BASE_SLEEP_TICKS);

    if (!CFG_IsDeviceEnabled()) {
        Network_Disconnect();
        return STATE_INIT;
    }

    if (Network_RecvPoll() != 0) {
        s_loginAcked = 0;
        return STATE_ERROR_RETRY;
    }

    if (s_requestReplayAll) {
        s_requestReplayAll = 0;
        ReplayBurst();
    } else if (Backup_Count() > 0) {
        ReplayBurst();
    }

    HAL_GNSS_GetRtc(&rtc);
    if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100) {
        ResetDailyCountersIfNeeded(rtc.tm_mday);

        /* Lich reset cmd 32 */
        if (CFG_Get()->resetEveryDays > 0 &&
            rtc.tm_hour == CFG_Get()->resetAtHour &&
            rtc.tm_min == 0) {
            static int lastResetDay = -1;
            if (lastResetDay != rtc.tm_mday) {
                lastResetDay = rtc.tm_mday;
                HAL_OS_SysReset();
            }
        }
    }
    BuildDateTimeAutoFallback(dtBuf, sizeof(dtBuf));
    {
        GpsSnapshot_t gpsTemp = {0};
        GPS_Snapshot(&gpsTemp);
        NASA_ParkUpdate(dtBuf, gpsTemp.lat, gpsTemp.lon, gpsTemp.speedKph);
        NASA_WorkUpdate(dtBuf, gpsTemp.lat, gpsTemp.lon, gpsTemp.speedKph);

        {
            int accOn = IsAccOn();
            if (s_prevAccOn != accOn) {
                s_prevAccOn = accOn;
                s_trackingImmediateSend = 1;
                s_immediateMsgType = 2;
            }

            {
                int periodSec;
                uint32_t nowTick = GetTickNow();
                uint32_t periodTicks;

                if (!accOn) periodSec = CFG_GetPeriodStopped();
                else periodSec = GetAdaptivePeriodSec(gpsTemp.speedKph, gpsTemp.headingDeg);

                periodTicks = (uint32_t)periodSec * (uint32_t)HAL_TICKS_PER_SEC;

                if (s_trackingImmediateSend || s_lastTrackingTick == 0 || (nowTick - s_lastTrackingTick) >= periodTicks) {
                    uint8_t csq = 0;
                    int msg = s_immediateMsgType;
                    HAL_NET_GetCsq(&csq);
                    s_trackingImmediateSend = 0;
                    s_immediateMsgType = 2;
                    if (SendTrackOrBackup(msg, dtBuf, csq) < 0)
                        return STATE_ERROR_RETRY;
                    s_lastTrackingTick = nowTick;
                    HAL_LOG("[NASA/Tracking] Report sent OK (type=%d, acc=%d, next_in=%ds)", msg, accOn, periodSec);
                } else {
                    if (!Network_IsConnected() || !s_loginAcked) {
                        uint8_t csq = 0;
                        float voltage = NASA_GetVoltage();
                        uint32_t status = BuildDeviceStatus(&gpsTemp, voltage);
                        BackupRecord_t bak;
                        HAL_NET_GetCsq(&csq);
                        FillBackupFromGps(&bak, &gpsTemp, dtBuf, csq, status, voltage);
                        Backup_OnTick(&bak, nowTick);
                    }
                }
            }
        }
    }

    return STATE_TRACKING;
}

// Xử lý khởi tạo
void NASA_Init(void)
{
    s_nasaState = STATE_INIT;
    s_messageIdNum = 1;
    s_loginAcked = 0;

    ParkRules_Init(&s_parkState);
    LxltRules_Init(&s_lxltState);

    DetectResetReason();
    Backup_Init();

    if (CFG_IsDriverLoggedIn()) {
        CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 0);
        CFG_Save();
    }

    if (HAL_NET_GetImei(s_devImei, sizeof(s_devImei)) != 0)
        strncpy(s_devImei, "UNKNOWN_IMEI", sizeof(s_devImei) - 1);
    if (HAL_NET_GetIccid(s_devIccid, sizeof(s_devIccid)) != 0)
        strncpy(s_devIccid, "UNKNOWN_SIM", sizeof(s_devIccid) - 1);

    HAL_LOG("[NASA] Init IMEI=%s ICCID=%s reset=%s", s_devImei, s_devIccid, s_resetReason);
    s_disconnStartTick = 0;
    NASA_FeedWatchdog();
}
// Xử lý làm mới Watchdog
void NASA_FeedWatchdog(void)
{
    s_lastWatchdogFeedTick = GetTickNow();
}
// Kiểm tra Watchdog
int NASA_IsWatchdogTimeout(void)
{
    uint32_t now = GetTickNow();
    if (s_lastWatchdogFeedTick == 0) return 0;
    if (now >= s_lastWatchdogFeedTick) {
        return ((now - s_lastWatchdogFeedTick) >= (uint32_t)(180 * HAL_TICKS_PER_SEC)) ? 1 : 0;
    }
    return 0;
}

#define NET_DISCONN_RESET_SEC (15 * 60) /* 15 phút = 900s */
// Xử lý kiểm tra timeout kết nối
static void CheckNetworkDisconnTimeout(void)
{
    uint32_t now = GetTickNow();
    if (!CFG_IsDeviceEnabled()) {
        s_disconnStartTick = 0;
        return;
    }

    if (Network_IsConnected() && s_loginAcked) {
        s_disconnStartTick = 0;
        return;
    }

    if (s_disconnStartTick == 0) {
        s_disconnStartTick = (now != 0) ? now : 1;
        return;
    }

    if (now >= s_disconnStartTick &&
        (now - s_disconnStartTick) >= (uint32_t)(NET_DISCONN_RESET_SEC * HAL_TICKS_PER_SEC)) {
        HAL_LOG("[NASA] Mat ket noi >15 phut (%ds) -> Tu dong SysReset modem...", NET_DISCONN_RESET_SEC);
        HAL_OS_TaskSleep(200);
        HAL_OS_SysReset();
    }
}

// Xử lý logic chính của NASA
void NASA_RunStep(void)
{
    static uint32_t s_lastCfgFlushTick = 0;
    uint32_t nowFlush = GetTickNow();

    NASA_FeedWatchdog();
    CheckNetworkDisconnTimeout();
    if (s_lastCfgFlushTick == 0 ||
        (nowFlush - s_lastCfgFlushTick) >= (uint32_t)(10 * HAL_TICKS_PER_SEC)) {
        s_lastCfgFlushTick = nowFlush;
        CFG_FlushDirty();
    }
    switch (s_nasaState) {
    case STATE_INIT:
        s_nasaState = Handle_StateInit(NASA_PDP_ID);
        break;
    case STATE_CONNECT:
        s_nasaState = Handle_StateConnect();
        break;
    case STATE_LOGIN:
        s_nasaState = Handle_StateLogin();
        break;
    case STATE_TRACKING:
        s_nasaState = Handle_StateTracking();
        break;
    case STATE_ERROR_RETRY:
    default:
        HAL_LOG("[NASA/Retry] wait %ds", ERROR_RETRY_DELAY_SEC);
        Network_Disconnect();
        s_loginAcked = 0;
        {
            int sec;
            for (sec = 0; sec < ERROR_RETRY_DELAY_SEC; sec++) {
                NASA_FeedWatchdog();
                CheckNetworkDisconnTimeout();
                HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
                {
                    char dtBuf[40] = {0};
                    GpsSnapshot_t gps = {0};
                    uint8_t csq = 0;
                    float voltage;
                    uint32_t status;
                    BackupRecord_t bak;

                    GPS_Snapshot(&gps);
                    BuildDateTimeAutoFallback(dtBuf, sizeof(dtBuf));
                    voltage = NASA_GetVoltage();
                    status = BuildDeviceStatus(&gps, voltage);
                    HAL_NET_GetCsq(&csq);
                    FillBackupFromGps(&bak, &gps, dtBuf, csq, status, voltage);
                    Backup_OnTick(&bak, GetTickNow());
                }
            }
        }
        s_nasaState = STATE_INIT;
        break;
    }
}
// Xử lý set ACC
void NASA_SetAcc(int accVal)
{
    s_accRaw = accVal ? 1 : 0;
}
// Kiểm tra phiên làm việc
int NASA_IsSessionActive(void)
{
    return (s_loginAcked && (s_nasaState == STATE_LOGIN || s_nasaState == STATE_TRACKING)) ? 1 : 0;
}

// Yêu cầu gửi bản tin ngay lập tức
void NASA_RequestImmediate(int msgType)
{
    s_immediateMsgType = (msgType == 7) ? 7 : 2;
    s_trackingImmediateSend = 1;
}

// Yêu cầu phát lại
void NASA_RequestReplay(const char *fromDt, const char *toDt)
{
    (void)fromDt;
    (void)toDt;
    s_requestReplayAll = 1;
}
// Xóa dữ liệu backup
void NASA_ClearBackup(void)
{
    Backup_Clear();
}

// Xóa tất cả dữ liệu
void NASA_ClearAllData(void)
{
    Backup_Clear();
    GPS_ResetOdometer();
}
// Xử lý đăng nhập
void NASA_DriverLogin(void)
{
    char dt[40] = {0};
    GpsSnapshot_t gps = {0};
    LxltEvent_t evt = {0};

    BuildDateTimeAutoFallback(dt, sizeof(dt));
    GPS_Snapshot(&gps);
    if (s_lxltState.workActive) return;

    CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 1);
    LxltRules_StartSession(&s_lxltState, dt, gps.lat, gps.lon, gps.totalKm, GetTickNow(), 0, &evt);
    NASA_WorkSendEvent(&evt);
}

// Xử lý đăng xuất
void NASA_DriverLogout(void)
{
    char dt[40] = {0};
    GpsSnapshot_t gps = {0};
    LxltEvent_t evt = {0};

    if (!s_lxltState.workActive && !CFG_IsDriverLoggedIn()) return;
    BuildDateTimeAutoFallback(dt, sizeof(dt));
    GPS_Snapshot(&gps);
    if (s_lxltState.workActive) {
        LxltRules_EndSession(&s_lxltState, dt, gps.lat, gps.lon, GetTickNow(), (uint32_t)HAL_TICKS_PER_SEC, &evt);
        CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 0);
        NASA_WorkSendEvent(&evt);
    } else {
        CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 0);
    }
}

uint32_t NASA_GetNextMessageId(void)
{
    return s_messageIdNum++;
}
