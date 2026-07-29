#include "simcom_api.h"
#include "simcom_common.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include "app_cfg.h"
#include "app_cmd.h"
#include "app_network.h"
#include "app_backup.h"
#include "drv_en25qh64a.h"
#include "app_nasa.h"
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

static int         s_isParkStopped = 0;
static uint32_t    s_parkStartTick = 0;
static char        s_parkStartDateTime[40] = {0};
static double      s_parkLat = 0.0;
static double      s_parkLon = 0.0;
static uint32_t    s_parkLastReportTick = 0;
static int         s_parkDailyCount = 0;

/* Bản tin 5 — phiên làm việc lái xe */
static int         s_workActive = 0;
static char        s_workLoginDT[40] = {0};
static double      s_workLoginLat = 0.0;
static double      s_workLoginLon = 0.0;
static uint32_t    s_workLoginTick = 0;
static uint32_t    s_workLastReportTick = 0;
static int         s_workStopPending = 0;
static uint32_t    s_workStopStartTick = 0;
static int         s_workWasMoving = 0;
static int         s_driveMinToday = 0;       /* LXTN phút */
static uint32_t    s_driveAccumSecToday = 0;
static int         s_lxltOver4hCount = 0;
static int         s_lxltCounted4h = 0;
static double      s_workOdomStartKm = 0.0;
static uint32_t    s_workLastAccumTick = 0;

static uint32_t    s_lastTrackingTick = 0;
static int         s_prevAccOn = -1;
static volatile int s_trackingImmediateSend = 0;
static volatile int s_immediateMsgType = 2; /* 2 or 7 */
static volatile int s_requestReplayAll = 0;
static double      s_prevSentHeadingDeg = -1.0;

static int NetworkSendFrame(const char *frame);

float NASA_GetVoltage(void)
{
    unsigned int adc_mv = sAPI_ReadAdc(1);
    if (adc_mv == 0) return 0.0f;
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
    if (s_workActive && s_lxltCounted4h) st |= ST_LXLT_VIOLATION;
    if (voltage >= NASA_BATTERY_LOW_VOLT) st |= ST_BATTERY_OK;
    return st;
}

/* Checksum theo firmware cũ: payload*checksum\\r (login dùng \\r\\n) */
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
                              const GpsSnapshot_t *gps, UINT8 csq,
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

static void BuildParkingFrame(char *out, size_t size, const char *msgId, const char *dateTime,
                              int msgType, const char *endDateTime,
                              double lat, double lon, uint32_t stopSec, int dailyCount)
{
    static char payload[360];
    const char *endStr = (endDateTime != NULL) ? endDateTime : "";
    (void)snprintf(payload, sizeof(payload),
                   "!NASA,6,%s,%s,%s,%s,%s,%d,%s,%.6f,%.6f,%lu,%d,",
                   msgId, dateTime,
                   CFG_GetDriverName(), CFG_GetDriverLicense(),
                   s_parkStartDateTime, msgType, endStr,
                   lat, lon, (unsigned long)stopSec, dailyCount);
    AppendChecksumAndEnd(out, size, payload, 0);
}

static int WorkLxltMinutes(void)
{
    uint32_t now;
    if (!s_workActive || s_workLoginTick == 0) return 0;
    now = GetTickNow();
    if (now < s_workLoginTick) return 0;
    return (int)((now - s_workLoginTick) / ((uint32_t)SC_TICKS_PER_SECOND * 60u));
}

static int WorkLxltDistMeters(void)
{
    GpsSnapshot_t gps = {0};
    double deltaKm;
    GPS_Snapshot(&gps);
    deltaKm = gps.totalKm - s_workOdomStartKm;
    if (deltaKm < 0.0) deltaKm = 0.0;
    return (int)(deltaKm * 1000.0 + 0.5);
}

