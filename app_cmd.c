#include "simcom_api.h"
#include "app_config.h"
#include "app_cfg.h"
#include "app_cmd.h"
#include "app_gps.h"
#include "app_network.h"
#include "app_backup.h"
#include "app_utils.h"
#include "app_nasa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *strtok_r(char *str, const char *delim, char **saveptr);

static volatile int g_fota_download_ready = 0;

static int FotaCb(int isok)
{
    sAPI_Debug("[FOTA] status=%d", isok);
    g_fota_download_ready = (isok == 100) ? 1 : 0;
    return 0;
}

int CMD_IsFotaReady(void) { return g_fota_download_ready; }
void CMD_SetFotaHandled(void) { g_fota_download_ready = 2; }

static void Reply(CmdReplyFn reply, void *ctx, const char *src, const char *msg)
{
    char payload[384];
    char out[420];
    uint32_t cs;
    const char *content;
    const char *stripped;
    char dateTime[40] = {0};
    uint32_t msgId;

    if (!reply || !msg || !msg[0]) return;

    /* SMS / local: gửi nguyên văn */
    if (!src || strcmp(src, "SERVER") != 0) {
        reply(msg, ctx);
        return;
    }

    /* Nếu msg đã được đóng gói sẵn dạng !NASA,...*checksum, gửi trực tiếp */
    if (strncmp(msg, "!NASA", 5) == 0 && strchr(msg, '*') != NULL) {
        reply(msg, ctx);
        return;
    }

    content = msg;
    if (strncmp(msg, "!NASA,13,", 9) == 0)
        content = msg + 9;

    /* Loại bỏ tiền tố "sa,X " (nếu có) khi gửi lên server */
    stripped = content;
    if (strncmp(content, "sa,", 3) == 0) {
        const char *p = content + 3;
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        if (*p == ' ') {
            stripped = p + 1;
        } else {
            stripped = p;
        }
    }

    BuildDateTimeAutoFallback(dateTime, sizeof(dateTime));
    msgId = NASA_GetNextMessageId();

    /* Đóng gói dạng !NASA,13,<msgId>,<dateTime>,<stripped>,*checksum */
    snprintf(payload, sizeof(payload), "!NASA,13,%lu,%s,%s,", (unsigned long)msgId, dateTime, stripped);
    cs = Buffer_GetChecksum((const uint8_t *)payload, (uint32_t)strlen(payload));
    snprintf(out, sizeof(out), "%s*%lu", payload, (unsigned long)cs);
    reply(out, ctx);
}

static void Deny(CmdReplyFn reply, void *ctx, const char *src)
{
    Reply(reply, ctx, src, "Ban khong co quyen thuc hien chuc nang nay");
}

static int NextToken(char **save, char *out, int outsz)
{
    char *t = strtok_r(NULL, ",", save);
    if (!t) {
        if (out && outsz > 0) out[0] = '\0';
        return 0;
    }
    strncpy(out, t, outsz - 1);
    out[outsz - 1] = '\0';
    return 1;
}

#if 0
static void BuildDeviceInfo(char *out, int outsz)
{
    char imei[32] = {0};
    char dt[40] = {0};
    char boot[20] = {0};
    UINT8 csq = 0;
    GpsSnapshot_t gps = {0};
    t_rtc rtc;
    int gprs = Network_IsConnected() ? 1 : 0;
    int gpsSt;
    int fix;

    sAPI_SysGetImei(imei);
    sAPI_NetworkGetCsq(&csq);
    GPS_Snapshot(&gps);
    BuildDateTimeAutoFallback(dt, sizeof(dt));
    sAPI_GetRealTimeClock(&rtc);

    /* boot time gần nhất — dùng RTC hiện tại làm placeholder */
    snprintf(boot, sizeof(boot), "%02d%02d%02d%02d%02d%02d",
             rtc.tm_year % 100, rtc.tm_mon, rtc.tm_mday,
             rtc.tm_hour, rtc.tm_min, rtc.tm_sec);

    gpsSt = (gps.satellites > 0) ? 0 : 1; /* 0=OK 1=ERROR */
    fix = gps.valid ? 1 : 0;

    snprintf(out, outsz,
             "%s,%s,%s,%s,%d,%d,%s\r\n"
             "GSM,%d,%u\r\n"
             "GPS,%d,%d,%d\r\n"
             "SV,%s,%d,%d\r\n"
             "PW,%d,%.2f\r\n"
             "TI,%02d%02d%02d-%02d:%02d:%02d",
             NASA_DEVICE_NAME, imei, NASA_FW_CODE, NASA_HW_CODE,
             CFG_IsLocked() ? 1 : 0, CFG_Get()->operateDays, boot,
             gprs, (unsigned)csq,
             gpsSt, fix, gps.satellites,
             CFG_GetServerHost(), CFG_GetServerPort(), NASA_IsSessionActive() ? 1 : 0,
             0, (double)NASA_GetVoltage(),
             rtc.tm_year % 100, rtc.tm_mon, rtc.tm_mday,
             rtc.tm_hour, rtc.tm_min, rtc.tm_sec);
}
#endif

