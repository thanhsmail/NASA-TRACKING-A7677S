/**
 * @file    port_log.c
 * @brief   HAL_LOG, HAL_USBCOM: log qua sAPI_Debug + chep ra cong COM ao USB -- SIMCom A7677S OpenSDK
 *
 * Chi cac file trong port/ duoc goi sAPI_*. Ham SDK dung o day: SDK_API_LIST.md.
 */

#include "port_sdk.h"

/* ========================== HAL_LOG ======================================= */

static void UsbLogPush(const char *line); /* HAL_USBCOM, cuoi file */

void HAL_LOG_Print(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sAPI_Debug("%s", buf);
    UsbLogPush(buf); /* debug,1: chep them ra cong USB COM */
}

/* ========================== HAL_USBCOM ==================================== */

#define USBCOM_RX_BUF_SIZE 1024u
#define USBCOM_FLAG_RX     0x01u

static uint8_t           s_usbRxBuf[USBCOM_RX_BUF_SIZE];
static volatile uint32_t s_usbRxHead; /* callback ghi */
static volatile uint32_t s_usbRxTail; /* task doc */
static sFlagRef          s_usbRxFlag = NULL;
static sMutexRef         s_usbTxMutex = NULL;

/* Callback cua SDK: doc DUNG len byte nhu demo cus_usb_vcom.c (khong goi
 * sAPI_UsbVcomRead them khi da het du lieu), chep vao vong dem, bao task.
 * Day vong dem thi bo phan du (van phai doc de driver khong giu lai). */
static volatile uint32_t s_usbRxCbCount;
static volatile int      s_usbRxCbLastLen;
static volatile int      s_usbRxCbLastRead;

static void UsbComRxCb(int len, void *reserve)
{
    uint8_t tmp[128];
    int left = len, n, i, got = 0, total = 0;
    (void)reserve;

    s_usbRxCbCount++;
    s_usbRxCbLastLen = len;
    while (left > 0) {
        int want = (left > (int)sizeof(tmp)) ? (int)sizeof(tmp) : left;
        n = sAPI_UsbVcomRead(tmp, (unsigned long)want);
        if (n <= 0) break;
        if (n > want) n = want;
        left -= n;
        total += n;
        for (i = 0; i < n; i++) {
            uint32_t next = (s_usbRxHead + 1u) % USBCOM_RX_BUF_SIZE;
            if (next == s_usbRxTail) break;
            s_usbRxBuf[s_usbRxHead] = tmp[i];
            s_usbRxHead = next;
            got = 1;
        }
    }
    s_usbRxCbLastRead = total;
    if (got && s_usbRxFlag) sAPI_FlagSet(s_usbRxFlag, USBCOM_FLAG_RX, SC_FLAG_OR);
}

int HAL_USBCOM_Open(void)
{
    int rc;
    if (s_usbRxFlag == NULL && sAPI_FlagCreate(&s_usbRxFlag) != SC_SUCCESS) {
        s_usbRxFlag = NULL;
        return -1;
    }
    if (s_usbTxMutex == NULL && sAPI_MutexCreate(&s_usbTxMutex, SC_FIFO) != SC_SUCCESS) {
        s_usbTxMutex = NULL;
        return -1;
    }
    rc = sAPI_UsbVcomRegisterCallbackEX(UsbComRxCb, NULL);
    HAL_LOG_Print("[USBCOM] register callback rc=%d usb=%d", rc, HAL_OS_UsbConnected());
    return (rc == 0) ? 0 : -1;
}

int HAL_USBCOM_Read(uint8_t *buf, uint32_t len, uint32_t timeoutTicks)
{
    uint32_t n = 0;
    UINT32 flags = 0;

    if (!buf || len == 0 || !s_usbRxFlag) return 0;

    if (s_usbRxTail == s_usbRxHead) {
        UINT32 tout;
        if (timeoutTicks == HAL_OS_NO_WAIT) return 0;
        tout = (timeoutTicks == HAL_OS_WAIT_FOREVER) ? SC_SUSPEND : timeoutTicks;
        /* Co flag con set tu lan truoc thi tra ve ngay, vong lap ngoai doc lai */
        if (sAPI_FlagWait(s_usbRxFlag, USBCOM_FLAG_RX, SC_FLAG_OR_CLEAR, &flags, tout) != SC_SUCCESS)
            return 0;
    }
    while (n < len && s_usbRxTail != s_usbRxHead) {
        buf[n++] = s_usbRxBuf[s_usbRxTail];
        s_usbRxTail = (s_usbRxTail + 1u) % USBCOM_RX_BUF_SIZE;
    }
    return (int)n;
}

void HAL_USBCOM_GetStats(uint32_t *cbCount, int *lastLen, int *lastRead)
{
    if (cbCount)  *cbCount  = s_usbRxCbCount;
    if (lastLen)  *lastLen  = s_usbRxCbLastLen;
    if (lastRead) *lastRead = s_usbRxCbLastRead;
}

int HAL_USBCOM_AtSend(const char *line)
{
    char buf[260];
    int n;
    if (!line || !line[0]) return -1;
    n = snprintf(buf, sizeof(buf), "%s\r\n", line);
    if (n <= 0 || n >= (int)sizeof(buf)) return -1;
    return sAPI_AtSend((UINT8 *)buf, (UINT32)n) ? 0 : -1;
}