static void BuildWorkFrame(char *out, size_t size, const char *msgId, const char *dateTime,
                           int msgType, const char *logoutDT,
                           double logoutLat, double logoutLon)
{
    static char payload[420];
    int lxltMin = WorkLxltMinutes();
    int distM = WorkLxltDistMeters();

    payload[0] = '\0';
    if (msgType == 1) {
        (void)snprintf(payload, sizeof(payload),
                       "!NASA,5,%s,%s,%d,%s,%s,%s,%.6f,%.6f,%d,%d,,,%d,%d,",
                       msgId, dateTime, msgType,
                       CFG_GetDriverName(), CFG_GetDriverLicense(),
                       s_workLoginDT, s_workLoginLat, s_workLoginLon,
                       lxltMin, s_driveMinToday, s_lxltOver4hCount, distM);
    } else {
        const char *outDt = (logoutDT != NULL) ? logoutDT : dateTime;
        (void)snprintf(payload, sizeof(payload),
                       "!NASA,5,%s,%s,%d,%s,%s,%s,%.6f,%.6f,%d,%d,%s,%.6f,%.6f,%d,%d,",
                       msgId, dateTime, msgType,
                       CFG_GetDriverName(), CFG_GetDriverLicense(),
                       s_workLoginDT, s_workLoginLat, s_workLoginLon,
                       lxltMin, s_driveMinToday,
                       outDt, logoutLat, logoutLon,
                       s_lxltOver4hCount, distM);
    }
    AppendChecksumAndEnd(out, size, payload, 0);
}

static void NASA_WorkSend(int msgType, const char *dateTime, double lat, double lon)
{
    static char frame[480];
    char msgIdStr[16] = {0};

    if (!Network_IsConnected() || !s_loginAcked) return;

    frame[0] = '\0';
    (void)snprintf(msgIdStr, sizeof(msgIdStr), "%lu", (unsigned long)s_messageIdNum);
    BuildWorkFrame(frame, sizeof(frame), msgIdStr, dateTime, msgType, dateTime, lat, lon);
    sAPI_Debug("[NASA/Work] type=%d %s", msgType, frame);
    if (NetworkSendFrame(frame) == 0) s_messageIdNum++;
}

static void WorkAccumDriveTime(uint32_t nowTick)
{
    uint32_t elapsedSec;
    if (!s_workActive) {
        s_workLastAccumTick = nowTick;
        return;
    }
    if (s_workLastAccumTick == 0) {
        s_workLastAccumTick = nowTick;
        return;
    }
    if (nowTick <= s_workLastAccumTick) return;
    elapsedSec = (nowTick - s_workLastAccumTick) / (uint32_t)SC_TICKS_PER_SECOND;
    if (elapsedSec == 0) return;
    s_workLastAccumTick = nowTick;
    s_driveAccumSecToday += elapsedSec;
    s_driveMinToday = (int)(s_driveAccumSecToday / 60u);
}

static void WorkStartSession(const char *dateTime, double lat, double lon, int continuous)
{
    GpsSnapshot_t gps = {0};
    uint32_t now = GetTickNow();

    GPS_Snapshot(&gps);
    if (!continuous) {
        strncpy(s_workLoginDT, dateTime, sizeof(s_workLoginDT) - 1);
        s_workLoginDT[sizeof(s_workLoginDT) - 1] = '\0';
        s_workLoginLat = lat;
        s_workLoginLon = lon;
        s_workLoginTick = now;
        s_workOdomStartKm = gps.totalKm;
        s_lxltCounted4h = 0;
    }
    s_workActive = 1;
    s_workStopPending = 0;
    s_workLastReportTick = now;
    s_workLastAccumTick = now;
    CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 1);
    NASA_WorkSend(1, dateTime, lat, lon);
}

static void WorkEndSession(const char *dateTime, double lat, double lon)
{
    if (!s_workActive) return;
    WorkAccumDriveTime(GetTickNow());
    NASA_WorkSend(3, dateTime, lat, lon);
    s_workActive = 0;
    s_workStopPending = 0;
    s_workLoginTick = 0;
    s_lxltCounted4h = 0;
    CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 0);
}