void CMD_Execute(const char *body, const char *src, const char *fromPhone,
                 CmdReplyFn reply, void *ctx)
{
    char buf[256];
    char *save = NULL;
    char *tag;
    char *codeStr;
    int code;
    int isSet;
    char resp[384];
    char a[96], b[96];

    if (!body || body[0] == '\0') return;

    /* Bỏ qua bản tin giao thức !NASA (xử lý ở NASA) */
    if (strncmp(body, "!NASA", 5) == 0) return;

    strncpy(buf, body, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tag = strtok_r(buf, ",", &save);
    if (!tag) return;

    /* Hỗ trợ legacy lệnh cũ để tương thích tạm thời */
    if (strcmp(tag, "reset#") == 0 || (strcmp(tag, "reset") == 0)) {
        Reply(reply, ctx, src, "OK");
        sAPI_TaskSleep(200);
        sAPI_SysReset();
        return;
    }

    if (strcmp(tag, "update") == 0) {
        if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
        struct SC_FotaApiParam param;
        memset(&param, 0, sizeof(param));
        param.mode = (strncmp(a, "ftp://", 6) == 0) ? 0 : 1;
        {
            const char *host = a;
            if (strncmp(a, "ftp://", 6) == 0) host = a + 6;
            else if (strncmp(a, "https://", 8) == 0) host = a + 8;
            else if (strncmp(a, "http://", 7) == 0) host = a + 7;
            strncpy(param.host, host, sizeof(param.host) - 1);
        }
        param.sc_fota_cb = FotaCb;
        sAPI_FotaServiceBegin(&param);

        snprintf(resp, sizeof(resp), "Update Firmwwave ok");
        if (resp[0]) Reply(reply, ctx, src, resp);
        return;
    }

    if (strcmp(tag, "na") != 0 && strcmp(tag, "sa") != 0) {
        sAPI_Debug("[CMD/%s] ignore unknown tag: %s", src ? src : "?", tag);
        return;
    }

    isSet = (strcmp(tag, "na") == 0);
    codeStr = strtok_r(NULL, ",", &save);
    if (!codeStr) return;
    code = atoi(codeStr);
    resp[0] = '\0';

    sAPI_Debug("[CMD/%s] %s code=%d raw=%s", src ? src : "?", isSet ? "SET" : "GET", code, body);

    switch (code) {
    case 1: /* IP/Port */
        if (isSet) {
            if (!CFG_CanChangeProtected(src, fromPhone)) { Deny(reply, ctx, src); return; }
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            if (!NextToken(&save, b, sizeof(b))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetServer(a, atoi(b));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,1 ip/port: %s / %d", CFG_GetServerHost(), CFG_GetServerPort());
        break;

    case 2: /* D_OUT (điều khiển rơ-le) */
    {
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            int val = 0;
            if (strcmp(a, "on") == 0 || strcmp(a, "1") == 0) {
                val = 1;
            } else if (strcmp(a, "off") == 0 || strcmp(a, "0") == 0) {
                val = 0;
            } else {
                val = atoi(a);
            }
            CFG_SetDout(val);
            CFG_Save();
            sAPI_GpioSetValue(RV26_GPIO_DOUT, CFG_GetDout() ? 1 : 0);
        }
        snprintf(resp, sizeof(resp), "sa,2 dieu khien ra: %s", CFG_GetDout() ? "bat" : "tat");
        break;
    }

    case 4: /* Tần suất chạy/dừng */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a)) || !NextToken(&save, b, sizeof(b))) {
                Reply(reply, ctx, src, "ERROR"); return;
            }
            CFG_SetPeriod(atoi(a), atoi(b));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,4 tan suat truyen: %d/%d",
                 CFG_GetPeriodMoving(), CFG_GetPeriodStopped());
        break;

    case 8: /* Chế độ ACC */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetAccMode(atoi(a));
            CFG_Save();
        }
        {
            int m = CFG_GetAccMode();
            const char *txt = (m == 0) ? "khong dau day ACC" :
                              (m == 1) ? "co dau day ACC" : "co dau + khong dau ACC";
            snprintf(resp, sizeof(resp), "sa,8 %s", txt);
        }
        break;

    case 9: /* Reset */
        if (isSet) {
            snprintf(resp, sizeof(resp), "reset ok");
            Reply(reply, ctx, src, resp);
            sAPI_TaskSleep(SC_TICKS_PER_SECOND * 5);
            sAPI_SysReset();
            return;
        }
        break;

    case 10: /* Ngưỡng vận tốc */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetSpeedThresh(atoi(a));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,10 nguong van toc: %d km/h", CFG_GetSpeedThresh());
        break;

    case 11: /* Im lặng */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetSilentMode(atoi(a));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,11 trang thai im lang: %s",
                 CFG_GetSilentMode() ? "bat" : "tat");
        break;

    case 12: /* Yêu cầu gửi lại dữ liệu */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a)) || !NextToken(&save, b, sizeof(b))) {
                Reply(reply, ctx, src, "ERROR"); return;
            }
            NASA_RequestReplay(a, b);
            snprintf(resp, sizeof(resp), "bat dau truyen lai du lieu cu");
        }
        break;

    case 17: /* Phone 1 */
    case 18: /* Phone 2 — doc dùng phone index = code-16 */
    {
        int idx = (code == 17) ? 1 : 2;
        if (isSet) {
            if (!CFG_CanChangeProtected(src, fromPhone)) { Deny(reply, ctx, src); return; }
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetPhone(idx, a);
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,%d so dien thoai %d: %s", code, idx, CFG_GetPhone(idx));
        break;
    }

    case 26: /* Thời gian ghi nhận dừng đỗ */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetParkConfirmSec(atoi(a));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,26 thoi gian ghi nhan dung/do: %d giay",
                 CFG_GetParkConfirmSec());
        break;

    case 27: /* Lấy tọa độ */
    {
        GpsSnapshot_t gps = {0};
        GPS_Snapshot(&gps);
        if (gps.valid) {
            int latD = (int)gps.lat;
            double latM = (gps.lat - latD) * 60.0;
            if (latM < 0) latM = -latM;
            int lonD = (int)gps.lon;
            double lonM = (gps.lon - lonD) * 60.0;
            if (lonM < 0) lonM = -lonM;
            snprintf(resp, sizeof(resp),
                     "sa,27 https://maps.google.com/maps?q=N%%20%d%%20%.6f%%20E%%20%d%%20%.6f#OK",
                     latD < 0 ? -latD : latD, latM, lonD < 0 ? -lonD : lonD, lonM);
        } else {
            snprintf(resp, sizeof(resp), "sa,27 NO FIX");
        }
        break;
    }

    case 29: /* Thông tin thiết bị */
        {
            char imei[32] = {0};
            char iccid[32] = {0};
            UINT8 csq = 0;
            GpsSnapshot_t gps = {0};
            int acc = 0;
            const char *conn_str = "";

            sAPI_SysGetImei(imei);
            if (sAPI_SysGetIccid(iccid) != SC_SIM_RETURN_SUCCESS) {
                strncpy(iccid, "UNKNOWN_SIM", sizeof(iccid) - 1);
            }
            sAPI_NetworkGetCsq(&csq);
            GPS_Snapshot(&gps);
            acc = (sAPI_GpioGetValue(RV26_GPIO_ACC_IN) == SC_GPIORC_LOW) ? 1 : 0;
            conn_str = (Network_IsConnected() && NASA_IsSessionActive()) ? "da ket noi" : "chua ket noi";

            snprintf(resp, sizeof(resp),
                     "NASA4G %s %s GSM: %u GPS: %d %s %d %s %d",
                     imei, iccid, (unsigned)csq, gps.satellites,
                     CFG_GetServerHost(), CFG_GetServerPort(),
                     conn_str, acc);
        }
        break;

    case 32: /* Lịch reset */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a)) || !NextToken(&save, b, sizeof(b))) {
                Reply(reply, ctx, src, "ERROR"); return;
            }
            CFG_SetResetSchedule(atoi(a), atoi(b));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,32 lich reset lai thiet bi: %d,%d",
                 CFG_Get()->resetEveryDays, CFG_Get()->resetAtHour);
        break;

    case 33: /* FOTA */
        if (isSet) {
            struct SC_FotaApiParam param;
            memset(&param, 0, sizeof(param));
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            param.mode = (strncmp(a, "ftp://", 6) == 0) ? 0 : 1;
            {
                const char *host = a;
                if (strncmp(a, "ftp://", 6) == 0) host = a + 6;
                else if (strncmp(a, "https://", 8) == 0) host = a + 8;
                else if (strncmp(a, "http://", 7) == 0) host = a + 7;
                strncpy(param.host, host, sizeof(param.host) - 1);
            }
            param.sc_fota_cb = FotaCb;
            sAPI_FotaServiceBegin(&param);
            snprintf(resp, sizeof(resp), "dang update firmware");
        }
        break;

    case 34: /* Xóa dữ liệu hành trình backup */
        if (isSet) {
            NASA_ClearBackup();
            snprintf(resp, sizeof(resp), "dang xoa du lieu hanh trinh");
        }
        break;

    case 35: /* Xóa toàn bộ */
        if (isSet) {
            NASA_ClearAllData();
            snprintf(resp, sizeof(resp), "dang xoa tat ca du lieu");
        }
        break;

    case 37: /* Forward SMS lấy SĐT SIM — gửi SMS tới A với nội dung B */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a)) || !NextToken(&save, b, sizeof(b))) {
                Reply(reply, ctx, src, "ERROR"); return;
            }
            /* Gửi SMS đơn giản; queue phản hồi có thể NULL nếu API cho phép */
            sAPI_SmsSendMsg(1, (UINT8 *)b, (UINT16)strlen(b), (UINT8 *)a, NULL);
            snprintf(resp, sizeof(resp), "%s", b);
        }
        break;

    case 40: /* Khóa cấu hình */
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            if (!CFG_CanChangeProtected(src, fromPhone) && strcmp(a, "open") == 0) {
                /* mở khóa cần SĐT auth khi đã khóa */
                if (!CFG_IsAuthPhone(fromPhone) && !(src && strcmp(src, "SERVER") == 0)) {
                    Deny(reply, ctx, src); return;
                }
            }
            if (strcmp(a, "close") == 0) CFG_SetLocked(1);
            else if (strcmp(a, "open") == 0) CFG_SetLocked(0);
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,40 thiet bi %s",
                 CFG_IsLocked() ? "da khoa cau hinh" : "da mo khoa cau hinh");
        break;

    case 41: /* Factory reset */
        if (isSet) {
            if (CFG_IsLocked()) {
                if (!CFG_IsAuthPhone(fromPhone) && !(src && strcmp(src, "SERVER") == 0)) {
                    Deny(reply, ctx, src); return;
                }
            }
            CFG_FactoryReset();
            snprintf(resp, sizeof(resp), "khoi phuc cai dat mac dinh ban dau");
        }
        break;

    default:
        snprintf(resp, sizeof(resp), "ERROR ma lenh khong ho tro");
        break;
    }

    if (resp[0]) Reply(reply, ctx, src, resp);
}