int HAL_USBCOM_Write(const uint8_t *data, uint32_t len)
{
    int rc;
    if (!data || len == 0) return 0;
    if (!HAL_OS_UsbConnected()) return -1;
    if (s_usbTxMutex) sAPI_MutexLock(s_usbTxMutex, SC_SUSPEND);
    rc = sAPI_UsbVcomWrite((unsigned char *)data, (unsigned long)len);
    if (s_usbTxMutex) sAPI_MutexUnLock(s_usbTxMutex);
    return (rc == USB_TX_SUCCEED) ? 0 : -1;
}

/* ---- Log ra USB COM (lenh debug,1) ----
 * HAL_LOG_Print chi chep dong log vao vong dem (co mutex, chi goi tu task;
 * cac ham ngat khong log). Task usb_log rut vong dem va ghi ra cong USB, nen
 * neu sAPI_UsbVcomWrite bi chan (PC chua mo cong) thi chi task nay doi, cac
 * task khac khong bi ket. Day vong dem thi bo dong moi va dem so dong mat. */

#define USBLOG_BUF_SIZE  4096u
#define USBLOG_FLAG_DATA 0x01u

static uint8_t           s_usbLogBuf[USBLOG_BUF_SIZE];
static volatile uint32_t s_usbLogHead;
static volatile uint32_t s_usbLogTail;
static volatile uint32_t s_usbLogDropped;
static volatile int      s_usbLogOn = 0;
static sMutexRef         s_usbLogMutex = NULL;
static sFlagRef          s_usbLogFlag = NULL;
static sTaskRef          s_usbLogTask = NULL;
static uint8_t           s_usbLogStack[1024 * 3];

static void UsbLogPush(const char *line)
{
    uint32_t len, freeSpace, i;

    if (!s_usbLogOn || !s_usbLogMutex || !line) return;
    len = (uint32_t)strlen(line);
    sAPI_MutexLock(s_usbLogMutex, SC_SUSPEND);
    freeSpace = (s_usbLogTail + USBLOG_BUF_SIZE - s_usbLogHead - 1u) % USBLOG_BUF_SIZE;
    if (len + 2u > freeSpace) {
        s_usbLogDropped++;
    } else {
        for (i = 0; i < len; i++) {
            s_usbLogBuf[s_usbLogHead] = (uint8_t)line[i];
            s_usbLogHead = (s_usbLogHead + 1u) % USBLOG_BUF_SIZE;
        }
        s_usbLogBuf[s_usbLogHead] = '\r';
        s_usbLogHead = (s_usbLogHead + 1u) % USBLOG_BUF_SIZE;
        s_usbLogBuf[s_usbLogHead] = '\n';
        s_usbLogHead = (s_usbLogHead + 1u) % USBLOG_BUF_SIZE;
    }
    sAPI_MutexUnLock(s_usbLogMutex);
    sAPI_FlagSet(s_usbLogFlag, USBLOG_FLAG_DATA, SC_FLAG_OR);
}

static void UsbLogTask(void *arg)
{
    uint8_t chunk[256];
    UINT32 flags;
    (void)arg;

    for (;;) {
        uint32_t n = 0, dropped;

        sAPI_FlagWait(s_usbLogFlag, USBLOG_FLAG_DATA, SC_FLAG_OR_CLEAR, &flags, SC_SUSPEND);
        for (;;) {
            sAPI_MutexLock(s_usbLogMutex, SC_SUSPEND);
            n = 0;
            while (n < sizeof(chunk) && s_usbLogTail != s_usbLogHead) {
                chunk[n++] = s_usbLogBuf[s_usbLogTail];
                s_usbLogTail = (s_usbLogTail + 1u) % USBLOG_BUF_SIZE;
            }
            dropped = s_usbLogDropped;
            s_usbLogDropped = 0;
            sAPI_MutexUnLock(s_usbLogMutex);

            if (n == 0) break;
            /* Rut USB / tat debug giua chung: bo du lieu, khong ghi */
            if (!s_usbLogOn || !HAL_OS_UsbConnected()) continue;
            if (dropped) {
                char msg[48];
                int m = snprintf(msg, sizeof(msg), "[USBLOG] mat %lu dong\r\n", (unsigned long)dropped);
                if (m > 0) HAL_USBCOM_Write((const uint8_t *)msg, (uint32_t)m);
            }
            HAL_USBCOM_Write(chunk, n);
        }
    }
}

int HAL_LOG_SetUsbMirror(int on)
{
    if (on) {
        if (s_usbLogMutex == NULL && sAPI_MutexCreate(&s_usbLogMutex, SC_FIFO) != SC_SUCCESS) {
            s_usbLogMutex = NULL;
            return -1;
        }
        if (s_usbLogFlag == NULL && sAPI_FlagCreate(&s_usbLogFlag) != SC_SUCCESS) {
            s_usbLogFlag = NULL;
            return -1;
        }
        if (s_usbLogTask == NULL &&
            sAPI_TaskCreate(&s_usbLogTask, s_usbLogStack, sizeof(s_usbLogStack), 100,
                            "usb_log", UsbLogTask, NULL) != SC_SUCCESS) {
            s_usbLogTask = NULL;
            return -1;
        }
    }
    s_usbLogOn = on ? 1 : 0;
    return 0;
}

int HAL_LOG_GetUsbMirror(void)
{
    return s_usbLogOn;
}
