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
    sAPI_GpioConfig(pin, cfg);
}

void HAL_GPIO_Write(int pin, int level)
{
    sAPI_GpioSetValue(pin, level);
}

int HAL_GPIO_Read(int pin)
{
    return (int)sAPI_GpioGetValue(pin);
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

static void sTask_GnssUrcListener(void *argv)
{
    SIM_MSG_T msg;
    (void)argv;

    HAL_LOG("[GNSS] Task Listener Start");
    sAPI_GnssPowerStatusSet(SC_GNSS_POWER_ON);
    sAPI_TaskSleep(100);
    sAPI_GnssInfoGet(1); /* Bat URC dinh ky 1s (+CGPSINFO) */

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

int HAL_SPI_Init(HalSpiDev_t *dev, int spiIndex)
{
    SC_SPI_DEV *d = (SC_SPI_DEV *)dev;
    d->index = spiIndex;
    SC_SPI_ReturnCode rc = sAPI_SpiConfigInitEx(d);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_Write(HalSpiDev_t *dev, const uint8_t *data, uint32_t len)
{
    SC_SPI_DEV *d = (SC_SPI_DEV *)dev;
    SC_SPI_ReturnCode rc = sAPI_SpiWriteBytesEx(d, (unsigned char *)data, (unsigned int)len);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_Read(HalSpiDev_t *dev,
                 const uint8_t *cmd, uint32_t cmdLen,
                 uint8_t *rxBuf, uint32_t rxLen)
{
    SC_SPI_DEV *d = (SC_SPI_DEV *)dev;
    SC_SPI_ReturnCode rc = sAPI_SpiReadBytesEx(d, (unsigned char *)cmd, (unsigned int)cmdLen,
                                                (unsigned char *)rxBuf, (unsigned int)rxLen);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_CustomCsSetLevel(HalSpiDev_t *dev, int level)
{
    SC_SPI_DEV *d = (SC_SPI_DEV *)dev;
    SC_SPI_ReturnCode rc = sAPI_SpiCustomCsSetLevel(d->index, level);
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

