/**
 * @file app_cmd.c
 * @brief BỘ PHÂN TÍCH VÀ THỰC THI LỆNH (AT / SMS / SERVER COMMAND PARSER)
 *
 * File này chịu trách nhiệm:
 * 1. Phân tích các câu lệnh dạng "na,code,param..." (Lệnh cài đặt - SET)
 *    và "sa,code" (Lệnh truy vấn - GET) nhận từ SMS hoặc Server.
 * 2. Phản hồi kết quả thực thi lệnh đóng gói theo chuẩn giao thức !NASA,13,... lên Server
 *    hoặc nhắn tin trả lời qua SMS.
 * 3. Kiểm tra phân quyền bảo mật (Phone 1, Phone 2, Khóa cấu hình).
 *
 * Lưu ý: File này KHÔNG #include simcom_api.h trực tiếp.
 * Mọi tương tác phần cứng đi qua lớp HAL layer.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_gpio.h"
#include "hal/hal_net.h"
#include "hal/hal_gnss.h"
#include "hal/hal_sms.h"
#include "app_config.h"
#include "app_cfg.h"
#include "app_cmd.h"
#include "app_gps.h"
#include "app_network.h"
#include "app_backup.h"
#include "app_utils.h"
#include "app_nasa.h"
#include "app_fota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *strtok_r(char *str, const char *delim, char **saveptr);

/**
 * Khởi động FOTA từ phần còn lại của câu lệnh (URL có thể chứa dấu phẩy).
 * @return 1 nếu đã nhận yêu cầu; 0 nếu từ chối (lý do ghi vào resp)
 */
static int StartFota(const char *url, const char *fromPhone, char *resp, int respsz)
{
    int ret = FOTA_Request(url, fromPhone);

    HAL_LOG("[FOTA] Update requested via URL: %s ret=%d", url ? url : "", ret);
    if (ret == FOTA_REQ_OK) return 1;
    snprintf(resp, respsz, "%s",
             (ret == FOTA_REQ_BUSY)    ? "dang update firmware, vui long cho" :
             (ret == FOTA_REQ_BAD_URL) ? "ERROR url khong hop le" : "ERROR");
    return 0;
}

/* Mutex bảo vệ luồng phản hồi lệnh */
static HalMutexRef_t s_cmdReplyMutex = NULL;

/**
 * @brief Trả lời kết quả thực thi lệnh tới kênh nguồn yêu cầu (SMS hoặc Server TCP)
 * @param reply Function pointer gửi phản hồi
 * @param ctx Context gửi tin (ví dụ SĐT người gửi SMS hoặc Socket Server)
 * @param src Nguồn lệnh ("SERVER", "SMS", ...)
 * @param msg Nội dung phản hồi
 */
static void Reply(CmdReplyFn reply, void *ctx, const char *src, const char *msg)
{
    /* Mảng cục bộ trên stack đảm bảo thread-safety */
    char payload[384];
    char out[420];
    uint32_t cs;
    const char *content;
    const char *stripped;
    char dateTime[40] = {0};
    uint32_t msgId;

    if (!reply || !msg || !msg[0]) return;

    if (s_cmdReplyMutex == NULL) {
        HAL_OS_MutexCreate(&s_cmdReplyMutex);
    }
    HAL_OS_MutexLock(s_cmdReplyMutex);

    /* SMS / Local: Gửi nguyên văn trực tiếp cho người dùng */
    if (!src || strcmp(src, "SERVER") != 0) {
        reply(msg, ctx);
        HAL_OS_MutexUnlock(s_cmdReplyMutex);
        return;
    }

    /* Nếu msg đã được đóng gói sẵn dạng !NASA,...*checksum thì gửi thẳng */
    if (strncmp(msg, "!NASA", 5) == 0 && strchr(msg, '*') != NULL) {
        reply(msg, ctx);
        HAL_OS_MutexUnlock(s_cmdReplyMutex);
        return;
    }

    content = msg;
    if (strncmp(msg, "!NASA,13,", 9) == 0)
        content = msg + 9;

    /* Loại bỏ tiền tố "sa,X " (nếu có) khi gửi phản hồi lên Server */
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

    /* Đóng gói bản tin phản hồi lệnh Server theo chuẩn: !NASA,13,<msgId>,<dateTime>,<stripped>,*checksum */
    snprintf(payload, sizeof(payload), "!NASA,13,%lu,%s,%s,", (unsigned long)msgId, dateTime, stripped);
    cs = Buffer_GetChecksum((const uint8_t *)payload, (uint32_t)strlen(payload));
    snprintf(out, sizeof(out), "%s*%lu", payload, (unsigned long)cs);
    reply(out, ctx);

    HAL_OS_MutexUnlock(s_cmdReplyMutex);
}

