/**
 * @file    hal_impl_simcom.c
 * @brief   HAL Implementation -- SIMCom A7677S OpenSDK
 *
 * Day la FILE DUY NHAT duoc phep #include simcom_api.h.
 * Moi module app_*.c chi goi qua hal/ -- khong import SDK truc tiep.
 */

/* ========================== INCLUDES ====================================== */
#include "simcom_api.h"
#include "simcom_common.h"
#include "simcom_tcpip.h"
#include "simcom_gpio.h"
#include "simcom_gps.h"
#include "sc_spi.h"

#include "hal_log.h"
#include "hal_os.h"
#include "hal_gpio.h"
#include "hal_net.h"
#include "hal_gnss.h"
#include "hal_spi.h"
#include "hal_fs.h"
#include "hal_sms.h"
#include "hal_adc.h"
#include "hal_fota.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef sAPI_htons
#define sAPI_htons(n) ((uint16_t)((((uint16_t)(n) & 0xFF00) >> 8) | (((uint16_t)(n) & 0x00FF) << 8)))
#endif

/* ========================== HAL_LOG ======================================= */

void HAL_LOG_Print(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sAPI_Debug("%s", buf);
}

/* ========================== HAL_GPIO ====================================== */

void HAL_GPIO_Config(int pin, HalGpioDir_t dir, HalGpioPull_t pull)
{
    SC_GPIOConfiguration cfg;
    cfg.pinDir  = (dir == HAL_GPIO_OUT) ? SC_GPIO_OUT_PIN : SC_GPIO_IN_PIN;
    cfg.initLv  = (dir == HAL_GPIO_OUT) ? 0 : 1;
    cfg.pinPull = (pull == HAL_GPIO_PULL_UP)   ? SC_GPIO_PULLUP_ENABLE :
                  (pull == HAL_GPIO_PULL_DOWN) ? SC_GPIO_PULLDN_ENABLE :
                                                 SC_GPIO_PULL_DISABLE;
    cfg.pinEd   = SC_GPIO_NO_EDGE;
    cfg.isr     = NULL;
    cfg.wu      = NULL;
    {
        SC_GPIOReturnCode rc = sAPI_GpioConfig(pin, cfg);
        if (rc != SC_GPIORC_OK) {
            HAL_LOG_Print("[GPIO] config pin %d fail rc=%d", pin, (int)rc);
        }
    }
}

void HAL_GPIO_Write(int pin, int level)
{
    sAPI_GpioSetValue(pin, level);
}

int HAL_GPIO_Read(int pin)
{
    /* SDK tra ve muc 0/1 hoac ma loi am (SC_GPIORC_INVALID_*): khong de ma loi
     * bi hieu nham thanh muc HIGH */
    int rc = (int)sAPI_GpioGetValue(pin);
    if (rc == SC_GPIORC_LOW || rc == SC_GPIORC_HIGH) return rc;
    return -1;
}

/* ========================== HAL_OS ======================================== */

int HAL_OS_TaskCreate(HalTaskRef_t *ref, void *stack, uint32_t stackSize,
                      int priority, const char *name,
                      void (*fn)(void *), void *arg)
{
    SC_STATUS st = sAPI_TaskCreate((sTaskRef *)ref, (UINT8 *)stack,
                                   stackSize, (UINT8)priority,
                                   (char *)name, fn, arg);
    return (st == SC_SUCCESS) ? 0 : -1;
}

void HAL_OS_TaskSleep(uint32_t ticks)
{
    sAPI_TaskSleep(ticks);
}

void HAL_OS_SysReset(void)
{
    sAPI_SysReset();
}

uint32_t HAL_OS_GetTick(void)
{
    return (uint32_t)sAPI_GetTicks();
}

HalPowerUpReason_t HAL_OS_GetPowerUpReason(void)
{
    POWER_UP_REASON r = sAPI_GetPowerUpEvent();
    if (r == POWER_UP_SOFTWARE_RESET) return HAL_POWER_UP_SOFTWARE_RESET;
    if (r == POWER_UP_RESET_KEY)       return HAL_POWER_UP_RESET_KEY;
    if (r == POWER_UP_POWER_KEY)       return HAL_POWER_UP_POWER_KEY;
    return HAL_POWER_UP_NORMAL;
}