static void NASA_WorkUpdate(const char *dateTime, double lat, double lon, double speed)
{
    (void)speed;
    uint32_t nowTick = GetTickNow();
    int moving = GPS_IsMoving();
    int lxltMin;

    WorkAccumDriveTime(nowTick);

    if (s_workActive) {
        lxltMin = WorkLxltMinutes();
        if (lxltMin >= WORK_LXLT_LIMIT_MIN && !s_lxltCounted4h) {
            s_lxltOver4hCount++;
            s_lxltCounted4h = 1;
        }

        if (!moving) {
            if (!s_workStopPending) {
                s_workStopPending = 1;
                s_workStopStartTick = nowTick;
            } else if ((nowTick - s_workStopStartTick) >=
                       (uint32_t)WORK_LOGOUT_STOP_SEC * (uint32_t)SC_TICKS_PER_SECOND) {
                WorkEndSession(dateTime, lat, lon);
            }
        } else {
            /* Đang di chuyển: reset cờ s_workStopPending */
            s_workStopPending = 0;
        }

        /* Gửi bản tin 5-2 định kỳ mỗi WORK_PERIODIC_SEC (60s) khi phiên s_workActive còn mở (kể cả khi đỗ) */
        if (s_workActive && (nowTick - s_workLastReportTick) >=
            (uint32_t)WORK_PERIODIC_SEC * (uint32_t)SC_TICKS_PER_SECOND) {
            s_workLastReportTick = nowTick;
            NASA_WorkSend(2, dateTime, lat, lon);
        }
    } else {
        /* Cạnh dừng→chạy: bắt đầu chuyến mới (5-1) với tên mặc định/CFG */
        if (moving && !s_workWasMoving)
            WorkStartSession(dateTime, lat, lon, 0);
    }
    s_workWasMoving = moving;
}

static void DetectResetReason(void)
{
    POWER_UP_REASON r = sAPI_GetPowerUpEvent();
    if (r == POWER_UP_SOFTWARE_RESET) {
        strncpy(s_resetReason, "R-3.0", sizeof(s_resetReason) - 1);
    } else if (r == POWER_UP_RESET_KEY || r == POWER_UP_POWER_KEY) {
        strncpy(s_resetReason, "R-2.0", sizeof(s_resetReason) - 1);
    } else {
        strncpy(s_resetReason, "R-0.0", sizeof(s_resetReason) - 1);
    }
}

static void MakeLoginCode(void)
{
    uint32_t t = GetTickNow();
    (void)snprintf(s_loginCode, sizeof(s_loginCode), "%06lu", (unsigned long)(t % 1000000u));
}

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
        s_parkDailyCount = 0;
        s_driveMinToday = 0;
        s_driveAccumSecToday = 0;
        s_lxltOver4hCount = 0;
        s_workOdomStartKm = 0.0;
        s_messageIdNum = 1;
        cfg->operateDays++;
        cfg->lastMday = today_mday;
        CFG_Save();
    }
}

static int NetworkSendFrame(const char *frame)
{
    int ret = Network_Send(frame, (uint32_t)strlen(frame));
    return (ret > 0) ? 0 : -1;
}

static void NASA_ParkSendFrame6(int msgType, const char *dateTime)
{
    static char frame[400];
    char msgIdStr[16] = {0};
    uint32_t stopSec = 0;

    if (!Network_IsConnected() || !s_loginAcked) return;

    if (s_parkStartTick > 0) {
        stopSec = (GetTickNow() - s_parkStartTick) / (uint32_t)SC_TICKS_PER_SECOND;
    }
    (void)snprintf(msgIdStr, sizeof(msgIdStr), "%lu", (unsigned long)s_messageIdNum);
    BuildParkingFrame(frame, sizeof(frame), msgIdStr, dateTime, msgType,
                      (msgType == 1) ? "" : dateTime,
                      s_parkLat, s_parkLon, stopSec, s_parkDailyCount);
    sAPI_Debug("[NASA/Park] type=%d %s", msgType, frame);
    if (NetworkSendFrame(frame) == 0) s_messageIdNum++;
}

