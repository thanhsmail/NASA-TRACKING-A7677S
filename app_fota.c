/**
 * @file app_fota.c
 * @brief CẬP NHẬT FIRMWARE TỪ XA (FOTA) -- NASA Tracking
 *
 * Luồng xử lý:
 * 1. Lệnh "na,33,<URL>" (SMS / Server) gọi FOTA_Request() -- trả về ngay.
 * 2. Task FOTA chọn loại cập nhật theo tên file trong URL:
 *    - customer_app.bin → cập nhật ỨNG DỤNG: tải về phân vùng cập nhật
 *      (HTTP/HTTPS/FTP), kiểm CRC. Đạt → reset, bootloader nạp ứng dụng mới.
 *    - tên khác (vd system_patch.bin) → cập nhật HỆ THỐNG: SDK tải gói vi sai
 *      (adiff), báo tiến trình qua callback. Đạt 100% → reset để nạp.
 *    Thử lại tối đa FOTA_MAX_ATTEMPTS lần.
 * 3. Lỗi → giữ nguyên firmware đang chạy, báo lỗi.
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

/* Trạng thái FOTA hệ thống do callback của SDK cập nhật */
static volatile int      s_sysStatus = 0;
static volatile uint32_t s_sysCbCount = 0;

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

/** @return 1 nếu URL trỏ tới gói ứng dụng (customer_app.bin), 0 nếu là gói hệ thống */
static int IsAppPackageUrl(const char *url)
{
    size_t ulen = strlen(url);
    size_t nlen = strlen(FOTA_APP_FILE_NAME);

    return (ulen > nlen && url[ulen - nlen - 1] == '/' &&
            strcmp(url + ulen - nlen, FOTA_APP_FILE_NAME) == 0) ? 1 : 0;
}

/** Tải customer_app.bin + kiểm CRC. @return 0 nếu gói sẵn sàng để nạp */
static int DoAppUpdate(uint32_t *size)
{
    int attempt;
    int ret = -1;

    for (attempt = 1; attempt <= FOTA_MAX_ATTEMPTS; attempt++) {
        HAL_LOG("[FOTA] download %d/%d: %s", attempt, FOTA_MAX_ATTEMPTS, s_url);
        ret = HAL_FOTA_AppDownload(s_url, FOTA_RECV_TIMEOUT_MS);
        if (ret != 0) {
            HAL_LOG("[FOTA] download fail ret=%d", ret);
        } else {
            ret = HAL_FOTA_AppPackageVerify(size);
            if (ret == 0) break;
            HAL_LOG("[FOTA] package CRC fail ret=%d size=%u", ret, (unsigned)*size);
            ret = FOTA_ERR_CRC;
        }
        if (attempt < FOTA_MAX_ATTEMPTS) {
            HAL_OS_TaskSleep(FOTA_RETRY_DELAY_SEC * HAL_TICKS_PER_SEC);
        }
    }
    return ret;
}

/* Callback chạy trong task của SDK: chỉ ghi nhận, xử lý ở sTask_Fota */
static int SysFotaCb(int status)
{
    s_sysStatus = status;
    s_sysCbCount++;
    return 0;
}

/** Tải gói vi sai hệ thống (system_patch.bin). @return 0 nếu tải + kiểm tra xong */
static int DoSysUpdate(void)
{
    int attempt;
    int ret = -1;

    for (attempt = 1; attempt <= FOTA_MAX_ATTEMPTS; attempt++) {
        uint32_t lastCount = 0;
        int lastPct = -1;
        int idleSec = 0;

        s_sysStatus = 0;
        s_sysCbCount = 0;
        HAL_LOG("[FOTA] system download %d/%d: %s", attempt, FOTA_MAX_ATTEMPTS, s_url);
        ret = HAL_FOTA_SysStart(s_url, SysFotaCb);
        if (ret != 0) {
            HAL_LOG("[FOTA] system start fail ret=%d", ret);
        } else {
            for (;;) {
                int st;

                HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
                st = s_sysStatus;
                if (s_sysCbCount != lastCount) {
                    lastCount = s_sysCbCount;
                    idleSec = 0;
                    if (st == 100) return 0;
                    if (st < 0 || st > 100) {
                        HAL_LOG("[FOTA] system download fail status=%d", st);
                        ret = FOTA_ERR_SYS_FAIL;
                        break;
                    }
                    if (st != lastPct) {
                        lastPct = st;
                        HAL_LOG("[FOTA] system download %d%%", st);
                    }
                } else if (++idleSec >= FOTA_SYS_STALL_SEC) {
                    /* Phiên của SDK có thể còn treo → không thử lại chồng lên */
                    HAL_LOG("[FOTA] system download stalled (last status=%d)", st);
                    return FOTA_ERR_SYS_TIMEOUT;
                }
            }
        }
        if (attempt < FOTA_MAX_ATTEMPTS) {
            HAL_OS_TaskSleep(FOTA_RETRY_DELAY_SEC * HAL_TICKS_PER_SEC);
        }
    }
    return ret;
}

static void sTask_Fota(void *argv)
{
    char msg[64];
    (void)argv;

    for (;;) {
        int ret;
        int isApp;
        uint32_t size = 0;

        if (s_state != FOTA_STATE_PENDING) {
            HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
            continue;
        }
        s_state = FOTA_STATE_DOWNLOADING;

        isApp = IsAppPackageUrl(s_url);
        ret = isApp ? DoAppUpdate(&size) : DoSysUpdate();

        if (ret != 0) {
            snprintf(msg, sizeof(msg), "update firmware loi: %d", ret);
            Notify(msg);
            s_state = FOTA_STATE_IDLE;
            continue;
        }

        s_state = FOTA_STATE_REBOOTING;
        if (isApp) {
            snprintf(msg, sizeof(msg), "update firmware ok (%u byte), khoi dong lai",
                     (unsigned)size);
        } else {
            snprintf(msg, sizeof(msg), "tai firmware he thong ok, khoi dong lai de nap");
        }
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