int HAL_OS_MutexCreate(HalMutexRef_t *m)
{
    if (!m) return -1;
    SC_STATUS st = sAPI_MutexCreate((sMutexRef *)m, SC_FIFO);
    return (st == SC_SUCCESS) ? 0 : -1;
}

void HAL_OS_MutexLock(HalMutexRef_t m)
{
    if (m) sAPI_MutexLock((sMutexRef)m, SC_SUSPEND);
}

void HAL_OS_MutexUnlock(HalMutexRef_t m)
{
    if (m) sAPI_MutexUnLock((sMutexRef)m);
}

int HAL_OS_MsgQCreate(HalMsgQRef_t *q, const char *name,
                      uint32_t msgSize, uint32_t depth)
{
    if (!q) return -1;
    SC_STATUS st = sAPI_MsgQCreate((sMsgQRef *)q, (char *)name,
                                   (UINT32)msgSize, (UINT32)depth, SC_FIFO);
    return (st == SC_SUCCESS) ? 0 : -1;
}

int HAL_OS_MsgQSend(HalMsgQRef_t q, const void *msg, uint32_t timeoutTicks)
{
    (void)timeoutTicks;
    SC_STATUS st = sAPI_MsgQSend((sMsgQRef)q, (SIM_MSG_T *)msg);
    return (st == SC_SUCCESS) ? 0 : -1;
}

int HAL_OS_MsgQRecv(HalMsgQRef_t q, void *msg, uint32_t timeoutTicks)
{
    UINT32 tout = (timeoutTicks == HAL_OS_WAIT_FOREVER) ? SC_SUSPEND :
                  (timeoutTicks == HAL_OS_NO_WAIT)      ? SC_NO_SUSPEND :
                  timeoutTicks;
    SC_STATUS st = sAPI_MsgQRecv((sMsgQRef)q, (SIM_MSG_T *)msg, tout);
    return (st == SC_SUCCESS) ? 0 : -1;
}

void *HAL_OS_Malloc(uint32_t size)
{
    return sAPI_Malloc(size);
}

void HAL_OS_Free(void *ptr)
{
    if (ptr) sAPI_Free(ptr);
}

void HAL_OS_UrcRegister(HalMsgQRef_t q, uint32_t mask)
{
    sAPI_UrcRefRegister((sMsgQRef)q, mask);
}

/* ========================== HAL_NET ======================================= */

static INT32 s_netSocketFd = -1;

void HAL_NET_Init(void)
{
    sAPI_NetworkInit();
}

void HAL_NET_NitzEnable(void)
{
    sAPI_NetworkSetCtzu(1);
}

int HAL_NET_GetImei(char *outImei, uint32_t bufLen)
{
    if (!outImei || bufLen == 0) return -1;
    if (sAPI_SysGetImei(outImei) == 0) return 0;
    return -1;
}

int HAL_NET_GetIccid(char *outIccid, uint32_t bufLen)
{
    if (!outIccid || bufLen == 0) return -1;
    if (sAPI_SysGetIccid(outIccid) == SC_SIM_RETURN_SUCCESS) return 0;
    return -1;
}

int HAL_NET_GetSimState(void)
{
    UINT8 cpin = SC_SIM_UNKNOW;

    if (sAPI_SimcardPinGet(&cpin) != SC_SIM_RETURN_SUCCESS) return HAL_SIM_UNKNOWN;
    switch (cpin) {
    case SC_SIM_READY:
        return HAL_SIM_READY;
    case SC_SIM_PIN:
    case SC_SIM_PUK:
    case SC_SIM_BLOCKED:
        return HAL_SIM_LOCKED;
    case SC_SIM_REMOVED:
    case SC_SIM_CRASH:
    case SC_SIM_NOINSRETED:
        return HAL_SIM_ABSENT;
    default:
        return HAL_SIM_UNKNOWN;
    }
}

int HAL_NET_SetRadio(int on)
{
    return (sAPI_NetworkSetCfun(on ? SC_CFUN_1 : SC_CFUN_0) == SC_NET_SUCCESS) ? 0 : -1;
}

