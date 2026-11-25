/**
 * @file app_com.c
 * @brief CẤU HÌNH QUA CỔNG COM ẢO USB
 *
 * Cắm cáp USB, mở cổng "AT Port" của module bằng terminal (PuTTY, Hercules...),
 * gõ lệnh giống SMS rồi Enter, ví dụ "sa,29" hoặc "na,1,103.27.60.10,9999".
 * Dòng lệnh chuyển cho CMD_Execute() với nguồn "COM": không cần số điện thoại
 * và bỏ qua khóa cấu hình (lệnh 40), vì phải cắm dây trực tiếp vào thiết bị.
 *
 * Khi đã đăng ký, cổng AT USB chuyển dữ liệu cho firmware; dòng bắt đầu bằng
 * "AT" được chuyển tiếp cho bộ xử lý AT của module nên lệnh AT vẫn dùng được.
 * Module không ngủ khi đang cắm USB nên luôn cấu hình được.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_usbcom.h"
#include "app_config.h"
#include "app_cmd.h"
#include "app_com.h"
#include <string.h>

#if COM_CFG_ENABLE

#define COM_LINE_MAX 256

static uint8_t      s_comTaskStack[1024 * 8]; /* CMD_Execute dùng ~2 KB stack, như SmsRecvTask */
static HalTaskRef_t s_comTaskRef = NULL;
static int          s_rxLogged = 0;

static void ComWriteStr(const char *s)
{
    if (s && s[0] && HAL_USBCOM_Write((const uint8_t *)s, (uint32_t)strlen(s)) != 0)
        HAL_LOG("[COM] write fail (usb=%d)", HAL_OS_UsbConnected());
}

static void ComReply(const char *reply, void *ctx)
{
    (void)ctx;
    if (!HAL_LOG_GetUsbMirror()) HAL_LOG("[COM] reply: %s", reply); /* debug,1: khỏi in 2 lần */
    ComWriteStr(reply);
    ComWriteStr("\r\n");
}

static void ComRunLine(char *line)
{
    char *p = line;
    char *end;

    while (*p == ' ' || *p == '\t') p++;
    end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    if (*p == '\0') return;

    HAL_LOG("[COM] cmd: %s", p);
    if ((p[0] == 'A' || p[0] == 'a') && (p[1] == 'T' || p[1] == 't')) {
        if (HAL_USBCOM_AtSend(p) != 0) ComWriteStr("ERROR\r\n");
        return;
    }
    if (strncmp(p, "na,", 3) != 0 && strncmp(p, "sa,", 3) != 0 &&
        strcmp(p, "reset") != 0 && strcmp(p, "reset#") != 0 &&
        strcmp(p, "debug") != 0 && strncmp(p, "debug,", 6) != 0 &&
        strncmp(p, "update,", 7) != 0) {
        ComWriteStr("ERROR lenh phai bat dau bang na, hoac sa,\r\n");
        return;
    }
    CMD_Execute(p, "COM", NULL, ComReply, NULL);
}

static void ComTask(void *arg)
{
    char line[COM_LINE_MAX];
    uint32_t len = 0;
    int overflow = 0;
    uint8_t rx[64];
    int n, i;
    (void)arg;

    while (HAL_USBCOM_Open() != 0) {
        HAL_LOG("[COM] open USB COM fail, retry in 10s");
        HAL_OS_TaskSleep(10 * HAL_TICKS_PER_SEC);
    }
    ComWriteStr("\r\nNASA COM ready: go na,... / sa,... roi Enter\r\n");

    for (;;) {
        n = HAL_USBCOM_Read(rx, sizeof(rx), HAL_OS_WAIT_FOREVER);
        if (!s_rxLogged && n > 0) { /* chỉ log lần đầu: mỗi phím một dòng log thì rối khi debug,1 */
            uint32_t cb; int cbLen, cbRead;
            HAL_USBCOM_GetStats(&cb, &cbLen, &cbRead);
            HAL_LOG("[COM] first rx %d byte (cb=%lu len=%d read=%d)", n,
                    (unsigned long)cb, cbLen, cbRead);
            s_rxLogged = 1;
        }
        for (i = 0; i < n; i++) {
            uint8_t c = rx[i];

            if (c == '\r' || c == '\n') {
#if COM_ECHO
                ComWriteStr("\r\n");
#endif
                if (overflow) {
                    ComWriteStr("ERROR lenh qua dai\r\n");
                } else if (len > 0) {
                    line[len] = '\0';
                    ComRunLine(line);
                }
                len = 0;
                overflow = 0;
            } else if (c == 0x08 || c == 0x7F) { /* Backspace */
                if (len > 0) {
                    len--;
#if COM_ECHO
                    ComWriteStr("\b \b");
#endif
                }
            } else if (c >= 0x20 && c < 0x7F) {
                if (len < sizeof(line) - 1) {
                    line[len++] = (char)c;
#if COM_ECHO
                    HAL_USBCOM_Write(&c, 1);
#endif
                } else {
                    overflow = 1;
                }
            }
        }
    }
}

void COM_TaskStart(void)
{
    if (s_comTaskRef != NULL) return;
    if (HAL_OS_TaskCreate(&s_comTaskRef, s_comTaskStack, sizeof(s_comTaskStack),
                          18, "com_cmd", ComTask, NULL) != 0) {
        s_comTaskRef = NULL;
        HAL_LOG("[COM] create task fail");
    } else {
        HAL_LOG("[COM] create task OK");
    }
}

#else

void COM_TaskStart(void) {}

#endif /* COM_CFG_ENABLE */
