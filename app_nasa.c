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
#include "hal/hal_sms.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include "app_cfg.h"
#include "app_cmd.h"
#include "app_network.h"
#include "app_backup.h"
#include "drv_en25qh64a.h"
#include "drv_lis3dsh.h"
#include "app_nasa.h"
#include "app_park_rules.h"
#include "app_lxlt_rules.h"
#include "app_fota.h"
#include "app_indication.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef enum {
    STATE_INIT = 1,
    STATE_CONNECT = 2,
    STATE_LOGIN = 3,
    STATE_TRACKING = 4,
    STATE_ERROR_RETRY = 5
} NasaState_t;

static NasaState_t s_nasaState = STATE_INIT;
static uint32_t s_messageIdNum = 1;
static int s_accRaw = -1;         /* 1 = ACC ON, 0 = OFF, -1 = chưa đọc được chân (đã quy đổi active-low ở GPIO task) */
static int s_loginAcked = 0;
static char s_resetReason[16] = "R-0.0";
static char s_loginCode[16] = "0";
static char s_devImei[32] = {0};
static char s_devIccid[32] = {0};
static char s_serverLastPacket[40] = {0};
static uint32_t s_lastWatchdogFeedTick = 0;
static uint32_t s_disconnStartTick = 0;

/* ICCID mới do task SIM báo; task NASA áp dụng ở đầu NASA_RunStep */
static char         s_pendingIccid[32] = {0};
static volatile int s_simChanged = 0;

/* Bộ Quy tắc đỗ xe & LXLT */
static ParkState_t s_parkState;
static LxltState_t s_lxltState;

static uint32_t    s_lastTrackingTick = 0;
static int         s_prevAccOn = -1;
static volatile int s_trackingImmediateSend = 0;
static volatile int s_immediateMsgType = 2; /* 2 or 7 */
static volatile int s_requestReplayAll = 0;
static double      s_prevSentHeadingDeg = -1.0;

/* Ngủ sâu tiết kiệm ắc quy (SleepPoll / SleepDeepStep). PWR_SLEEP: Server ngắt,
 * sóng tắt, GNSS tắt, các task dừng; nasa_reporter chỉ còn đọc ACC + cảm biến
 * gia tốc mỗi SLEEP_POLL_SEC giây. */
typedef enum { PWR_AWAKE = 0, PWR_SLEEP } PwrState_t;

static volatile PwrState_t s_pwrState = PWR_AWAKE;
static volatile int s_wakeReq = 0;            /* đặt từ task khác / ngắt GPIO */
static const char *volatile s_wakeReqReason = "";
static int      s_accWireSeenOn = 0;          /* dây ACC đã từng báo ON từ lúc khởi động */
static uint32_t s_idleStartTick = 0;          /* ACC tắt + đứng yên từ lúc này, 0 = chưa */
static uint32_t s_holdStartTick = 0;          /* thức vì sự kiện từ lúc này, 0 = không giữ */
static uint32_t s_sleepStartTick = 0;
static int      s_battLow = 0;
static int      s_battLowCnt = 0;
static uint32_t s_battCheckTick = 0;
/* Rung / chuyển động lúc ngủ (LIS3DSH SM1 -> INT1) */
static int      s_motionWakeOk = 0;           /* cảm biến + chân INT1 dùng được */
static volatile int s_motionIrq = 0;          /* đặt từ ngắt INT1 */
static volatile int s_accIrq = 0;             /* đặt từ ngắt chân ACC, SleepPoll kiểm lại mức chân */
static uint32_t s_sleepStatusTick = 0;        /* lần log trạng thái ngủ trước */
static int      s_motionCount = 0;            /* số lần ngắt trong cửa sổ hiện tại */
static uint32_t s_motionWinTick = 0;          /* đầu cửa sổ đếm, 0 = chưa có ngắt */
static int      s_motionThrMg = 0;            /* ngưỡng lần nạp gần nhất (mg), 0 = chưa nạp */
static int      s_motionArmed = 0;            /* SleepEnter nạp cảm biến rung thành công (MotionPoll chỉ tin INT1 khi này) */
/* Nghiêng lúc ngủ (TiltPoll): véc-tơ trọng lực lúc vào ngủ */
static int32_t  s_tiltRef[3];
static int      s_tiltRefOk = 0;
static int      s_tiltLastDeg10 = -1;         /* góc lệch lần đọc gần nhất x10, -1 = chưa đọc */
/* Xe di chuyển khi tắt ACC (NoAccMovePoll) */
static int      s_naActive = 0;
static uint32_t s_naSpeedTick = 0;            /* vận tốc > ngưỡng liên tục từ lúc này, 0 = không */
static uint32_t s_naStopTick = 0;             /* đang cảnh báo: đứng yên từ lúc này, 0 = không */
static int      s_naRefOk = 0;                /* có chỗ đỗ để đo quãng dời */
static int      s_naRefFresh = 0;             /* chỗ đỗ lấy từ fix GNSS thật (không phải vị trí cũ) */
static double   s_naRefLat = 0.0, s_naRefLon = 0.0;
static uint32_t s_naSmsTick = 0;              /* lần SMS cảnh báo trước, 0 = chưa */
/* Thử độ nhạy lúc thức (lệnh na,43,t): nạp SM1, đếm ngắt và log X/Y/Z */
static volatile int s_motionTestReqSec = 0;   /* đặt từ task lệnh, nasa_reporter thực hiện */
static int      s_motionTestOn = 0;
static uint32_t s_motionTestEndTick = 0;
static int      s_motionTestSec = 0;
static int      s_motionTestCount = 0;        /* số ngắt của lần thử đang chạy / gần nhất */
/* Vào cua theo gia tốc ngang (LIS3DSH, TurnPoll): 0 = không, 1 = cua, 2 = cua gấp */
static int      s_turnLevel = 0;
static int      s_turnPending = 0;            /* số giây liên tiếp mức mới khác mức hiện tại */
static int      s_turnLatMg = 0;              /* gia tốc ngang ước lượng lần đo gần nhất */
static float    s_gravEst[3];                 /* véc-tơ trọng lực ước lượng (mg) */
static int      s_gravOk = 0;
static double   s_turnPrevSpeed = -1.0;       /* vận tốc GPS lần fix trước, để tính gia tốc dọc */
static uint32_t s_turnPrevFixTick = 0;
static int      s_turnLongMg = 0;

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
    int byWire = (s_accRaw > 0) ? 1 : 0; /* s_accRaw đã là trạng thái logic ON/OFF */

    if (mode == 0) return bySpeed;
    if (s_accRaw < 0) return bySpeed; /* chưa đọc được chân ACC: theo vận tốc */
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

/**
 * Gửi bản tin sự kiện (5 / 6). Chưa có phiên với Server hoặc gửi lỗi thì bỏ:
 * bản tin 5 / 6 hiện không lưu vào bộ đệm điểm mù (chỉ lưu vị trí).
 * Muốn lưu lại thì gọi Backup_PushText(frame) ở nhánh bỏ.
 */
static void SendOrStoreEventFrame(const char *frame)
{
    if (Network_IsConnected() && s_loginAcked && NetworkSendFrame(frame) == 0) {
        s_messageIdNum++;
        return;
    }
    HAL_LOG("[NASA] offline -> event frame dropped (not backed up)");
}