static uint32_t s_park61Tick = 0;
static int      s_pendingSend63 = 0;

static void NASA_ParkUpdate(const char *dateTime, double lat, double lon, double speed)
{
    (void)speed;
    uint32_t nowTick = GetTickNow();
    int isParked = GPS_IsParked();
    int isMoving = GPS_IsMoving();
    int parkPeriodSec = CFG_GetParkConfirmSec(); /* Config cmd 26: chu kỳ báo 6-2 (mặc định 60s) */
    if (parkPeriodSec <= 0) parkPeriodSec = PARK_REPORT_PERIOD_SEC_DEFAULT;

    /* Xử lý xả bản tin 6-3 hoãn (pending 5s guard) nếu đã đủ thời gian */
    if (s_pendingSend63 && (nowTick - s_park61Tick) >= (uint32_t)(PARK_FRAME_GUARD_SEC * SC_TICKS_PER_SECOND)) {
        NASA_ParkSendFrame6(3, dateTime);
        s_pendingSend63 = 0;
    }

    if (!s_isParkStopped) {
        if (isParked) {
            /* Nếu có 6-3 cũ đang chờ -> Xả 6-3 cũ trước (Bảo đảm thứ tự FIFO) */
            if (s_pendingSend63) {
                NASA_ParkSendFrame6(3, dateTime);
                s_pendingSend63 = 0;
            }

            s_isParkStopped = 1;
            s_parkStartTick = nowTick;
            s_parkLastReportTick = nowTick;
            s_parkDailyCount++;
            s_parkLat = lat;
            s_parkLon = lon;
            snprintf(s_parkStartDateTime, sizeof(s_parkStartDateTime), "%s", dateTime);
            s_park61Tick = nowTick;
            NASA_ParkSendFrame6(1, dateTime);
        }
    } else {
        if (!isParked && isMoving) {
            s_isParkStopped = 0;
            s_parkStartTick = 0;
            s_trackingImmediateSend = 1;
            s_immediateMsgType = 2;

            /* Guard 5s giữa 6-1 và 6-3 */
            if ((nowTick - s_park61Tick) >= (uint32_t)(PARK_FRAME_GUARD_SEC * SC_TICKS_PER_SECOND)) {
                NASA_ParkSendFrame6(3, dateTime);
                s_pendingSend63 = 0;
            } else {
                s_pendingSend63 = 1; /* Hoãn gửi 6-3 cho tới khi đủ 5s */
            }
        } else if (isParked) {
            if ((nowTick - s_parkLastReportTick) >= (uint32_t)(parkPeriodSec * SC_TICKS_PER_SECOND)) {
                s_parkLastReportTick = nowTick;
                NASA_ParkSendFrame6(2, dateTime);
            }
        }
    }
}

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

static void FillBackupFromGps(BackupRecord_t *rec, const GpsSnapshot_t *gps,
                              const char *dateTime, UINT8 csq, uint32_t status, float voltage)
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

static int SendTrackOrBackup(int msgCode, const char *dateTime, UINT8 csq)
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

    sAPI_Debug("[NASA] msg=%d %s", msgCode, frame);
    if (NetworkSendFrame(frame) < 0) {
        Backup_Push(&bak);
        return -1;
    }
    s_prevSentHeadingDeg = gps.headingDeg;
    s_messageIdNum++;
    return 0;
}

