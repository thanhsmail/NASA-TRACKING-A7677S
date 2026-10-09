/**
 * @file app_fota.c
 * @brief CẬP NHẬT FIRMWARE ỨNG DỤNG TỪ XA (FOTA) -- NASA Tracking
 *
 * Luồng xử lý:
 * 1. Lệnh "na,33,<URL>" (SMS / Server) gọi FOTA_Request() -- trả về ngay.
 * 2. Task FOTA tải customer_app.bin về phân vùng cập nhật (HTTP/HTTPS/FTP),
 *    thử lại tối đa FOTA_MAX_ATTEMPTS lần.
 * 3. Kiểm CRC gói vừa tải. Đạt → reset, bootloader nạp ứng dụng mới.
 *    Không đạt → giữ nguyên firmware đang chạy, báo lỗi.
 *
 * Trong lúc tải, task nasa_reporter vẫn chạy bình thường (vẫn nuôi watchdog).
 *
 * Lưu ý: File này KHÔNG #include simcom_api.h trực tiếp.
 * Mọi tương tác phần cứng đi qua lớp HAL layer.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_sms.h"
#include "hal/hal_fota.h"
#include "app_config.h"
#include "app_fota.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    FOTA_STATE_IDLE = 0,
    FOTA_STATE_PENDING,
    FOTA_STATE_DOWNLOADING,
    FOTA_STATE_REBOOTING
} FotaState_t;

static volatile FotaState_t s_state = FOTA_STATE_IDLE;
static char s_url[FOTA_URL_MAX_LEN];
static char s_notifyPhone[20];

static HalTaskRef_t s_fotaTaskRef = NULL;
static uint8_t      s_fotaTaskStack[1024 * 10];

/** Báo kết quả cho người gửi lệnh qua SMS (nếu lệnh đến từ SMS) */
static void Notify(const char *msg)
{
    HAL_LOG("[FOTA] %s", msg);
    if (s_notifyPhone[0]) {
        HAL_SMS_Send(s_notifyPhone, msg);
    }
}

static void sTask_Fota(void *argv)
{
    char msg[64];
    (void)argv;

    for (;;) {
        int attempt;
        int ret = -1;
        uint32_t size = 0;

        if (s_state != FOTA_STATE_PENDING) {
            HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
            continue;
        }
        s_state = FOTA_STATE_DOWNLOADING;

        for (attempt = 1; attempt <= FOTA_MAX_ATTEMPTS; attempt++) {
            HAL_LOG("[FOTA] download %d/%d: %s", attempt, FOTA_MAX_ATTEMPTS, s_url);
            ret = HAL_FOTA_AppDownload(s_url, FOTA_RECV_TIMEOUT_MS);
            if (ret != 0) {
                HAL_LOG("[FOTA] download fail ret=%d", ret);
            } else {
                ret = HAL_FOTA_AppPackageVerify(&size);
                if (ret == 0) break;
                HAL_LOG("[FOTA] package CRC fail ret=%d size=%u", ret, (unsigned)size);
                ret = FOTA_ERR_CRC;
            }
            if (attempt < FOTA_MAX_ATTEMPTS) {
                HAL_OS_TaskSleep(FOTA_RETRY_DELAY_SEC * HAL_TICKS_PER_SEC);
            }
        }

        if (ret != 0) {
            snprintf(msg, sizeof(msg), "update firmware loi: %d", ret);
            Notify(msg);
            s_state = FOTA_STATE_IDLE;
            continue;
        }

        s_state = FOTA_STATE_REBOOTING;
        snprintf(msg, sizeof(msg), "update firmware ok (%u byte), khoi dong lai",
                 (unsigned)size);
        Notify(msg);
        HAL_OS_TaskSleep(HAL_TICKS_PER_SEC * 5);
        HAL_OS_SysReset();
    }
}

int FOTA_IsBusy(void)
{
    return (s_state != FOTA_STATE_IDLE) ? 1 : 0;
}

int FOTA_Request(const char *url, const char *notifyPhone)
{
    const char *p;
    const char *prefix = "";

    if (!url) return FOTA_REQ_BAD_URL;
    while (*url == ' ' || *url == '\t') url++;
    if (url[0] == '\0') return FOTA_REQ_BAD_URL;
    for (p = url; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') return FOTA_REQ_BAD_URL;
    }

    if (strstr(url, "://") == NULL) {
        prefix = "http://";
    } else if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0 &&
               strncmp(url, "ftp://", 6) != 0 && strncmp(url, "ftps://", 7) != 0) {
        return FOTA_REQ_BAD_URL;
    }
    if (strlen(prefix) + strlen(url) >= sizeof(s_url)) return FOTA_REQ_BAD_URL;

    if (s_state != FOTA_STATE_IDLE) return FOTA_REQ_BUSY;

    if (s_fotaTaskRef == NULL) {
        if (HAL_OS_TaskCreate(&s_fotaTaskRef, s_fotaTaskStack, sizeof(s_fotaTaskStack),
                              20, "fota_task", sTask_Fota, NULL) != 0) {
            s_fotaTaskRef = NULL;
            HAL_LOG("[FOTA] create task fail");
            return FOTA_REQ_NO_TASK;
        }
    }

    snprintf(s_url, sizeof(s_url), "%s%s", prefix, url);
    s_notifyPhone[0] = '\0';
    if (notifyPhone && notifyPhone[0]) {
        strncpy(s_notifyPhone, notifyPhone, sizeof(s_notifyPhone) - 1);
        s_notifyPhone[sizeof(s_notifyPhone) - 1] = '\0';
    }
    s_state = FOTA_STATE_PENDING;
    return FOTA_REQ_OK;
}