int HAL_NET_ActivatePdp(int pdpId)
{
    return (sAPI_TcpipPdpActive(pdpId, 1) == 0) ? 0 : -1;
}

int HAL_NET_Connect(const char *host, int port)
{
    SCsockAddrIn addr;
    INT32 fd;

    if (s_netSocketFd >= 0) {
        sAPI_TcpipClose(s_netSocketFd);
        s_netSocketFd = -1;
    }

    fd = sAPI_TcpipSocket(SC_AF_INET, SC_SOCK_STREAM, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = SC_AF_INET;
    addr.sin_port = sAPI_htons((UINT16)port);
    addr.sin_addr.s_addr = sAPI_TcpipInetAddr((INT8 *)host);

    if (sAPI_TcpipConnect(fd, (SCsockAddr *)&addr, sizeof(addr)) != 0) {
        sAPI_TcpipClose(fd);
        return -1;
    }

    s_netSocketFd = fd;
    return 0;
}

void HAL_NET_Disconnect(void)
{
    if (s_netSocketFd >= 0) {
        sAPI_TcpipClose(s_netSocketFd);
        s_netSocketFd = -1;
    }
}

int HAL_NET_Send(const uint8_t *data, uint32_t len)
{
    if (s_netSocketFd < 0 || !data) return -1;
    return (int)sAPI_TcpipSend(s_netSocketFd, (void *)data, (INT32)len, 0);
}

extern int lwip_getsockerrno(int s);

int HAL_NET_Recv(uint8_t *buf, uint32_t bufLen, uint32_t timeoutUs)
{
    (void)timeoutUs;
    if (s_netSocketFd < 0 || !buf) return -1;

    int ret = (int)sAPI_TcpipRecv(s_netSocketFd, buf, (INT32)bufLen, SC_MSG_DONTWAIT);
    if (ret < 0) {
        int err = lwip_getsockerrno(s_netSocketFd);
        /* 11 = EAGAIN / EWOULDBLOCK (Không có dữ liệu mới trên non-blocking socket) */
        if (err == 11 || err == 0) {
            return 0;
        }
        return -1;
    }
    return ret;
}

int HAL_NET_IsConnected(void)
{
    return (s_netSocketFd >= 0) ? 1 : 0;
}

int HAL_NET_GetCsq(uint8_t *csq)
{
    if (!csq) return -1;
    sAPI_NetworkGetCsq((UINT8 *)csq);
    return 0;
}

void HAL_NET_SetNonBlocking(void)
{
    if (s_netSocketFd >= 0) {
        int flags = 1;
        sAPI_TcpipIoctlsocket(s_netSocketFd, SC_FIONBIO, &flags);
    }
}

/* ========================== HAL_GNSS ====================================== */

static HalGnssUrcCb_t s_gnssUrcCb = NULL;
static void          *s_gnssUrcCtx = NULL;

int HAL_GNSS_GetInfo(HalGnssInfo_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    (void)s_gnssUrcCb;
    (void)s_gnssUrcCtx;
    return -1;
}

void HAL_GNSS_RegisterUrc(HalGnssUrcCb_t cb, void *ctx)
{
    s_gnssUrcCb  = cb;
    s_gnssUrcCtx = ctx;
}

int HAL_GNSS_GetRtc(HalDateTime_t *rtc)
{
    t_rtc r;
    if (!rtc) return -1;
    if (sAPI_GetRealTimeClock(&r) == 0) {
        rtc->tm_year = (uint16_t)r.tm_year;
        rtc->tm_mon  = (uint8_t)r.tm_mon;
        rtc->tm_mday = (uint8_t)r.tm_mday;
        rtc->tm_hour = (uint8_t)r.tm_hour;
        rtc->tm_min  = (uint8_t)r.tm_min;
        rtc->tm_sec  = (uint8_t)r.tm_sec;
        return 0;
    }
    return -1;
}

int HAL_GNSS_SetRtc(const HalDateTime_t *rtc)
{
    t_rtc r;
    if (!rtc) return -1;
    memset(&r, 0, sizeof(r));
    r.tm_year = rtc->tm_year;
    r.tm_mon  = rtc->tm_mon;
    r.tm_mday = rtc->tm_mday;
    r.tm_hour = rtc->tm_hour;
    r.tm_min  = rtc->tm_min;
    r.tm_sec  = rtc->tm_sec;
    return (sAPI_SetRealTimeClock(&r) == 0) ? 0 : -1;
}

extern void GPS_OnUrcString(const char *gpsUrc);

static sMsgQRef s_gnssUrcMsgQ = NULL;
static sTaskRef s_gnssTaskRef = NULL;
static uint8_t  s_gnssTaskStack[1024 * 4];

/*
 * Che do ve tinh: chip Unicorecomm ban trong nuoc, SDK ban nay chi co
 * 1 = GPS+BeiDou+QZSS, 2 = BeiDou, 3 = GPS+QZSS (khong co Galileo/GLONASS).
 */
static volatile int s_gnssWantMode = SC_GNSS_MODE_GPS_BDS_QZSS;
static volatile int s_gnssStarted = 0;

static int GnssApplyMode(void)
{
    SC_Gnss_Mode cur = sAPI_GnssModeGet();
    SC_Gnss_Return_Code rc;

    if ((int)cur == s_gnssWantMode) {
        HAL_LOG_Print("[GNSS] mode %d", (int)cur);
        return 0;
    }
    rc = sAPI_GnssModeSet((SC_Gnss_Mode)s_gnssWantMode);
    HAL_LOG_Print("[GNSS] mode %d -> %d rc=%d now=%d",
                  (int)cur, s_gnssWantMode, (int)rc, (int)sAPI_GnssModeGet());
    return (rc == 0) ? 0 : -1;
}

int HAL_GNSS_SetMode(int mode)
{
    if (mode < SC_GNSS_MODE_GPS_BDS_QZSS || mode > SC_GNSS_MODE_GPS_QZSS) return -1;
    s_gnssWantMode = mode;
    if (!s_gnssStarted) return 0; /* ap dung khi task GNSS bat chip */
    return GnssApplyMode();
}

int HAL_GNSS_GetMode(void)
{
    if (!s_gnssStarted) return -1;
    return (int)sAPI_GnssModeGet();
}

/* Che do giu tinh (A76xx GNSS Application Note: chong troi khi dung yen) */
static volatile int s_gnssStaticHoldCms = -1; /* -1 = khong gui lenh, de mac dinh chip */

static int GnssApplyStaticHold(void)
{
    char cmd[40];
    SC_Gnss_Return_Code rc;

    if (s_gnssStaticHoldCms < 0) return 0;
    snprintf(cmd, sizeof(cmd), "$CFGDYN,h02,1,%d", s_gnssStaticHoldCms);
    rc = sAPI_SendCmd2Gnss(cmd);
    HAL_LOG_Print("[GNSS] %s rc=%d", cmd, (int)rc);
    return (rc == 0) ? 0 : -1;
}

int HAL_GNSS_SetStaticHold(int cmPerSec)
{
    if (cmPerSec < 0 || cmPerSec > 1000) return -1;
    s_gnssStaticHoldCms = cmPerSec;
    if (!s_gnssStarted) return 0;
    return GnssApplyStaticHold();
}

/* AGPS: sAPI_GnssAgpsSeviceOpen mo socket toi may chu AGNSS va cho, nen chay
 * trong task rieng de khong chan task GNSS (mat URC) hay task NASA (watchdog) */
static sTaskRef s_agpsTaskRef = NULL;
static uint8_t  s_agpsTaskStack[1024 * 4];
static volatile int s_agpsRequested = 0;
static volatile int s_agpsBusy = 0;

static void sTask_Agps(void *argv)
{
    (void)argv;
    for (;;) {
        if (!s_agpsRequested) {
            sAPI_TaskSleep(200);
            continue;
        }
        s_agpsRequested = 0;
        s_agpsBusy = 1;
        HAL_LOG_Print("[GNSS/AGPS] download... (GNSS power=%d)", (int)sAPI_GnssPowerStatusGet());
        {
            UINT32 t0 = sAPI_GetTicks();
            int rc = (int)sAPI_GnssAgpsSeviceOpen();
            UINT32 ms = (sAPI_GetTicks() - t0) * 5u; /* 200 tick/s */
            const char *txt =
                (rc == 0)   ? "OK" :
                (rc == -1)  ? "GNSS power off" :
                (rc == 101) ? "open socket fail" :
                (rc == 102) ? "fetch AGNSS server fail" :
                (rc == 103) ? "connect AGNSS server fail" :
                (rc == 104) ? "socket write fail" :
                (rc == 105) ? "socket read fail" :
                (rc == 106) ? "AGNSS data error" :
                (rc == 107) ? "send data to GNSS chip fail" : "unknown";
            HAL_LOG_Print("[GNSS/AGPS] done rc=%d (%s) in %lu ms", rc, txt, (unsigned long)ms);
        }
        s_agpsBusy = 0;
    }
}

int HAL_GNSS_AgpsRequest(void)
{
    if (!s_gnssStarted || s_agpsBusy || s_agpsRequested) return -1;
    if (s_agpsTaskRef == NULL) {
        if (sAPI_TaskCreate(&s_agpsTaskRef, s_agpsTaskStack, sizeof(s_agpsTaskStack),
                            25, "agps_task", sTask_Agps, NULL) != SC_SUCCESS) {
            s_agpsTaskRef = NULL;
            HAL_LOG_Print("[GNSS/AGPS] create task fail");
            return -1;
        }
    }
    s_agpsRequested = 1;
    return 0;
}

static void sTask_GnssUrcListener(void *argv)
{
    SIM_MSG_T msg;
    (void)argv;

    HAL_LOG("[GNSS] Task Listener Start");
    /* Trinh tu theo A76xx GNSS Application Note: bat nguon 1 lan (bat 2 lan lam
     * hong nap dong), moi lenh cach nhau ~100 ms de chip kip tra loi */
    sAPI_GnssPowerStatusSet(SC_GNSS_POWER_ON);
    sAPI_TaskSleep(100);
    s_gnssStarted = 1;
    HAL_LOG_Print("[GNSS] hot start rc=%d", (int)sAPI_GnssStartMode(SC_GNSS_START_HOT));
    sAPI_TaskSleep(20);
    (void)GnssApplyMode();
    sAPI_TaskSleep(20);
    (void)GnssApplyStaticHold();
    sAPI_TaskSleep(20);
    sAPI_GnssInfoGet(1); /* Bat URC dinh ky 1s (+CGNSSINFO) */

    while (1) {
        memset(&msg, 0, sizeof(msg));
        if (sAPI_MsgQRecv(s_gnssUrcMsgQ, &msg, SC_SUSPEND) == SC_SUCCESS) {
            if (msg.arg3) {
                const char *str = (const char *)msg.arg3;
                HAL_LOG("[GNSS/URC] %s", str);
                GPS_OnUrcString(str);
                sAPI_Free(msg.arg3);
            }
        }
    }
}

void HAL_GNSS_UrcListenerStart_Impl(void)
{
    if (s_gnssUrcMsgQ == NULL) {
        if (sAPI_MsgQCreate(&s_gnssUrcMsgQ, "gnss_urcq", sizeof(SIM_MSG_T), 20, SC_FIFO) == SC_SUCCESS) {
            sAPI_UrcRefRegister(s_gnssUrcMsgQ, SC_URC_GNSS_MASK);
            sAPI_TaskCreate(&s_gnssTaskRef, s_gnssTaskStack, sizeof(s_gnssTaskStack), 10, "gnss_task", sTask_GnssUrcListener, NULL);
        }
    }
}

void HAL_GNSS_UrcListenerStart(void)
{
    HAL_GNSS_UrcListenerStart_Impl();
}

/* ========================== HAL_SPI ======================================= */

/*
 * Cau hinh SPI that su truyen cho SDK. HalSpiDev_t (8 byte) KHONG cung bo cuc
 * voi SC_SPI_DEV (index/clock/mode/csMode) nen khong duoc ep kieu truc tiep:
 * SDK se doc clock/mode/csMode tu vung nho ngoai handle.
 */
static SC_SPI_DEV s_spiCfg;

int HAL_SPI_Init(HalSpiDev_t *dev, int spiIndex)
{
    SC_SPI_ReturnCode rc;

    if (!dev) return -1;
    dev->index = spiIndex;

    memset(&s_spiCfg, 0, sizeof(s_spiCfg));
    s_spiCfg.index = (unsigned char)spiIndex;
    /* Clock thap: bus di qua IC chuyen muc dung dien tro keo len nen suon len cham */
    s_spiCfg.clock = SPI_CLOCK_812KHz;
    s_spiCfg.mode = SPI_MODE_PH0_PO0;   /* SPI mode 0: NOR flash lay mau o canh len */
    s_spiCfg.csMode = GPIO_MODE;        /* CS cua SDK giu suot chuoi byte; CS flash do driver tu lai */
    rc = sAPI_SpiConfigInitEx(&s_spiCfg);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_Write(HalSpiDev_t *dev, const uint8_t *data, uint32_t len)
{
    SC_SPI_ReturnCode rc;
    (void)dev;
    rc = sAPI_SpiWriteBytesEx(&s_spiCfg, (unsigned char *)data, (unsigned int)len);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_Read(HalSpiDev_t *dev,
                 const uint8_t *cmd, uint32_t cmdLen,
                 uint8_t *rxBuf, uint32_t rxLen)
{
    SC_SPI_ReturnCode rc;
    (void)dev;
    rc = sAPI_SpiReadBytesEx(&s_spiCfg, (unsigned char *)cmd, (unsigned int)cmdLen,
                             (unsigned char *)rxBuf, (unsigned int)rxLen);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_CustomCsSetLevel(HalSpiDev_t *dev, int level)
{
    SC_SPI_ReturnCode rc;
    if (!dev) return -1;
    rc = sAPI_SpiCustomCsSetLevel(dev->index, level);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

/* ========================== HAL_FS ======================================== */

HalFile_t HAL_FS_Open(const char *path, const char *mode)
{
    SCFILE *f = sAPI_fopen(path, mode);
    return (HalFile_t)f;
}

int HAL_FS_Close(HalFile_t f)
{
    if (!f) return -1;
    return sAPI_fclose((SCFILE *)f) == 0 ? 0 : -1;
}

int HAL_FS_Read(HalFile_t f, void *buf, uint32_t size)
{
    if (!f) return -1;
    return (int)sAPI_fread(buf, 1, size, (SCFILE *)f);
}

int HAL_FS_Write(HalFile_t f, const void *buf, uint32_t size)
{
    if (!f) return -1;
    return (int)sAPI_fwrite(buf, 1, size, (SCFILE *)f);
}

int HAL_FS_Seek(HalFile_t f, int32_t offset, int whence)
{
    if (!f) return -1;
    return sAPI_fseek((SCFILE *)f, offset, whence);
}

int HAL_FS_Size(HalFile_t f)
{
    if (!f) return -1;
    sAPI_fseek((SCFILE *)f, 0, HAL_FS_SEEK_END);
    return (int)sAPI_ftell((SCFILE *)f);
}

int HAL_FS_Delete(const char *path)
{
    return sAPI_remove(path) == 0 ? 0 : -1;
}

/* ========================== HAL_SMS ======================================= */

static sMsgQRef s_smsRspQ = NULL;

static void DrainSmsRspQ(void)
{
    SIM_MSG_T stale;
    while (1) {
        memset(&stale, 0, sizeof(stale));
        if (sAPI_MsgQRecv(s_smsRspQ, &stale, SC_NO_SUSPEND) != SC_SUCCESS) break;
        if (stale.arg3) sAPI_Free(stale.arg3);
    }
}

int HAL_SMS_SetFormat(int format)
{
    SC_SMSReturnCode ret = sAPI_SmsSetFormat(format);
    return (ret == SC_SMS_SUCESS) ? 0 : -1;
}

int HAL_SMS_SetNewMsgInd(int mode, int mt, int bm, int ds, int bfr, int dummy)
{
    (void)dummy;
    SC_SMSReturnCode ret = sAPI_SmsSetNewMsgInd(mode, mt, bm, ds, bfr, 0);
    return (ret == SC_SMS_SUCESS) ? 0 : -1;
}

int HAL_SMS_Send(const char *phone, const char *text)
{
    SC_SMSReturnCode ret;
    if (!phone || !text) return -1;
    if (s_smsRspQ == NULL) {
        if (sAPI_MsgQCreate(&s_smsRspQ, "hal_sms_rspq", sizeof(SIM_MSG_T), 4, SC_FIFO) != SC_SUCCESS)
            return -1;
    }
    ret = sAPI_SmsSendMsg(1, (UINT8 *)text, (UINT16)strlen(text), (UINT8 *)phone, s_smsRspQ);
    if (ret == SC_SMS_SUCESS) {
        SIM_MSG_T rsp = {0};
        if (sAPI_MsgQRecv(s_smsRspQ, &rsp, 2000) == SC_SUCCESS) {
            if (rsp.arg3) sAPI_Free(rsp.arg3);
        }
        return 0;
    }
    return -1;
}

int HAL_SMS_EnsureReady(void)
{
    if (HAL_SMS_SetFormat(1) == 0 && HAL_SMS_SetNewMsgInd(1, 2, 1, 0, 0, 0) == 0) {
        return 0;
    }
    return -1;
}

int HAL_SMS_CreateUrcQueue(HalSmsRspQ_t *q)
{
    sMsgQRef urcQ = NULL;
    SC_STATUS st;

    if (s_smsRspQ == NULL) {
        st = sAPI_MsgQCreate(&s_smsRspQ, "hal_sms_rspq",
                             sizeof(SIM_MSG_T), 4, SC_FIFO);
        if (st != SC_SUCCESS) return -1;
    }

    st = sAPI_MsgQCreate(&urcQ, "hal_sms_urcq",
                         sizeof(SIM_MSG_T), 10, SC_FIFO);
    if (st != SC_SUCCESS) return -1;

    *q = (HalSmsRspQ_t)urcQ;
    return 0;
}

int HAL_SMS_RegisterUrc(HalSmsRspQ_t q, uint32_t mask)
{
    (void)mask;
    sAPI_UrcRefRegister((sMsgQRef)q, SC_URC_SMS_MASK);
    return 0;
}

int HAL_SMS_Read(int index, char *outBuf, uint32_t bufLen)
{
    SIM_MSG_T rsp = {0};
    if (!outBuf || !s_smsRspQ) return -1;

    DrainSmsRspQ();
    if (sAPI_SmsReadMsg(1, index, s_smsRspQ) != SC_SMS_SUCESS) return -1;
    if (sAPI_MsgQRecv(s_smsRspQ, &rsp, 1000) != SC_SUCCESS) return -1;

    if (rsp.arg3) {
        strncpy(outBuf, (const char *)rsp.arg3, bufLen - 1);
        outBuf[bufLen - 1] = '\0';
        sAPI_Free(rsp.arg3);
        return 0;
    }
    return -1;
}

int HAL_SMS_Delete(int index)
{
    SIM_MSG_T rsp = {0};
    if (!s_smsRspQ) return -1;

    DrainSmsRspQ();
    if (sAPI_SmsDelOneMsg(index, s_smsRspQ) != SC_SMS_SUCESS) return -1;
    if (sAPI_MsgQRecv(s_smsRspQ, &rsp, 1000) == SC_SUCCESS) {
        if (rsp.arg3) sAPI_Free(rsp.arg3);
    }
    return 0;
}

int HAL_SMS_RecvUrc(HalSmsRspQ_t q, HalSmsUrcEvent_t *evt, uint32_t timeoutTicks)
{
    SIM_MSG_T msg = {0};
    UINT32 tout;

    if (!evt) return -1;
    memset(evt, 0, sizeof(*evt));

    tout = (timeoutTicks == HAL_OS_WAIT_FOREVER) ? SC_SUSPEND :
           (timeoutTicks == HAL_OS_NO_WAIT)      ? SC_NO_SUSPEND :
           timeoutTicks;

    if (sAPI_MsgQRecv((sMsgQRef)q, &msg, tout) != SC_SUCCESS) return -1;
    if (msg.msg_id != SRV_URC) {
        if (msg.arg3) sAPI_Free(msg.arg3);
        return -1;
    }

    if (msg.arg2 == SC_URC_NEW_MSG_IND) {
        evt->type = HAL_SMS_URC_NEW_MSG;
        if (msg.arg3) {
            const char *p = strrchr((const char *)msg.arg3, ',');
            evt->index = p ? atoi(p + 1) : 0;
            sAPI_Free(msg.arg3);
        }
    } else if (msg.arg2 == SC_URC_FLASH_MSG) {
        evt->type = HAL_SMS_URC_FLASH_MSG;
        if (msg.arg3) {
            strncpy(evt->body, (const char *)msg.arg3, sizeof(evt->body) - 1);
            sAPI_Free(msg.arg3);
        }
    } else {
        if (msg.arg3) sAPI_Free(msg.arg3);
        return -1;
    }

    return 0;
}

/* ========================== HAL_ADC ======================================= */

uint32_t HAL_ADC_ReadMv(int channel)
{
    /* sAPI_ReadAdc tra ve gia tri dien ap tai chan ADC tinh bang mV */
    unsigned int mv = sAPI_ReadAdc(channel);
    return (uint32_t)mv;
}


/* ========================== HAL_FOTA ====================================== */

int HAL_FOTA_AppDownload(const char *url, uint32_t recvTimeoutMs)
{
    SCAppDownloadPram pram;

    if (!url) return -1;

    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        pram.mod = SC_APP_DOWNLOAD_HTTP_MOD;
    } else if (strncmp(url, "ftp://", 6) == 0 || strncmp(url, "ftps://", 7) == 0) {
        pram.mod = SC_APP_DOWNLOAD_FTP_MOD;
    } else {
        return -1;
    }
    pram.url = (char *)url;
    pram.recvtimeout = recvTimeoutMs;

    /* Theo demo SDK (demo_app_download.c): phai mo khoa bao ve flash truoc khi
     * tai, neu khong goi ghi xuong flash khong co hieu luc. */
    sAPI_FBF_Disable();

    return (int)sAPI_AppDownload(&pram);
}

int HAL_FOTA_AppPackageVerify(uint32_t *outSize)
{
    SCAppPackageInfo info = {0};
    int ret = sAPI_AppPackageCrc(&info);

    if (outSize) *outSize = (uint32_t)info.binSize;
    return ret;
}

int HAL_FOTA_SysStart(const char *url, HalFotaSysCb_t cb)
{
    /* static: SDK co the con tham chieu param sau khi ham tra ve */
    static struct SC_FotaApiParam param;
    const char *rest;
    const char *at;
    const char *colon;
    const char *slash;

    if (!url || !cb) return -1;
    memset(&param, 0, sizeof(param));

    if (strncmp(url, "http://", 7) == 0) {
        /* Theo demo SDK: HTTP truyen host khong kem scheme */
        param.mode = 1;
        rest = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        param.mode = 1;
        rest = url;
    } else if (strncmp(url, "ftp://", 6) == 0) {
        param.mode = 0;
        rest = url + 6;
        /* Tach user:pass@ (neu co) nam truoc dau '/' dau tien */
        slash = strchr(rest, '/');
        at = strchr(rest, '@');
        if (at && (!slash || at < slash)) {
            size_t ulen, plen = 0;
            colon = memchr(rest, ':', (size_t)(at - rest));
            ulen = (size_t)((colon ? colon : at) - rest);
            if (colon) plen = (size_t)(at - colon - 1);
            if (ulen >= sizeof(param.username) || plen >= sizeof(param.password)) return -1;
            memcpy(param.username, rest, ulen);
            if (colon) memcpy(param.password, colon + 1, plen);
            rest = at + 1;
        }
    } else {
        return -1;
    }

    if (rest[0] == '\0' || strlen(rest) >= sizeof(param.host)) return -1;
    strcpy(param.host, rest);
    /* SDK dung lenh AT+CFOTA=0,<mode>,"<host>",<user>,<pass>: de trong user/pass
     * thi AT parser bao "Missing AT command parameter", FOTA khong chay ma
     * sAPI_FotaServiceBegin van tra ve 0 → luon dien gia tri mac dinh. */
    if (param.username[0] == '\0') strcpy(param.username, "anonymous");
    if (param.password[0] == '\0') strcpy(param.password, "anonymous");
    param.sc_fota_cb = (sc_fota_callback)cb;

    return sAPI_FotaServiceBegin((void *)&param);
}