static int ReplayOneBackup(void)
{
    BackupRecord_t rec;
    char frame[360] = {0};
    char payload[320] = {0};
    char msgId[16] = {0};
    GpsSnapshot_t gps;

    /* Peek trước — chỉ CommitPop sau khi gửi TCP thành công.
     * Tránh mất bản ghi / đảo thứ tự khi Pop rồi Push lại cuối hàng đợi. */
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

    sAPI_Debug("[NASA/Replay] %s", frame);
    if (NetworkSendFrame(frame) < 0) {
        return -1; /* giữ nguyên hàng đợi, thử lại sau */
    }
    Backup_CommitPop();
    s_messageIdNum++;
    return 1;
}

static void ReplayBurst(void)
{
    int i;
    for (i = 0; i < NASA_BACKUP_REPLAY_BURST; i++) {
        int r = ReplayOneBackup();
        if (r <= 0) break;
    }
    Backup_Flush();
}

void NASA_OnServerProtocolLine(const char *line)
{
    /* !NASA,1,<serverTime>,<lastPacket>,checksum */
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
        sAPI_Debug("[NASA] Login ACK OK lastPacket=%s", s_serverLastPacket);
    } else {
        /* Bản tin !NASA khác type 1 — hiện chưa xử lý cấu hình ở đây */
        sAPI_Debug("[NASA] Unhandled protocol line: %s", line);
    }
}

static NasaState_t Handle_StateInit(INT32 pdp_id)
{
    if (!CFG_IsDeviceEnabled()) {
        sAPI_Debug("[NASA] Device disabled (cmd38), sleeping...");
        sAPI_TaskSleep(10 * SC_TICKS_PER_SECOND);
        return STATE_INIT;
    }
    if (Network_ActivatePdp(pdp_id) == 0) {
        TriggerNtpSyncIfNeeded();
        return STATE_CONNECT;
    }
    return STATE_ERROR_RETRY;
}

static NasaState_t Handle_StateConnect(void)
{
    if (Network_Connect(CFG_GetServerHost(), CFG_GetServerPort()) == 0)
        return STATE_LOGIN;
    return STATE_ERROR_RETRY;
}

static NasaState_t Handle_StateLogin(void)
{
    char dateTime[40] = {0};
    char msgId[16] = {0};
    char frame[360] = {0};
    uint32_t startTick;
    uint32_t timeoutTicks = NASA_LOGIN_ACK_TIMEOUT_SEC * SC_TICKS_PER_SECOND;

    s_loginAcked = 0;
    MakeLoginCode();
    BuildDateTimeAutoFallback(dateTime, sizeof(dateTime));
    (void)snprintf(msgId, sizeof(msgId), "%lu", (unsigned long)s_messageIdNum);
    BuildLoginFrame(frame, sizeof(frame), msgId, dateTime);

    sAPI_Debug("[NASA/Login] %s", frame);
    sAPI_Debug("[NASA/Login] wait ACK...");
    if (NetworkSendFrame(frame) < 0) return STATE_ERROR_RETRY;
    s_messageIdNum++;

    startTick = GetTickNow();
    while (!s_loginAcked) {
        NASA_FeedWatchdog();
        if (Network_RecvPoll() != 0) return STATE_ERROR_RETRY;
        if ((GetTickNow() - startTick) >= timeoutTicks) {
            sAPI_Debug("[NASA/Login] ACK timeout");
            return STATE_ERROR_RETRY;
        }
        sAPI_TaskSleep(SC_TICKS_PER_SECOND / 5);
    }

    sAPI_Debug("[NASA/Login] ACK ok -> TRACKING");
    s_lastTrackingTick = 0;
    s_trackingImmediateSend = 1;
    s_immediateMsgType = 2;
    s_prevSentHeadingDeg = -1.0;
    s_requestReplayAll = 1;
    return STATE_TRACKING;
}