/** Từ chối thực thi khi số điện thoại không có quyền hoặc thiết bị đang khóa cấu hình */
static void Deny(CmdReplyFn reply, void *ctx, const char *src)
{
    Reply(reply, ctx, src, "Ban khong co quyen thuc hien chuc nang nay");
}

/** Trích xuất tham số kế tiếp từ chuỗi câu lệnh phân cách bằng dấu phẩy */
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

/**
 * @brief HÀM XỬ LÝ CHÍNH - Phân tích và thực thi câu lệnh từ SMS / Server
 * @param body Nội dung câu lệnh nhận được (vd: "na,1,103.27.60.10,9999" hoặc "sa,29")
 * @param src Nguồn nhận lệnh ("SMS" hoặc "SERVER")
 * @param fromPhone Số điện thoại gửi lệnh (nếu nhận qua SMS)
 * @param reply Hàm phản hồi kết quả
 * @param ctx Tham số mở rộng cho hàm reply
 */
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

    /* Bỏ qua các bản tin giao thức truyền dữ liệu định kỳ !NASA,... (xử lý ở module NASA) */
    if (strncmp(body, "!NASA", 5) == 0) return;

    strncpy(buf, body, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tag = strtok_r(buf, ",", &save);
    if (!tag) return;

    /* Hỗ trợ lệnh reset cũ để tương thích (Legacy support) */
    if (strcmp(tag, "reset#") == 0 || (strcmp(tag, "reset") == 0)) {
        Reply(reply, ctx, src, "OK");
        HAL_OS_TaskSleep(200);
        HAL_OS_SysReset();
        return;
    }

    /* Lệnh cập nhật phần mềm FOTA cũ */
    if (strcmp(tag, "update") == 0) {
        if (!CFG_CanChangeProtected(src, fromPhone)) { Deny(reply, ctx, src); return; }
        if (StartFota(save, fromPhone, resp, sizeof(resp)))
            snprintf(resp, sizeof(resp), "Update Firmware ok");
        Reply(reply, ctx, src, resp);
        return;
    }

    /* Chỉ chấp nhận tiền tố "na" (Cài đặt - SET) hoặc "sa" (Truy vấn - GET) */
    if (strcmp(tag, "na") != 0 && strcmp(tag, "sa") != 0) {
        HAL_LOG("[CMD/%s] ignore unknown tag: %s", src ? src : "?", tag);
        return;
    }

    isSet = (strcmp(tag, "na") == 0);
    codeStr = strtok_r(NULL, ",", &save);
    if (!codeStr) return;
    code = atoi(codeStr);
    resp[0] = '\0';

    HAL_LOG("[CMD/%s] %s code=%d raw=%s", src ? src : "?", isSet ? "SET" : "GET", code, body);

    /* BAN PHÂN PHỐI MÃ LỆNH (COMMAND CODE DISPATCHER) */
    switch (code) {

    /* --- Mã 1: Cài đặt / Đọc IP & Port Server --- */
    case 1:
        if (isSet) {
            if (!CFG_CanChangeProtected(src, fromPhone)) { Deny(reply, ctx, src); return; }
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            if (!NextToken(&save, b, sizeof(b))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetServer(a, atoi(b));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,1 ip/port: %s / %d", CFG_GetServerHost(), CFG_GetServerPort());
        break;

    /* --- Mã 2: Điều khiển Ngõ ra D_OUT (Rơ-le cắt nhiên liệu/điện) --- */
    case 2:
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
            HAL_GPIO_Write(GPIO_DOUT, CFG_GetDout() ? 1 : 0);
        }
        snprintf(resp, sizeof(resp), "sa,2 dieu khien ra: %s", CFG_GetDout() ? "bat" : "tat");
        break;
    }

    /* --- Mã 4: Cài đặt / Đọc Chu kỳ phát bản tin (Chạy / Dừng) --- */
    case 4:
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

    /* --- Mã 8: Cài đặt / Đọc Chế độ phát hiện ACC (Dây / Vận tốc) --- */
    case 8:
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

    /* --- Mã 9: Lệnh Reset Khởi động lại Thiết bị --- */
    case 9:
        if (isSet) {
            snprintf(resp, sizeof(resp), "reset ok");
            Reply(reply, ctx, src, resp);
            HAL_OS_TaskSleep(HAL_TICKS_PER_SEC * 5);
            HAL_OS_SysReset();
            return;
        }
        break;

    /* --- Mã 10: Cài đặt / Đọc Ngưỡng Cảnh báo Quá tốc độ (km/h) --- */
    case 10:
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetSpeedThresh(atoi(a));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,10 nguong van toc: %d km/h", CFG_GetSpeedThresh());
        break;

    /* --- Mã 11: Cài đặt / Đọc Chế độ Im lặng (Silent Mode) --- */
    case 11:
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetSilentMode(atoi(a));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,11 trang thai im lang: %s",
                 CFG_GetSilentMode() ? "bat" : "tat");
        break;

    /* --- Mã 12: Yêu cầu Phát lại Dữ liệu Hành trình Cũ từ Flash Backup --- */
    case 12:
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a)) || !NextToken(&save, b, sizeof(b))) {
                Reply(reply, ctx, src, "ERROR"); return;
            }
            NASA_RequestReplay(a, b);
            snprintf(resp, sizeof(resp), "bat dau truyen lai du lieu cu");
        }
        break;

    /* --- Mã 14: Đăng nhập / Đăng xuất Lái xe (Driver Login/Logout Toggle) --- */
    case 14:
        if (isSet) {
            if (CFG_IsDriverLoggedIn()) {
                NASA_DriverLogout();
                snprintf(resp, sizeof(resp), "sa,14 dang xuat lai xe: %s / %s",
                         CFG_GetDriverName(), CFG_GetDriverLicense());
            } else {
                NASA_DriverLogin();
                snprintf(resp, sizeof(resp), "sa,14 dang nhap lai xe: %s / %s",
                         CFG_GetDriverName(), CFG_GetDriverLicense());
            }
        } else {
            snprintf(resp, sizeof(resp), "sa,14 trang thai lai xe: %s / %s / %s",
                     CFG_GetDriverName(), CFG_GetDriverLicense(),
                     CFG_IsDriverLoggedIn() ? "login" : "logout");
        }
        break;

    /* --- Mã 17 & 18: Cài đặt / Đọc Số điện thoại Chủ xe (Phone 1 & Phone 2) --- */
    case 17:
    case 18:
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

    /* --- Mã 25: Cài đặt / Đọc Thông tin Tên Lái xe & Số GPLX --- */
    case 25:
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            if (!NextToken(&save, b, sizeof(b))) { Reply(reply, ctx, src, "ERROR"); return; }
            if (CFG_IsDriverLoggedIn()) {
                int same = (strcmp(a, CFG_GetDriverName()) == 0 &&
                            strcmp(b, CFG_GetDriverLicense()) == 0);
                if (!same) {
                    NASA_DriverLogout();
                    CFG_SetDriver(a, b, 0);
                    NASA_DriverLogin();
                } else {
                    CFG_SetDriver(a, b, 1);
                }
            } else {
                CFG_SetDriver(a, b, 0);
                NASA_DriverLogin();
            }
        }
        snprintf(resp, sizeof(resp), "sa,25 lai xe: %s / %s / %s",
                 CFG_GetDriverName(), CFG_GetDriverLicense(),
                 CFG_IsDriverLoggedIn() ? "login" : "logout");
        break;

    /* --- Mã 26: Cài đặt / Đọc Thời gian xác nhận Dừng/Đỗ xe (giây) --- */
    case 26:
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            CFG_SetParkConfirmSec(atoi(a));
            CFG_Save();
        }
        snprintf(resp, sizeof(resp), "sa,26 thoi gian ghi nhan dung/do: %d giay",
                 CFG_GetParkConfirmSec());
        break;

    /* --- Mã 27: Truy vấn Tọa độ GPS hiện tại (Trả về Google Maps Link) --- */
    case 27:
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

    /* --- Mã 29: Truy vấn Thông tin Thiết bị tổng hợp (IMEI, ICCID, CSQ, Vệ tinh, Server, ACC) --- */
    case 29:
        {
            char imei[32] = {0};
            char iccid[32] = {0};
            uint8_t csq = 0;
            GpsSnapshot_t gps = {0};
            int acc = 0;
            const char *conn_str = "";

            HAL_NET_GetImei(imei, sizeof(imei));
            if (HAL_NET_GetIccid(iccid, sizeof(iccid)) != 0) {
                strncpy(iccid, "UNKNOWN_SIM", sizeof(iccid) - 1);
            }
            HAL_NET_GetCsq(&csq);
            GPS_Snapshot(&gps);
            acc = (HAL_GPIO_Read(GPIO_ACC_IN) == 0) ? 1 : 0;
            conn_str = (Network_IsConnected() && NASA_IsSessionActive()) ? "da ket noi" : "chua ket noi";

            snprintf(resp, sizeof(resp),
                     "NASA4G %s %s GSM: %u GPS: %d %s %d %s %d",
                     imei, iccid, (unsigned)csq, gps.satellites,
                     CFG_GetServerHost(), CFG_GetServerPort(),
                     conn_str, acc);
        }
        break;

    /* --- Mã 32: Cài đặt / Đọc Lịch Tự động Reset Thiết bị (Số ngày, Giờ reset) --- */
    case 32:
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

    /* --- Mã 33: Lệnh Nâng cấp Phần mềm Từ xa (FOTA qua URL) --- */
    case 33:
        if (isSet) {
            if (!CFG_CanChangeProtected(src, fromPhone)) { Deny(reply, ctx, src); return; }
            if (StartFota(save, fromPhone, resp, sizeof(resp)))
                snprintf(resp, sizeof(resp), "dang update firmware");
        } else {
            snprintf(resp, sizeof(resp), "sa,33 firmware: %s%s", NASA_FW_CODE,
                     FOTA_IsBusy() ? " (dang update)" : "");
        }
        break;

    /* --- Mã 34: Xóa Bộ nhớ Đệm Hành trình Backup trong Flash --- */
    case 34:
        if (isSet) {
            NASA_ClearBackup();
            snprintf(resp, sizeof(resp), "dang xoa du lieu hanh trinh");
        }
        break;

    /* --- Mã 35: Xóa Toàn bộ Dữ liệu Cấu hình & Bộ nhớ Đệm Flash --- */
    case 35:
        if (isSet) {
            NASA_ClearAllData();
            snprintf(resp, sizeof(resp), "dang xoa tat ca du lieu");
        }
        break;

    /* --- Mã 37: Lệnh Chuyển tiếp SMS (Dùng để kiểm tra SĐT SIM / nạp tiền) --- */
    case 37:
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a)) || !NextToken(&save, b, sizeof(b))) {
                Reply(reply, ctx, src, "ERROR"); return;
            }
            HAL_SMS_Send(a, b);
            snprintf(resp, sizeof(resp), "%s", b);
        }
        break;

    /* --- Mã 40: Lệnh Khóa / Mở khóa Cấu hình Bảo vệ Thiết bị --- */
    case 40:
        if (isSet) {
            if (!NextToken(&save, a, sizeof(a))) { Reply(reply, ctx, src, "ERROR"); return; }
            if (!CFG_CanChangeProtected(src, fromPhone) && strcmp(a, "open") == 0) {
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

    /* --- Mã 41: Khôi phục Cài đặt Mặc định Ban đầu (Factory Reset) --- */
    case 41:
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

    /* --- Mã lệnh không hợp lệ --- */
    default:
        snprintf(resp, sizeof(resp), "ERROR ma lenh khong ho tro");
        break;
    }

    if (resp[0]) Reply(reply, ctx, src, resp);
}