static void NASA_WorkSendEvent(const LxltEvent_t *evt)
{
    static char frame[480];
    char msgIdStr[16] = {0};

    if (!evt || evt->type == LXLT_EVENT_NONE) return;

    frame[0] = '\0';
    (void)snprintf(msgIdStr, sizeof(msgIdStr), "%lu", (unsigned long)s_messageIdNum);
    BuildWorkFramePayload(frame, sizeof(frame), msgIdStr, evt->dateTime, (int)evt->type,
                          CFG_GetDriverName(), CFG_GetDriverLicense(),
                          &s_lxltState, GetTickNow(), (uint32_t)HAL_TICKS_PER_SEC,
                          GPS_GetTotalKm(),
                          evt->endDateTime[0] ? evt->endDateTime : evt->dateTime,
                          evt->lat, evt->lon);
    HAL_LOG("[NASA/Work] type=%d %s", (int)evt->type, frame);
    SendOrStoreEventFrame(frame);
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

    (void)snprintf(msgIdStr, sizeof(msgIdStr), "%lu", (unsigned long)s_messageIdNum);
    BuildParkingFramePayload(frame, sizeof(frame), msgIdStr, evt->dateTime, (int)evt->type,
                              (evt->type == PARK_EVENT_SEND_TYPE1) ? "" : evt->dateTime,
                              CFG_GetDriverName(), CFG_GetDriverLicense(),
                              s_parkState.parkStartDateTime, evt->lat, evt->lon,
                              evt->stopSec, evt->dailyCount);
    HAL_LOG("[NASA/Park] type=%d %s", (int)evt->type, frame);
    SendOrStoreEventFrame(frame);
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
    /* Ngủ sâu không chạy quy tắc đỗ; giữ cờ để không gửi 6-2 lúc đang vào ngủ */
    s_parkState.holdPeriodic = NASA_IsSleeping();
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
/** Chu kỳ gửi khi chạy theo mức cua (0 = thường, 1 = cua, 2 = cua gấp) */
static int TurnPeriodSec(int level)
{
    int turnPeriod;

    if (level >= 2) return HEADING_PERIOD_SHARP_SEC;
    if (level <= 0) return CFG_GetPeriodMoving();
    turnPeriod = (int)((double)CFG_GetPeriodMoving() * HEADING_PERIOD_TURN_FACTOR);
    if (turnPeriod < TRACKING_PERIOD_MIN_SEC) turnPeriod = TRACKING_PERIOD_MIN_SEC;
    return turnPeriod;
}

static int GetAdaptivePeriodSec(double speedKph, double headingDeg)
{
    if (speedKph < HEADING_MIN_SPEED_KPH) return CFG_GetPeriodMoving();
    /* Gia tốc ngang xác nhận đang vào cua: rút ngắn ngay, không chờ hướng GPS lệch đủ góc */
    if (s_turnLevel >= 2) return TurnPeriodSec(2);

    if (s_prevSentHeadingDeg >= 0.0) {
        double delta = headingDeg - s_prevSentHeadingDeg;
        if (delta > 180.0) delta -= 360.0;
        if (delta < -180.0) delta += 360.0;
        if (delta < 0.0) delta = -delta;

        if (delta >= HEADING_THRESHOLD_SHARP_DEG) return TurnPeriodSec(2);
        if (delta >= HEADING_THRESHOLD_TURN_DEG) return TurnPeriodSec(1);
    }
    return TurnPeriodSec(s_turnLevel);
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

/**
 * Bản ghi điểm mù chỉ có giá trị khi đặt được lên dòng thời gian và lên bản đồ:
 * cần thời gian hợp lệ và đã từng có vị trí GPS.
 */
static int BackupRecordUsable(const BackupRecord_t *rec)
{
    if (rec->datetime[0] == '\0' || strncmp(rec->datetime, "0000-00-00", 10) == 0) return 0;
    if (rec->lat == 0.0 && rec->lon == 0.0) return 0;
    return 1;
}

static void PushBackupIfUsable(const BackupRecord_t *rec)
{
    if (BackupRecordUsable(rec)) {
        Backup_Push(rec);
    } else {
        HAL_LOG("[Backup] skip record without valid time/position (dt=%s)", rec->datetime);
    }
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
        PushBackupIfUsable(&bak);
        s_lastTrackingTick = GetTickNow();
        s_prevSentHeadingDeg = gps.headingDeg;
        return -1;
    }

    HAL_LOG("[NASA] msg=%d %s", msgCode, frame);
    if (NetworkSendFrame(frame) < 0) {
        PushBackupIfUsable(&bak);
        s_lastTrackingTick = GetTickNow();
        s_prevSentHeadingDeg = gps.headingDeg;
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

    /* Bản tin sự kiện 5 / 6 lưu khi mất mạng: gửi lại nguyên khung đã đóng gói */
    if (rec.used == BACKUP_KIND_TEXT) {
        static char text[BACKUP_TEXT_MAX + 1];
        int slots = 0;
        int r = Backup_PeekText(text, sizeof(text), &slots);

        if (r < 0) return -1;
        if (r == 1) {
            HAL_LOG("[NASA/Replay] %s", text);
            if (NetworkSendFrame(text) < 0) return -1;
        } else {
            HAL_LOG("[NASA/Replay] drop broken event frame (%d slot)", slots);
        }
        Backup_CommitPopN(slots);
        return 1;
    }

    /* Bản ghi cũ không có thời gian / vị trí hợp lệ: bỏ, không gán giờ hiện tại */
    if (!BackupRecordUsable(&rec)) {
        HAL_LOG("[NASA/Replay] drop record without valid time/position (dt=%.19s)", rec.datetime);
        Backup_CommitPop();
        return 1;
    }

    memset(&gps, 0, sizeof(gps));
    gps.lat = rec.lat;
    gps.lon = rec.lon;
    gps.speedKph = rec.speedKph;
    gps.satellites = rec.sats;
    gps.totalKm = rec.totalKm;
    gps.headingDeg = rec.headingDeg;
    gps.valid = (rec.lat != 0.0 || rec.lon != 0.0);

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
/* ============================ Ngủ sâu tiết kiệm ắc quy ============================ */

static uint32_t NonZeroTick(uint32_t t)
{
    return (t != 0) ? t : 1;
}

static uint32_t SecToTicks(uint32_t sec)
{
    return sec * (uint32_t)HAL_TICKS_PER_SEC;
}

int NASA_IsSleeping(void)
{
    return (s_pwrState != PWR_AWAKE) ? 1 : 0;
}

/* Gọi được từ ngắt GPIO: chỉ ghi 2 biến */
void NASA_WakeRequest(const char *reason)
{
    s_wakeReqReason = reason ? reason : "?";
    s_wakeReq = 1;
}

/* Ngắt cạnh xuống chân ACC: chưa chắc ACC bật (nhiễu / sự kiện thức khác của
 * module), SleepPoll đọc lại mức chân rồi mới thức */
void NASA_AccIrq(void)
{
    s_accIrq = 1;
}

/** Chu kỳ báo cáo khi ACC tắt (lúc ngủ sâu không có báo cáo nào) */
static int StoppedPeriodSec(void)
{
    return CFG_GetPeriodStopped();
}

/** Ắc quy yếu: đo khi ACC tắt, mỗi 10 s, xác nhận vài lần liên tiếp, có trễ khi hết yếu */
static void SleepCheckBattery(void)
{
    uint32_t now = GetTickNow();
    float v;

    if (SLEEP_LOW_BATT_VOLT <= 0.0f) return;
    if (IsAccOn()) {
        s_battLowCnt = 0;
        return;
    }
    if (s_battCheckTick != 0 && (now - s_battCheckTick) < SecToTicks(10)) return;
    s_battCheckTick = NonZeroTick(now);

    v = NASA_GetVoltage();
    if (!s_battLow) {
        if (v < SLEEP_LOW_BATT_VOLT) {
            if (++s_battLowCnt >= SLEEP_LOW_BATT_CONFIRM) {
                s_battLow = 1;
                HAL_LOG("[SLEEP] battery low %.1fV < %.1fV -> sleep after %ds",
                        (double)v, (double)SLEEP_LOW_BATT_VOLT, SLEEP_LOW_BATT_DELAY_SEC);
            }
        } else {
            s_battLowCnt = 0;
        }
    } else if (v >= SLEEP_LOW_BATT_VOLT + SLEEP_LOW_BATT_HYST_VOLT) {
        s_battLow = 0;
        s_battLowCnt = 0;
        HAL_LOG("[SLEEP] battery ok again %.1fV", (double)v);
    }
}

/** @return NULL nếu được ngủ, ngược lại là lý do không ngủ / phải thức */
static const char *SleepBlockReason(void)
{
    int mode = CFG_GetAccMode();

    if (!CFG_GetSleepEnabled()) return "sleep disabled";
    if (!CFG_IsDeviceEnabled()) return "device disabled";
    /* Không tin được dây ACC thì không có gì đánh thức khi xe chạy */
    if (s_accRaw < 0 || mode == 0 || (mode == 2 && !s_accWireSeenOn)) return "no ACC wire";
    /* Đang ngủ thì gpio_status không lọc ACC (s_accRaw cũ): SleepPoll đọc thẳng chân */
    if (s_pwrState == PWR_AWAKE) {
        if (IsAccOn()) return "ACC on";
        if (GPS_IsMoving()) return "moving";
        if (s_naActive) return "moving without ACC";
        if (FOTA_IsBusy()) return "FOTA";
    }
    return NULL;
}

/* Ngữ cảnh ngắt: chỉ đặt cờ, SleepPoll đọc OUTS1 để xóa ngắt */
static void MotionIsr(void)
{
    s_motionIrq = 1;
}

/** Đang ngủ: xử lý ngắt chuyển động; @return 1 nếu đủ điều kiện thức */
static int MotionPoll(uint32_t now)
{
    int outs;

    if (!s_motionArmed || !CFG_GetMotionWakeEnabled() || !CFG_GetAccelEnabled()) return 0;
    /* Chân không hỗ trợ đánh thức: vẫn thấy được ngắt chốt (mức thấp) mỗi vòng */
    if (!s_motionIrq && HAL_GPIO_Read(LIS3DSH_INT1_PIN) != 0) return 0;
    s_motionIrq = 0;
    outs = LIS3DSH_MotionClear();
    if (s_motionWinTick == 0 || (now - s_motionWinTick) >= SecToTicks((uint32_t)CFG_GetMotionWindowSec())) {
        s_motionWinTick = NonZeroTick(now);
        s_motionCount = 0;
    }
    s_motionCount++;
    HAL_LOG("[SLEEP] motion #%d/%d in %ds window (OUTS=0x%02X)",
            s_motionCount, CFG_GetMotionEvents(), CFG_GetMotionWindowSec(), outs & 0xFF);
    if (s_motionCount >= CFG_GetMotionEvents()) return 1;
    /* Ngắt đánh thức đã tắt sleep hệ thống: chưa đủ thì cho ngủ lại */
    (void)HAL_OS_SystemSleepEnable(1);
    return 0;
}

/** Góc (độ) giữa 2 véc-tơ trọng lực; -1 nếu không phải trọng lực tĩnh (đang rung / lắc) */
static double TiltDeg(const int32_t a[3], const int32_t b[3])
{
    double na = sqrt((double)a[0] * a[0] + (double)a[1] * a[1] + (double)a[2] * a[2]);
    double nb = sqrt((double)b[0] * b[0] + (double)b[1] * b[1] + (double)b[2] * b[2]);
    double c;

    if (na < 700.0 || na > 1300.0 || nb < 700.0 || nb > 1300.0) return -1.0;
    c = ((double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2]) / (na * nb);
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    return acos(c) * 57.29578;
}

/** Lúc vào ngủ: lưu véc-tơ trọng lực làm mốc nghiêng */
static void TiltArm(void)
{
    s_tiltRefOk = 0;
    s_tiltLastDeg10 = -1;
    if (MOTION_TILT_DEG <= 0 || !LIS3DSH_IsReady() || !CFG_GetAccelEnabled() ||
        !CFG_GetMotionWakeEnabled()) return;
    if (LIS3DSH_ReadGravity(s_tiltRef, MOTION_TILT_SAMPLES * 2) != 0 ||
        TiltDeg(s_tiltRef, s_tiltRef) < 0.0) {
        HAL_LOG("[SLEEP] tilt reference read fail -> tilt wake off");
        return;
    }
    s_tiltRefOk = 1;
    HAL_LOG("[SLEEP] tilt armed: g=(%ld,%ld,%ld) wake if > %d deg",
            (long)s_tiltRef[0], (long)s_tiltRef[1], (long)s_tiltRef[2], MOTION_TILT_DEG);
}

/** Đang ngủ: góc nghiêng so với lúc vào ngủ; @return 1 nếu lệch quá ngưỡng 2 lần đọc cách 1 s */
static int TiltPoll(void)
{
    int32_t g[3];
    double deg, deg2;

    if (!s_tiltRefOk || !CFG_GetMotionWakeEnabled() || !CFG_GetAccelEnabled()) return 0;
    if (LIS3DSH_ReadGravity(g, MOTION_TILT_SAMPLES) != 0) return 0;
    deg = TiltDeg(s_tiltRef, g);
    if (deg < 0.0) return 0; /* đang rung: để cảm biến rung xử lý */
    s_tiltLastDeg10 = (int)(deg * 10.0 + 0.5);
    if (deg < (double)MOTION_TILT_DEG) return 0;

    HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
    if (LIS3DSH_ReadGravity(g, MOTION_TILT_SAMPLES) != 0) return 0;
    deg2 = TiltDeg(s_tiltRef, g);
    if (deg2 < (double)MOTION_TILT_DEG) {
        HAL_LOG("[SLEEP] tilt %.1f deg then %.1f deg -> transient, ignore", deg, deg2);
        return 0;
    }
    HAL_LOG("[SLEEP] tilt %.1f / %.1f deg > %d deg: g=(%ld,%ld,%ld) ref=(%ld,%ld,%ld)",
            deg, deg2, MOTION_TILT_DEG, (long)g[0], (long)g[1], (long)g[2],
            (long)s_tiltRef[0], (long)s_tiltRef[1], (long)s_tiltRef[2]);
    return 1;
}

/** Không có phiên Server lúc vào ngủ: lưu vị trí cuối vào điểm mù */
static void SleepStoreReport(void)
{
    char dtBuf[40] = {0};
    GpsSnapshot_t gps = {0};
    uint8_t csq = 0;
    float voltage = NASA_GetVoltage();
    BackupRecord_t bak;

    GPS_Snapshot(&gps);
    BuildDateTimeAutoFallback(dtBuf, sizeof(dtBuf));
    HAL_NET_GetCsq(&csq);
    FillBackupFromGps(&bak, &gps, dtBuf, csq, BuildDeviceStatus(&gps, voltage), voltage);
    HAL_LOG("[SLEEP] no server -> store last position to backup");
    PushBackupIfUsable(&bak);
}

/**
 * Vào ngủ sâu: gửi bản tin cuối, ngắt Server, tắt sóng / GNSS, nạp cảm biến rung,
 * cho module sleep. Từ đây NASA_RunStep chỉ chạy SleepDeepStep.
 */
static void SleepEnter(uint32_t idleSec)
{
    HAL_LOG("[SLEEP] enter: ACC off & still %lus, V=%.1f%s, server=%d -> last report, server off, radio off, GNSS off, tasks idle",
            (unsigned long)idleSec, (double)NASA_GetVoltage(), s_battLow ? " (low)" : "",
            NASA_IsSessionActive());

    /* Bản tin vị trí cuối trước khi ngủ */
    if (NASA_IsSessionActive()) {
        char dtBuf[40] = {0};
        uint8_t csq = 0;
        BuildDateTimeAutoFallback(dtBuf, sizeof(dtBuf));
        HAL_NET_GetCsq(&csq);
        if (SendTrackOrBackup(2, dtBuf, csq) == 0) {
            HAL_LOG("[SLEEP] last report sent");
            HAL_OS_TaskSleep(HAL_TICKS_PER_SEC); /* cho TCP gửi hết trước khi đóng */
        }
    } else {
        SleepStoreReport();
    }
    Backup_Flush();
    CFG_FlushDirty();

    Network_Disconnect();
    s_loginAcked = 0;
    s_nasaState = STATE_INIT; /* thức thì kích hoạt PDP, kết nối, đăng nhập lại */
    if (HAL_NET_SetFlightMode(1) != 0) HAL_LOG("[SLEEP] radio off (CFUN=4) fail");
    HAL_GNSS_SetPower(0);

    s_sleepStartTick = NonZeroTick(GetTickNow());
    s_sleepStatusTick = s_sleepStartTick;
    s_motionCount = 0;
    s_motionWinTick = 0;
    s_motionTestOn = 0; /* lần thử độ nhạy (nếu có) nhường cho chế độ ngủ */
    s_motionArmed = 0;
    if (s_motionWakeOk && CFG_GetMotionWakeEnabled() && CFG_GetAccelEnabled()) {
        if (LIS3DSH_MotionArm(CFG_GetMotionDeltaMg(), &s_motionThrMg) == 0) {
            s_motionArmed = 1;
        } else {
            HAL_LOG("[SLEEP] motion wake arm failed, ACC / tilt wake only");
        }
    }
    TiltArm();
    /* Tự kiểm SM2 lúc nạp cố ý gây ngắt INT1: xóa cờ sau khi nạp */
    s_accIrq = 0;
    s_motionIrq = 0;
    s_pwrState = PWR_SLEEP;
    UTIL_SetLowPower(1);
    if (HAL_OS_UsbConnected()) {
        HAL_LOG("[SLEEP] USB connected: module stays in full power until USB is unplugged");
    }
    (void)HAL_OS_SystemSleepEnable(1);
}

static void SleepExit(const char *reason)
{
    uint32_t now = GetTickNow();

    HAL_LOG("[SLEEP] exit: %s after %lus asleep, V=%.1f -> radio on, GNSS on, tasks resume",
            reason, (unsigned long)((now - s_sleepStartTick) / HAL_TICKS_PER_SEC),
            (double)NASA_GetVoltage());
    s_pwrState = PWR_AWAKE;
    UTIL_SetLowPower(0);
    (void)HAL_OS_SystemSleepEnable(0);
    if (s_motionWakeOk) (void)LIS3DSH_MotionDisarm();
    s_motionArmed = 0;
    s_tiltRefOk = 0;
    if (HAL_NET_SetFlightMode(0) != 0) HAL_LOG("[SLEEP] radio on (CFUN=1) fail");
    HAL_GNSS_SetPower(1);
    INDICATION_Wake(); /* gpio_status chạy lại ngay: lọc ACC, LED, nuôi watchdog */
    s_holdStartTick = NonZeroTick(now);
    /* 15 phút mất kết nối (CheckNetworkDisconnTimeout) tính lại từ lúc thức */
    s_disconnStartTick = 0;
    s_nasaState = STATE_INIT;
    s_trackingImmediateSend = 1;
    s_immediateMsgType = 2;
}

/**
 * Gọi mỗi vòng của nasa_reporter: đang thức thì quyết định vào ngủ, đang ngủ thì
 * chỉ xét chân ACC và cảm biến gia tốc để thức.
 */
static void SleepPoll(void)
{
    uint32_t now = GetTickNow();
    const char *block;
    const char *wakeReason = s_wakeReqReason;
    int wakeReq = s_wakeReq;
    int accIrq = s_accIrq;

    if (wakeReq) s_wakeReq = 0;
    if (accIrq) s_accIrq = 0;

    if (s_pwrState == PWR_AWAKE) {
        uint32_t idleSec;
        int delaySec;

        SleepCheckBattery();
        block = SleepBlockReason();
        if (wakeReq) s_holdStartTick = NonZeroTick(now); /* SMS / lệnh khi đang thức: hoãn ngủ */
        if (block) {
            s_idleStartTick = 0;
            return;
        }
        if (s_idleStartTick == 0) {
            s_idleStartTick = NonZeroTick(now);
            HAL_LOG("[SLEEP] ACC off & still: sleep after %ds", s_battLow ?
                    SLEEP_LOW_BATT_DELAY_SEC : CFG_GetSleepDelaySec());
            return;
        }
        if (s_holdStartTick != 0) {
            if ((now - s_holdStartTick) < SecToTicks(SLEEP_EVENT_HOLD_SEC)) return;
            s_holdStartTick = 0;
        }
        idleSec = (now - s_idleStartTick) / HAL_TICKS_PER_SEC;
        delaySec = CFG_GetSleepDelaySec();
        if (s_battLow && delaySec > SLEEP_LOW_BATT_DELAY_SEC) delaySec = SLEEP_LOW_BATT_DELAY_SEC;
        if (idleSec < (uint32_t)delaySec) return;
        /* Đang có phiên mà còn điểm mù: phát bù hết rồi mới ngủ */
        if (NASA_IsSessionActive() && Backup_Count() > 0) return;
        SleepEnter(idleSec);
        return;
    }

    /* Đang ngủ: chỉ ACC và cảm biến gia tốc đánh thức (sóng tắt nên không có SMS /
     * lệnh Server); cấu hình đổi thì cũng thức */
    block = SleepBlockReason();
    if (block) {
        SleepExit(block);
        return;
    }
    if (wakeReq) {
        SleepExit(wakeReason);
        return;
    }
    /* gpio_status đang chờ nên không lọc ACC: đọc thẳng chân (active-low), đọc
     * lại sau 100 ms cho chắc; ngắt chỉ để thức sớm hơn */
    if (HAL_GPIO_Read(GPIO_ACC_IN) == 0) {
        HAL_OS_TaskSleep(SLEEP_ACC_CONFIRM_TICKS);
        if (HAL_GPIO_Read(GPIO_ACC_IN) == 0) {
            SleepExit(accIrq ? "ACC interrupt" : "ACC pin");
            return;
        }
    }
    if (accIrq) {
        HAL_LOG("[SLEEP] ACC interrupt but pin %d high (ACC off) -> ignore, usb=%d",
                GPIO_ACC_IN, HAL_OS_UsbConnected());
        /* Ngắt đánh thức đã tắt sleep hệ thống: cho ngủ lại */
        (void)HAL_OS_SystemSleepEnable(1);
    }
    if (MotionPoll(now)) {
        s_idleStartTick = NonZeroTick(now); /* đếm lại thời gian chờ ngủ */
        SleepExit("motion");
        return;
    }
    if (TiltPoll()) {
        s_idleStartTick = NonZeroTick(GetTickNow());
        SleepExit("tilt");
        return;
    }
    if (SLEEP_STATUS_LOG_SEC > 0 &&
        (now - s_sleepStatusTick) >= SecToTicks(SLEEP_STATUS_LOG_SEC)) {
        s_sleepStatusTick = NonZeroTick(now);
        HAL_LOG("[SLEEP] asleep %lus: sleep flag=%d usb=%d V=%.1f tilt=%d.%d deg",
                (unsigned long)((now - s_sleepStartTick) / HAL_TICKS_PER_SEC),
                HAL_OS_SystemSleepIsEnabled(), HAL_OS_UsbConnected(), (double)NASA_GetVoltage(),
                (s_tiltLastDeg10 < 0) ? -1 : s_tiltLastDeg10 / 10,
                (s_tiltLastDeg10 < 0) ? 0 : s_tiltLastDeg10 % 10);
    }
}

/**
 * Một vòng của nasa_reporter lúc ngủ sâu: nghỉ SLEEP_POLL_SEC giây rồi xét ACC /
 * cảm biến. gpio_status đang chờ nên task này tự nuôi watchdog phần mềm và phần
 * cứng (16 s); gpio_status vẫn thức mỗi SLEEP_IDLE_TASK_SEC giây canh task này treo.
 */
static void SleepDeepStep(void)
{
    NASA_FeedWatchdog();
#if HW_WDT_ENABLE
    HAL_OS_HwWatchdogFeed();
#endif
    HAL_OS_TaskSleep(SecToTicks(SLEEP_POLL_SEC));
    NASA_FeedWatchdog();
#if HW_WDT_ENABLE
    HAL_OS_HwWatchdogFeed();
#endif
    SleepPoll();
}

/**
 * Lấy mẫu khi chưa có phiên với Server (đang kết nối lại / chờ ACK / chờ thử
 * lại): chạy quy tắc dừng đỗ + lái xe và lưu vị trí vào bộ đệm điểm mù theo
 * chu kỳ, để hành trình không bị thủng trong lúc mất mạng.
 */
static void OfflineSample(void)
{
    char dtBuf[40] = {0};
    GpsSnapshot_t gps = {0};
    uint8_t csq = 0;
    float voltage;
    uint32_t status;
    BackupRecord_t bak;

    GPS_Snapshot(&gps);
    BuildDateTimeAutoFallback(dtBuf, sizeof(dtBuf));
    NASA_ParkUpdate(dtBuf, gps.lat, gps.lon, gps.speedKph);
    NASA_WorkUpdate(dtBuf, gps.lat, gps.lon, gps.speedKph);

    /* Đang ngủ: không đọc CSQ / lưu điểm mù (vị trí cuối đã lưu lúc vào ngủ) */
    if (NASA_IsSleeping()) return;

    voltage = NASA_GetVoltage();
    status = BuildDeviceStatus(&gps, voltage);
    HAL_NET_GetCsq(&csq);
    FillBackupFromGps(&bak, &gps, dtBuf, csq, status, voltage);

    /* Cùng chu kỳ với lúc gửi trực tiếp (theo ACC + góc cua), để mật độ điểm
     * trong điểm mù giống hệt lúc có mạng. */
    {
        uint32_t nowTick = GetTickNow();
        int periodSec = (IsAccOn() || s_naActive) ? GetAdaptivePeriodSec(gps.speedKph, gps.headingDeg)
                                                  : StoppedPeriodSec();

        if (s_lastTrackingTick == 0) {
            /* Vừa khởi động: chỉ đặt mốc, không lưu bản ghi thừa khi kết nối bình thường */
            s_lastTrackingTick = (nowTick != 0) ? nowTick : 1;
        } else if ((nowTick - s_lastTrackingTick) >= (uint32_t)periodSec * (uint32_t)HAL_TICKS_PER_SEC) {
            PushBackupIfUsable(&bak);
            s_lastTrackingTick = nowTick;
            s_prevSentHeadingDeg = gps.headingDeg;
        }
    }
}

// Xử lý trạng thái ban đầu
static NasaState_t Handle_StateInit(int pdp_id)
{
    int ret;

    if (!CFG_IsDeviceEnabled()) {
        HAL_LOG("[NASA] Device disabled (cmd38), sleeping...");
        HAL_OS_TaskSleep(10 * HAL_TICKS_PER_SEC);
        return STATE_INIT;
    }
    ret = Network_ActivatePdp(pdp_id);
    OfflineSample();
    if (ret == 0) {
        TriggerNtpSyncIfNeeded();
        return STATE_CONNECT;
    }
    return STATE_ERROR_RETRY;
}

// Xử lý trạng thái kết nối
static NasaState_t Handle_StateConnect(void)
{
    int ret = Network_Connect(CFG_GetServerHost(), CFG_GetServerPort());

    OfflineSample();
    return (ret == 0) ? STATE_LOGIN : STATE_ERROR_RETRY;
}
// Xử lý trạng thái đăng nhập
static NasaState_t Handle_StateLogin(void)
{
    char dateTime[40] = {0};
    char msgId[16] = {0};
    char frame[360] = {0};
    uint32_t startTick;
    uint32_t lastSampleTick;
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
    lastSampleTick = startTick;
    while (!s_loginAcked) {
        NASA_FeedWatchdog();
        if (Network_RecvPoll() != 0) return STATE_ERROR_RETRY;
        if ((GetTickNow() - startTick) >= timeoutTicks) {
            HAL_LOG("[NASA/Login] ACK timeout");
            return STATE_ERROR_RETRY;
        }
        if ((GetTickNow() - lastSampleTick) >= (uint32_t)HAL_TICKS_PER_SEC) {
            lastSampleTick = GetTickNow();
            OfflineSample();
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

    /* đang có mạng: tải AGPS nếu lâu chưa bắt được vệ tinh, hỏi vị trí LBS khi
     * mất GPS (ngủ thì GNSS tắt, giữ vị trí cuối) */
    if (!NASA_IsSleeping()) {
        GPS_AgpsPoll();
        GPS_LbsPoll();
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
            int sleeping = NASA_IsSleeping();
            if (s_prevAccOn != accOn) {
                s_prevAccOn = accOn;
                s_trackingImmediateSend = 1;
                s_immediateMsgType = 2;
            }

            {
                int periodSec;
                uint32_t nowTick = GetTickNow();
                uint32_t periodTicks;

                /* Xe bị dời khi tắt ACC: báo dày như lúc chạy */
                if (!accOn && !s_naActive) periodSec = StoppedPeriodSec();
                else periodSec = GetAdaptivePeriodSec(gpsTemp.speedKph, gpsTemp.headingDeg);

                periodTicks = (uint32_t)periodSec * (uint32_t)HAL_TICKS_PER_SEC;

                /* Đang ngủ: không gửi theo chu kỳ, chỉ gửi khi SleepPoll yêu cầu */
                if (s_trackingImmediateSend || s_lastTrackingTick == 0 ||
                    (!sleeping && (nowTick - s_lastTrackingTick) >= periodTicks)) {
                    uint8_t csq = 0;
                    int msg = s_immediateMsgType;
                    HAL_NET_GetCsq(&csq);
                    s_trackingImmediateSend = 0;
                    s_immediateMsgType = 2;
                    if (SendTrackOrBackup(msg, dtBuf, csq) < 0)
                        return STATE_ERROR_RETRY;
                    s_lastTrackingTick = nowTick;
                    HAL_LOG("[NASA/Tracking] Report sent OK (type=%d, acc=%d, next_in=%ds)", msg, accOn, periodSec);
                }
            }
        }
    }

    return STATE_TRACKING;
}

/** Bật đánh thức bằng INT1 khi có cảm biến (lúc khởi động hoặc khi cảm biến hồi lại) */
static void MotionWakeSetup(void)
{
    if (s_motionWakeOk || !LIS3DSH_IsReady() || LIS3DSH_INT1_PIN < 0) return;
    /* INT1 chốt, tích cực mức thấp (drv_lis3dsh.c) */
    if (HAL_GPIO_WakeupEnable(LIS3DSH_INT1_PIN, HAL_GPIO_EDGE_FALL, MotionIsr) != 0) {
        HAL_LOG("[LIS3DSH] INT1 wakeup not available on pin %d, polling only", LIS3DSH_INT1_PIN);
    }
    s_motionWakeOk = 1;
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
    MotionWakeSetup();
    /* Flash rời khởi tạo lỗi lúc bật nguồn → thử lại trước khi chọn nơi lưu backup */
    if (!EN25_IsReady()) EN25_Init();
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
int NASA_IsStarted(void)
{
    /* NASA_Init nuôi watchdog lần đầu ở cuối hàm */
    return (s_lastWatchdogFeedTick != 0) ? 1 : 0;
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
    /* Đang ngủ: mất kết nối là chuyện bình thường (thử lại thưa), không reset */
    if (!CFG_IsDeviceEnabled() || NASA_IsSleeping()) {
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

void NASA_OnSimChanged(const char *iccid)
{
    if (!iccid || !iccid[0]) return;
    strncpy(s_pendingIccid, iccid, sizeof(s_pendingIccid) - 1);
    s_pendingIccid[sizeof(s_pendingIccid) - 1] = '\0';
    s_simChanged = 1;
}

/** Áp dụng ICCID mới: nếu khác ICCID đang dùng thì đăng nhập lại */
static void ApplySimChange(void)
{
    if (!s_simChanged) return;
    s_simChanged = 0;
    if (strcmp(s_pendingIccid, s_devIccid) == 0) return;

    HAL_LOG("[NASA] SIM changed %s -> %s, re-login", s_devIccid, s_pendingIccid);
    strncpy(s_devIccid, s_pendingIccid, sizeof(s_devIccid) - 1);
    s_devIccid[sizeof(s_devIccid) - 1] = '\0';
    if (s_nasaState == STATE_TRACKING || s_nasaState == STATE_LOGIN) {
        Network_Disconnect();
        s_loginAcked = 0;
    }
    /* Kết nối lại ngay, không chờ hết vòng thử lại */
    s_nasaState = STATE_INIT;
}

/** Log gia tốc định kỳ lúc thức để kiểm tra lắp đặt LIS3DSH (ACCEL_LOG_SEC) */
static void AccelLogPoll(void)
{
    static uint32_t s_lastTick = 0;
    uint32_t now = GetTickNow();
    int16_t mg[3];

    if (ACCEL_LOG_SEC <= 0 || !LIS3DSH_IsReady() || NASA_IsSleeping()) return;
    if (s_lastTick != 0 && (now - s_lastTick) < SecToTicks(ACCEL_LOG_SEC)) return;
    s_lastTick = NonZeroTick(now);
    if (LIS3DSH_ReadXYZ(mg) == 0) {
        HAL_LOG("[ACCEL] x=%d y=%d z=%d mg", mg[0], mg[1], mg[2]);
    } else {
        HAL_LOG("[ACCEL] read failed");
    }
}

/* ---- Kiểm tra sức khỏe LIS3DSH (AccelHealthPoll, lệnh na,44) ---- */
enum {
    ACCEL_FAULT_NONE = 0,
    ACCEL_FAULT_ABSENT,   /* không tìm thấy / khởi tạo lỗi */
    ACCEL_FAULT_I2C,      /* lỗi I2C liên tiếp */
    ACCEL_FAULT_ID,       /* WHO_AM_I sai liên tiếp */
    ACCEL_FAULT_CONFIG,   /* CTRL_REG4 mất cấu hình (chip tự reset) */
    ACCEL_FAULT_STUCK,    /* X/Y/Z thô đứng yên */
    ACCEL_FAULT_GRAVITY,  /* |g| ngoài khoảng hợp lý kéo dài */
    ACCEL_FAULT_COUNT
};
static const char *const s_accelFaultName[ACCEL_FAULT_COUNT] = {
    "OK", "khong co cam bien", "loi I2C", "sai WHO_AM_I", "mat cau hinh (reset)",
    "du lieu dung yen", "|g| bat thuong"
};
static int      s_accelFault = ACCEL_FAULT_NONE;     /* trạng thái hiện tại */
static int      s_accelLastFault = ACCEL_FAULT_NONE; /* lỗi gần nhất đã gặp (kể cả đã hồi) */
static int      s_accelFaultCount = 0;               /* số lần phát hiện lỗi từ lúc khởi động */
static int      s_accelReinitCount = 0;              /* số lần khởi tạo lại thành công */
static int      s_accelFailCnt = 0;                  /* lỗi I2C / ID liên tiếp */
static int      s_accelStuckCnt = 0;
static int      s_accelBadGCnt = 0;
static int      s_accelHaveRaw = 0;
static int16_t  s_accelLastRaw[3];
static int16_t  s_accelLastMg[3];
static int      s_accelLastG = 0;                    /* |g| lần đo gần nhất (mg), 0 = chưa có */
static uint32_t s_accelCheckTick = 0;                /* lần kiểm tra trước, 0 = chưa */
static volatile int s_accelSelfTestReq = 0;          /* lệnh na,44,t: nasa_reporter chạy self-test */
static volatile int s_accelPowerReq = 0;            /* lệnh na,44,0|1: nasa_reporter bật / tắt cảm biến */

static void AccelHealthReset(void)
{
    s_accelFailCnt = 0;
    s_accelStuckCnt = 0;
    s_accelBadGCnt = 0;
    s_accelHaveRaw = 0;
}

static void AccelSelfTestRun(const char *why)
{
    int16_t d[3] = {0, 0, 0};
    int st = LIS3DSH_SelfTest(d);

    HAL_LOG("[ACCEL/HEALTH] self-test (%s) %s: dX=%d dY=%d dZ=%d mg", why,
            st == 0 ? "PASS" : (st > 0 ? "OUT OF RANGE" : "ERROR"), d[0], d[1], d[2]);
}

static void AccelFault(int fault)
{
    s_accelFault = fault;
    s_accelLastFault = fault;
    s_accelFaultCount++;
    HAL_LOG("[ACCEL/HEALTH] FAULT #%d: %s -> re-init", s_accelFaultCount, s_accelFaultName[fault]);
}

/** Khởi tạo lại LIS3DSH; @return 0 nếu thành công */
static int AccelReinit(void)
{
    AccelHealthReset();
    if (LIS3DSH_Init() != 0) {
        s_accelFault = ACCEL_FAULT_ABSENT;
        HAL_LOG("[ACCEL/HEALTH] re-init failed, retry in %ds", ACCEL_REINIT_SEC);
        return -1;
    }
    s_accelReinitCount++;
    s_accelFault = ACCEL_FAULT_NONE;
    s_turnLevel = 0;
    s_turnPending = 0;
    HAL_LOG("[ACCEL/HEALTH] re-init OK (#%d)", s_accelReinitCount);
    AccelSelfTestRun("re-init");
    MotionWakeSetup(); /* cảm biến không có lúc khởi động mà nay mới nhận */
    return 0;
}

/**
 * Chạy mỗi vòng nasa_reporter lúc thức (không chạy khi đang thử độ nhạy vì
 * SM1 đang nạp). Mỗi ACCEL_HEALTH_SEC: đọc WHO_AM_I, CTRL_REG4, một mẫu XYZ.
 *  - lỗi I2C / sai ID ACCEL_FAIL_MAX lần liên tiếp, hoặc mất cấu hình -> khởi tạo lại;
 *  - X/Y/Z thô y hệt ACCEL_STUCK_MAX lần (cảm biến thật luôn có nhiễu vài LSB),
 *    hoặc |g| ngoài [ACCEL_G_MIN_MG, ACCEL_G_MAX_MG] ACCEL_BAD_G_MAX lần -> khởi tạo lại;
 *  - không có cảm biến: thử khởi tạo lại mỗi ACCEL_REINIT_SEC.
 */
static void AccelHealthPoll(void)
{
    uint32_t now = GetTickNow();
    Lis3dshCheck_t c;
    int rc, k, fault = ACCEL_FAULT_NONE;
    double g2;

    if (s_accelPowerReq) {
        s_accelPowerReq = 0;
        if (!CFG_GetAccelEnabled()) {
            if (s_motionTestOn) {
                s_motionTestOn = 0;
                HAL_LOG("[ACCEL/TEST] stopped: sensor disabled");
            }
            s_turnLevel = 0;
            s_turnPending = 0;
            s_accelSelfTestReq = 0;
            AccelHealthReset();
            s_accelFault = ACCEL_FAULT_NONE;
            HAL_LOG("[ACCEL] disabled by cmd 44 -> power down %s", LIS3DSH_PowerDown() == 0 ? "OK" : "fail");
            return;
        }
        if (!LIS3DSH_IsReady()) {
            HAL_LOG("[ACCEL] enabled by cmd 44 -> init");
            s_accelCheckTick = NonZeroTick(now);
            (void)AccelReinit();
            return;
        }
    }
    if (!CFG_GetAccelEnabled()) return; /* tắt bằng lệnh 44: không kiểm tra, không thử khởi tạo lại */
    if (ACCEL_HEALTH_SEC <= 0 || NASA_IsSleeping() || s_motionTestOn) return;
    if (!LIS3DSH_IsReady()) {
        s_accelFault = ACCEL_FAULT_ABSENT;
        s_accelSelfTestReq = 0;
        if (s_accelCheckTick != 0 && (now - s_accelCheckTick) < SecToTicks(ACCEL_REINIT_SEC)) return;
        s_accelCheckTick = NonZeroTick(now);
        HAL_LOG("[ACCEL/HEALTH] sensor not ready, try init");
        (void)AccelReinit();
        return;
    }
    if (s_accelSelfTestReq) {
        s_accelSelfTestReq = 0;
        AccelSelfTestRun("cmd");
    }
    if (s_accelCheckTick != 0 && (now - s_accelCheckTick) < SecToTicks(ACCEL_HEALTH_SEC)) return;
    s_accelCheckTick = NonZeroTick(now);

    rc = LIS3DSH_Check(&c);
    if (rc == LIS3DSH_CHECK_I2C || rc == LIS3DSH_CHECK_ID) {
        s_accelFailCnt++;
        HAL_LOG("[ACCEL/HEALTH] %s (WHO_AM_I=%d) %d/%d", (rc == LIS3DSH_CHECK_I2C) ? "I2C error" : "bad ID",
                c.whoAmI, s_accelFailCnt, ACCEL_FAIL_MAX);
        if (s_accelFailCnt >= ACCEL_FAIL_MAX) fault = (rc == LIS3DSH_CHECK_I2C) ? ACCEL_FAULT_I2C : ACCEL_FAULT_ID;
    } else if (rc == LIS3DSH_CHECK_CONFIG) {
        HAL_LOG("[ACCEL/HEALTH] CTRL_REG4=0x%02X lost config", c.ctrl4 & 0xFF);
        fault = ACCEL_FAULT_CONFIG;
    } else {
        s_accelFailCnt = 0;
        memcpy(s_accelLastMg, c.mg, sizeof(c.mg));
        g2 = 0.0;
        for (k = 0; k < 3; k++) g2 += (double)c.mg[k] * c.mg[k];
        s_accelLastG = (int)(sqrt(g2) + 0.5);

        if (s_accelHaveRaw && memcmp(s_accelLastRaw, c.raw, sizeof(c.raw)) == 0) {
            s_accelStuckCnt++;
        } else {
            s_accelStuckCnt = 0;
        }
        memcpy(s_accelLastRaw, c.raw, sizeof(c.raw));
        s_accelHaveRaw = 1;

        if (s_accelLastG < ACCEL_G_MIN_MG || s_accelLastG > ACCEL_G_MAX_MG) {
            s_accelBadGCnt++;
            HAL_LOG("[ACCEL/HEALTH] |g|=%dmg out of %d..%d (%d/%d)", s_accelLastG,
                    ACCEL_G_MIN_MG, ACCEL_G_MAX_MG, s_accelBadGCnt, ACCEL_BAD_G_MAX);
        } else {
            s_accelBadGCnt = 0;
        }
        if (s_accelStuckCnt >= ACCEL_STUCK_MAX) fault = ACCEL_FAULT_STUCK;
        else if (s_accelBadGCnt >= ACCEL_BAD_G_MAX) fault = ACCEL_FAULT_GRAVITY;
    }
    if (fault != ACCEL_FAULT_NONE) {
        AccelFault(fault);
        if (AccelReinit() == 0) s_accelCheckTick = NonZeroTick(GetTickNow());
    }
}

void NASA_AccelSelfTestRequest(void)
{
    s_accelSelfTestReq = 1;
}

void NASA_AccelPowerRequest(void)
{
    s_accelPowerReq = 1;
}

int NASA_AccelStatus(char *buf, int len)
{
    int16_t d[3] = {0, 0, 0};
    int st = LIS3DSH_GetLastSelfTest(d);
    const char *stName = (st == 0) ? "PASS" : (st == 1) ? "LECH" : (st == -1) ? "LOI" : "chua chay";
    int n;

    if (!buf || len <= 0) return 0;
    n = snprintf(buf, (size_t)len,
                 "%s, addr 0x%02X, x=%d y=%d z=%d |g|=%dmg, self-test %s (%d,%d,%d), loi %d lan (cuoi: %s), khoi tao lai %d",
                 !CFG_GetAccelEnabled() ? "TAT" :
                 s_accelFaultName[LIS3DSH_IsReady() ? s_accelFault : ACCEL_FAULT_ABSENT],
                 LIS3DSH_GetAddr(), s_accelLastMg[0], s_accelLastMg[1], s_accelLastMg[2], s_accelLastG,
                 stName, d[0], d[1], d[2], s_accelFaultCount, s_accelFaultName[s_accelLastFault],
                 s_accelReinitCount);
    return (n < 0) ? 0 : n;
}

void NASA_MotionTestStart(int sec)
{
    if (sec <= 0) sec = MOTION_TEST_SEC_DEFAULT;
    if (sec > MOTION_TEST_SEC_MAX) sec = MOTION_TEST_SEC_MAX;
    s_motionTestReqSec = sec;
}

int NASA_MotionInfo(int *thrMg, int *testOn, int *testCount, int *testSec)
{
    if (thrMg) *thrMg = s_motionThrMg;
    if (testOn) *testOn = (s_motionTestOn || s_motionTestReqSec) ? 1 : 0;
    if (testCount) *testCount = s_motionTestCount;
    if (testSec) *testSec = s_motionTestSec;
    return s_motionWakeOk;
}

/**
 * Thử độ nhạy lúc đang thức: nạp SM1 như lúc vào ngủ (ngưỡng theo lệnh 43),
 * mỗi ngắt log số thứ tự và X/Y/Z để chỉnh ngưỡng mà không phải chờ ngủ.
 */
static void MotionTestPoll(void)
{
    uint32_t now = GetTickNow();
    int reqSec = s_motionTestReqSec;

    if (reqSec) {
        s_motionTestReqSec = 0;
        if (!s_motionWakeOk || !LIS3DSH_IsReady()) {
            HAL_LOG("[ACCEL/TEST] LIS3DSH / INT1 not available%s", CFG_GetAccelEnabled() ? "" : " (disabled, cmd 44)");
        } else if (NASA_IsSleeping()) {
            HAL_LOG("[ACCEL/TEST] skipped: device asleep");
        } else if (LIS3DSH_MotionArm(CFG_GetMotionDeltaMg(), &s_motionThrMg) == 0) {
            s_motionTestOn = 1;
            s_motionTestSec = reqSec;
            s_motionTestCount = 0;
            s_motionIrq = 0;
            s_motionTestEndTick = GetTickNow() + SecToTicks((uint32_t)reqSec); /* nạp mất ~1 s (tự kiểm) */
            HAL_LOG("[ACCEL/TEST] start %ds: delta=%dmg thr=%dmg, wake needs %d irq in %ds",
                    reqSec, CFG_GetMotionDeltaMg(), s_motionThrMg,
                    CFG_GetMotionEvents(), CFG_GetMotionWindowSec());
        }
    }
    if (!s_motionTestOn) return;
    if (NASA_IsSleeping()) {
        s_motionTestOn = 0; /* SleepEnter đã nạp lại SM1 cho chế độ ngủ */
        return;
    }
    if (s_motionIrq || HAL_GPIO_Read(LIS3DSH_INT1_PIN) == 0) {
        int16_t mg[3] = {0, 0, 0};
        int outs;

        s_motionIrq = 0;
        outs = LIS3DSH_MotionClear();
        (void)LIS3DSH_ReadXYZ(mg);
        s_motionTestCount++;
        HAL_LOG("[ACCEL/TEST] motion #%d OUTS=0x%02X x=%d y=%d z=%d mg",
                s_motionTestCount, outs & 0xFF, mg[0], mg[1], mg[2]);
    }
    if ((int32_t)(now - s_motionTestEndTick) >= 0) {
        s_motionTestOn = 0;
        (void)LIS3DSH_MotionDisarm();
        HAL_LOG("[ACCEL/TEST] done: %d irq in %ds (thr=%dmg)",
                s_motionTestCount, s_motionTestSec, s_motionThrMg);
    }
}

static void TurnSetLevel(int level, double speedKph)
{
    static const char *const names[] = {"end", "turn", "sharp"};
    double v = speedKph / 3.6;
    /* Tốc độ quay ước lượng: a_ngang / v (rad/s -> độ/s) */
    double yawDps = (v > 0.5) ? ((double)s_turnLatMg * 9.80665 / 1000.0) / v * 57.2958 : 0.0;

    s_turnLevel = level;
    HAL_LOG("[TURN] %s: lat=%dmg long=%dmg v=%.0fkm/h yaw~%.0fdeg/s -> period %ds",
            names[level], s_turnLatMg, s_turnLongMg, speedKph, yawDps,
            TurnPeriodSec(level));
}

/**
 * Xác nhận vào cua bằng gia tốc ngang, chạy mỗi vòng nasa_reporter (~1 s) lúc thức.
 *
 * Không biết hướng lắp cảm biến, nên:
 *  - trọng lực ước lượng bằng lọc thông thấp véc-tơ gia tốc (nhanh khi xe đứng,
 *    rất chậm khi chạy, đóng băng khi đang cua);
 *  - gia tốc ngang = phần vuông góc trọng lực (bỏ xóc lên xuống), trừ phần dọc
 *    tính từ thay đổi vận tốc GPS (tăng / giảm tốc);
 *  - mỗi lần đo là trung bình FIFO ~1 s nên rung mặt đường đã được lọc.
 * Mức mới phải giữ ACCEL_TURN_CONFIRM_SEC (vào) / ACCEL_TURN_RELEASE_SEC (ra)
 * giây liên tiếp; ngưỡng ra thấp hơn ngưỡng vào (trễ 30%).
 */
static void TurnPoll(void)
{
    GpsSnapshot_t gps = {0};
    int32_t a[3];
    int n = 0, k, level, thrTurn, thrSharp;
    double speed, gn, dot, h2, lat2;
    float alpha;

    if (!ACCEL_TURN_ENABLE || !LIS3DSH_IsReady() || NASA_IsSleeping() || s_motionTestOn) {
        s_turnLevel = 0;
        s_turnPending = 0;
        return;
    }
    if (LIS3DSH_ReadAvgXYZ(a, &n) != 0 || n < 5) return; /* lần đầu chỉ bật FIFO */

    GPS_Snapshot(&gps);
    speed = gps.valid ? gps.speedKph : 0.0;
    if (gps.valid && gps.fixTick != s_turnPrevFixTick) {
        uint32_t dtTicks = gps.fixTick - s_turnPrevFixTick;
        if (s_turnPrevSpeed >= 0.0 && dtTicks >= HAL_TICKS_PER_SEC / 2 && dtTicks <= 5 * HAL_TICKS_PER_SEC) {
            double dv = (speed - s_turnPrevSpeed) / 3.6;                     /* m/s */
            double acc = dv / ((double)dtTicks / HAL_TICKS_PER_SEC);         /* m/s^2 */
            s_turnLongMg = (int)(fabs(acc) / 9.80665 * 1000.0);
        }
        s_turnPrevSpeed = speed;
        s_turnPrevFixTick = gps.fixTick;
    } else if (!gps.valid) {
        s_turnPrevSpeed = -1.0;
        s_turnLongMg = 0;
    }

    if (!s_gravOk) {
        for (k = 0; k < 3; k++) s_gravEst[k] = (float)a[k];
        s_gravOk = 1;
        return;
    }
    alpha = (speed < 3.0) ? 0.3f : (s_turnLevel ? 0.0f : (1.0f / ACCEL_TURN_GRAVITY_TAU_SEC));
    for (k = 0; k < 3; k++) s_gravEst[k] += alpha * ((float)a[k] - s_gravEst[k]);

    gn = sqrt((double)s_gravEst[0] * s_gravEst[0] + (double)s_gravEst[1] * s_gravEst[1] +
              (double)s_gravEst[2] * s_gravEst[2]);
    if (gn < 500.0) return; /* chưa ước lượng được trọng lực */
    dot = ((double)a[0] * s_gravEst[0] + (double)a[1] * s_gravEst[1] + (double)a[2] * s_gravEst[2]) / gn;
    h2 = (double)a[0] * a[0] + (double)a[1] * a[1] + (double)a[2] * a[2] - dot * dot; /* |phần nằm ngang|^2 */
    lat2 = h2 - (double)s_turnLongMg * s_turnLongMg;
    s_turnLatMg = (lat2 > 0.0) ? (int)sqrt(lat2) : 0;

    thrTurn = (s_turnLevel >= 1) ? ACCEL_TURN_MG * 7 / 10 : ACCEL_TURN_MG;
    thrSharp = (s_turnLevel >= 2) ? ACCEL_TURN_SHARP_MG * 7 / 10 : ACCEL_TURN_SHARP_MG;
    level = 0;
    /* Gia tốc dọc lớn hơn ngang = đang phanh / tăng tốc, không tính là cua */
    if (speed >= HEADING_MIN_SPEED_KPH && s_turnLatMg > s_turnLongMg) {
        if (s_turnLatMg >= thrSharp) level = 2;
        else if (s_turnLatMg >= thrTurn) level = 1;
    }
    if (level == s_turnLevel) {
        s_turnPending = 0;
        return;
    }
    if (++s_turnPending >= ((level > s_turnLevel) ? ACCEL_TURN_CONFIRM_SEC : ACCEL_TURN_RELEASE_SEC)) {
        s_turnPending = 0;
        TurnSetLevel(level, speed);
    }
}

/** Dây ACC báo tắt và tin được (accMode 1, hoặc 2 đã từng thấy dây bật) */
static int WireAccOffTrusted(void)
{
    int mode = CFG_GetAccMode();

    if (s_accRaw != 0) return 0; /* bật hoặc chưa đọc được chân */
    return (mode == 1 || (mode == 2 && s_accWireSeenOn)) ? 1 : 0;
}

static double DistanceM(double lat1, double lon1, double lat2, double lon2)
{
    const double rad = 3.14159265358979 / 180.0;
    double dLat = (lat2 - lat1) * rad;
    double dLon = (lon2 - lon1) * rad;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * rad) * cos(lat2 * rad) * sin(dLon / 2) * sin(dLon / 2);
    if (a > 1.0) a = 1.0;
    return 6371000.0 * 2.0 * asin(sqrt(a));
}

/** SMS cảnh báo xe bị dời khi tắt ACC tới số chủ xe 1, 2 (lệnh 17, 18) */
static void NoAccMoveSms(const GpsSnapshot_t *gps, int distM)
{
    char msg[160];
    int i, sent = 0;
    uint32_t now = GetTickNow();

    if (!NOACC_MOVE_SMS_ENABLE) return;
    if (s_naSmsTick != 0 && (now - s_naSmsTick) < SecToTicks(NOACC_MOVE_SMS_MIN_SEC)) {
        HAL_LOG("[NOACC] SMS skipped (last %lus ago)",
                (unsigned long)((now - s_naSmsTick) / HAL_TICKS_PER_SEC));
        return;
    }
    snprintf(msg, sizeof(msg), "CANH BAO %s: xe di chuyen khi tat may, %.0fkm/h, cach cho do %dm "
             "https://maps.google.com/maps?q=%.6f,%.6f",
             CFG_GetPlate(), gps->speedKph, distM, gps->lat, gps->lon);
    for (i = 1; i <= 2; i++) {
        const char *ph = CFG_GetPhone(i);
        if (!ph || !ph[0]) continue;
        NASA_FeedWatchdog();
        HAL_LOG("[NOACC] SMS to %s %s", ph, (HAL_SMS_Send(ph, msg) == 0) ? "OK" : "fail");
        sent = 1;
    }
    if (sent) s_naSmsTick = NonZeroTick(now);
    else HAL_LOG("[NOACC] no owner phone (cmd 17/18) -> no SMS");
}

static void NoAccMoveReset(void)
{
    s_naActive = 0;
    s_naRefOk = 0;
    s_naRefFresh = 0;
    s_naSpeedTick = 0;
    s_naStopTick = 0;
}

/**
 * Xe di chuyển khi tắt ACC (bị đẩy / kéo / chở), gọi mỗi vòng lúc thức. Lúc dây
 * ACC tắt, FilterMovingStatus coi xe đứng yên bất kể vận tốc, nên ở đây xét
 * thẳng vận tốc GPS (fix thật) và quãng dời khỏi chỗ đỗ.
 */
static void NoAccMovePoll(void)
{
    GpsSnapshot_t gps = {0};
    uint32_t now = GetTickNow();
    double thresh = (double)CFG_GetSpeedThresh();
    int fresh, fast, distM = -1;

    if (!NOACC_MOVE_ENABLE || !WireAccOffTrusted()) {
        if (s_naActive) HAL_LOG("[NOACC] end: ACC on");
        NoAccMoveReset();
        return;
    }
    if (thresh <= 0.0) thresh = 3.0;
    GPS_Snapshot(&gps);
    fresh = (gps.valid && gps.source == GPS_SRC_GNSS && (gps.lat != 0.0 || gps.lon != 0.0));
    fast = (fresh && gps.speedKph > thresh) ? 1 : 0;

    /* Chỗ đỗ: fix thật lúc đứng yên; chưa có thì tạm lấy vị trí GNSS cuối đã lưu */
    if (!s_naActive && !s_naRefFresh && fresh && !fast) {
        s_naRefLat = gps.lat;
        s_naRefLon = gps.lon;
        s_naRefOk = 1;
        s_naRefFresh = 1;
    } else if (!s_naRefOk && gps.source == GPS_SRC_HOLD && (gps.lat != 0.0 || gps.lon != 0.0)) {
        s_naRefLat = gps.lat;
        s_naRefLon = gps.lon;
        s_naRefOk = 1;
    }

    if (!s_naActive) {
        uint32_t confirm = SecToTicks(NOACC_MOVE_CONFIRM_SEC);

        if (!fast) {
            s_naSpeedTick = 0;
            return;
        }
        if (s_naSpeedTick == 0) {
            s_naSpeedTick = NonZeroTick(now);
            return;
        }
        if (s_naRefOk) {
            distM = (int)DistanceM(s_naRefLat, s_naRefLon, gps.lat, gps.lon);
            if ((now - s_naSpeedTick) < confirm || distM < NOACC_MOVE_MIN_DIST_M) return;
        } else if ((now - s_naSpeedTick) < 3u * confirm) {
            return; /* không biết chỗ đỗ: chỉ tin vận tốc, chờ lâu hơn */
        }
        s_naActive = 1;
        s_naStopTick = 0;
        HAL_LOG("[NOACC] start: ACC off but %.1fkm/h for %lus, %dm from park (%s) -> report now, no sleep",
                gps.speedKph, (unsigned long)((now - s_naSpeedTick) / HAL_TICKS_PER_SEC), distM,
                s_naRefOk ? (s_naRefFresh ? "fix" : "last pos") : "unknown");
        s_trackingImmediateSend = 1;
        s_immediateMsgType = 2;
        NoAccMoveSms(&gps, distM);
        return;
    }

    /* Đang cảnh báo: đứng yên (hoặc mất fix) đủ lâu thì hết; chỗ đỗ mới lấy lại từ đầu */
    if (fast) {
        s_naStopTick = 0;
        return;
    }
    if (s_naStopTick == 0) {
        s_naStopTick = NonZeroTick(now);
        return;
    }
    if ((now - s_naStopTick) < SecToTicks(NOACC_MOVE_END_SEC)) return;
    HAL_LOG("[NOACC] end: still %ds", NOACC_MOVE_END_SEC);
    NoAccMoveReset();
    s_trackingImmediateSend = 1;
    s_immediateMsgType = 2;
}

int NASA_IsMovingNoAcc(void)
{
    return s_naActive;
}

// Xử lý logic chính của NASA
void NASA_RunStep(void)
{
    static uint32_t s_lastCfgFlushTick = 0;
    uint32_t nowFlush = GetTickNow();

    /* Ngủ sâu: chỉ đọc ACC / cảm biến gia tốc, không chạy gì khác */
    if (s_pwrState == PWR_SLEEP) {
        SleepDeepStep();
        return;
    }

    NASA_FeedWatchdog();
    SleepPoll();
    if (NASA_IsSleeping()) return; /* vừa vào ngủ: vòng sau chạy SleepDeepStep */
    NoAccMovePoll();
    AccelHealthPoll();
    AccelLogPoll();
    MotionTestPoll();
    TurnPoll();
    ApplySimChange();
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
        Network_Disconnect();
        s_loginAcked = 0;
        {
            int sec;
            HAL_LOG("[NASA/Retry] wait %ds", ERROR_RETRY_DELAY_SEC);
            for (sec = 0; sec < ERROR_RETRY_DELAY_SEC; sec++) {
                NASA_FeedWatchdog();
                CheckNetworkDisconnTimeout();
                HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
                SleepPoll();
                if (NASA_IsSleeping()) break; /* vào ngủ sâu: không thử kết nối nữa */
                NoAccMovePoll();
                OfflineSample();
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
    if (accVal) s_accWireSeenOn = 1;
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