static NasaState_t Handle_StateTracking(void)
{
    t_rtc rtc;
    char dtBuf[40];

    sAPI_TaskSleep(TASK_BASE_SLEEP_TICKS);

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

    sAPI_GetRealTimeClock(&rtc);
    if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100) {
        ResetDailyCountersIfNeeded(rtc.tm_mday);

        /* Lịch reset cmd 32 */
        if (CFG_Get()->resetEveryDays > 0 &&
            rtc.tm_hour == CFG_Get()->resetAtHour &&
            rtc.tm_min == 0) {
            static int lastResetDay = -1;
            if (lastResetDay != rtc.tm_mday) {
                lastResetDay = rtc.tm_mday;
                sAPI_SysReset();
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

                periodTicks = (uint32_t)periodSec * (uint32_t)SC_TICKS_PER_SECOND;

                if (s_trackingImmediateSend || (nowTick - s_lastTrackingTick) >= periodTicks) {
                    UINT8 csq = 0;
                    int msg = s_immediateMsgType;
                    sAPI_NetworkGetCsq(&csq);
                    s_trackingImmediateSend = 0;
                    s_immediateMsgType = 2;
                    if (SendTrackOrBackup(msg, dtBuf, csq) < 0)
                        return STATE_ERROR_RETRY;
                    s_lastTrackingTick = nowTick;
                } else {
                    /* Chỉ ghi backup khi thực sự mất mạng/chưa nhận ACK login */
                    if (!Network_IsConnected() || !s_loginAcked) {
                        UINT8 csq = 0;
                        float voltage = NASA_GetVoltage();
                        uint32_t status = BuildDeviceStatus(&gpsTemp, voltage);
                        BackupRecord_t bak;
                        sAPI_NetworkGetCsq(&csq);
                        FillBackupFromGps(&bak, &gpsTemp, dtBuf, csq, status, voltage);
                        Backup_OnTick(&bak, nowTick);
                    }
                }
            }
        }
    }

    return STATE_TRACKING;
}

void NASA_Init(void)
{
    s_nasaState = STATE_INIT;
    s_messageIdNum = 1;
    s_loginAcked = 0;
    s_workActive = 0;
    s_workStopPending = 0;
    s_workWasMoving = 0;
    DetectResetReason();
    Backup_Init();

    /* Phiên làm việc không sống qua reset — đồng bộ bit ST_DRIVER */
    if (CFG_IsDriverLoggedIn()) {
        CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 0);
        CFG_Save();
    }

    if (sAPI_SysGetImei(s_devImei) != 0)
        strncpy(s_devImei, "UNKNOWN_IMEI", sizeof(s_devImei) - 1);
    if (sAPI_SysGetIccid(s_devIccid) != SC_SIM_RETURN_SUCCESS)
        strncpy(s_devIccid, "UNKNOWN_SIM", sizeof(s_devIccid) - 1);

    sAPI_Debug("[NASA] Init IMEI=%s ICCID=%s reset=%s", s_devImei, s_devIccid, s_resetReason);
    s_disconnStartTick = 0;
    NASA_FeedWatchdog();
}

void NASA_FeedWatchdog(void)
{
    s_lastWatchdogFeedTick = GetTickNow();
}

int NASA_IsWatchdogTimeout(void)
{
    uint32_t now = GetTickNow();
    if (s_lastWatchdogFeedTick == 0) return 0;
    if (now >= s_lastWatchdogFeedTick) {
        return ((now - s_lastWatchdogFeedTick) >= (uint32_t)(180 * SC_TICKS_PER_SECOND)) ? 1 : 0;
    }
    return 0;
}

#define NET_DISCONN_RESET_SEC (15 * 60) /* 15 phút = 900s */

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
        (now - s_disconnStartTick) >= (uint32_t)(NET_DISCONN_RESET_SEC * SC_TICKS_PER_SECOND)) {
        sAPI_Debug("[NASA] Mat ket noi >15 phut (%ds) -> Tu dong SysReset modem...", NET_DISCONN_RESET_SEC);
        sAPI_TaskSleep(200);
        sAPI_SysReset();
    }
}

void NASA_RunStep(void)
{
    static uint32_t s_lastCfgFlushTick = 0;
    uint32_t nowFlush = GetTickNow();

    NASA_FeedWatchdog();
    CheckNetworkDisconnTimeout();
    /* Deferred CFG save (~10s): tránh nest Save trong CMD 14/25 + Work frame */
    if (s_lastCfgFlushTick == 0 ||
        (nowFlush - s_lastCfgFlushTick) >= (uint32_t)(10 * SC_TICKS_PER_SECOND)) {
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
        sAPI_Debug("[NASA/Retry] wait %ds", ERROR_RETRY_DELAY_SEC);
        Network_Disconnect();
        s_loginAcked = 0;
        /* Ngủ từng 1s và vẫn ghi backup định kỳ — không mất dữ liệu hành
         * trình khi xe chạy trong vùng mất sóng dài */
        {
            int sec;
            for (sec = 0; sec < ERROR_RETRY_DELAY_SEC; sec++) {
                NASA_FeedWatchdog();
                CheckNetworkDisconnTimeout();
                sAPI_TaskSleep(SC_TICKS_PER_SECOND);
                {
                    char dtBuf[40] = {0};
                    GpsSnapshot_t gps = {0};
                    UINT8 csq = 0;
                    float voltage;
                    uint32_t status;
                    BackupRecord_t bak;

                    GPS_Snapshot(&gps);
                    BuildDateTimeAutoFallback(dtBuf, sizeof(dtBuf));
                    voltage = NASA_GetVoltage();
                    status = BuildDeviceStatus(&gps, voltage);
                    sAPI_NetworkGetCsq(&csq);
                    FillBackupFromGps(&bak, &gps, dtBuf, csq, status, voltage);
                    Backup_OnTick(&bak, GetTickNow());
                }
            }
        }
        s_nasaState = STATE_INIT;
        break;
    }
}

void NASA_SetAcc(int accVal)
{
    s_accRaw = accVal ? 1 : 0;
}

int NASA_IsSessionActive(void)
{
    return (s_loginAcked && (s_nasaState == STATE_LOGIN || s_nasaState == STATE_TRACKING)) ? 1 : 0;
}

void NASA_RequestImmediate(int msgType)
{
    s_immediateMsgType = (msgType == 7) ? 7 : 2;
    s_trackingImmediateSend = 1;
}

void NASA_RequestReplay(const char *fromDt, const char *toDt)
{
    (void)fromDt;
    (void)toDt;
    /* MVP: phát lại toàn bộ queue; lọc theo thời gian khi có parser đầy đủ */
    s_requestReplayAll = 1;
}

void NASA_ClearBackup(void)
{
    Backup_Clear();
}

void NASA_ClearAllData(void)
{
    Backup_Clear();
    GPS_ResetOdometer();
}

void NASA_DriverLogin(void)
{
    char dt[40] = {0};
    GpsSnapshot_t gps = {0};

    BuildDateTimeAutoFallback(dt, sizeof(dt));
    GPS_Snapshot(&gps);
    if (s_workActive) {
        /* Đã login — không gửi lại 5-1 (dùng cho na,14 khi đã ON) */
        return;
    }
    WorkStartSession(dt, gps.lat, gps.lon, 0);
}

void NASA_DriverLogout(void)
{
    char dt[40] = {0};
    GpsSnapshot_t gps = {0};

    if (!s_workActive && !CFG_IsDriverLoggedIn()) return;
    BuildDateTimeAutoFallback(dt, sizeof(dt));
    GPS_Snapshot(&gps);
    if (s_workActive)
        WorkEndSession(dt, gps.lat, gps.lon);
    else {
        CFG_SetDriver(CFG_GetDriverName(), CFG_GetDriverLicense(), 0);
        /* driverLoggedIn dirty — CFG_FlushDirty trong NASA_RunStep */
    }
}

uint32_t NASA_GetNextMessageId(void)
{
    return s_messageIdNum++;
}
