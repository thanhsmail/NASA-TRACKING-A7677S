/**
 * @file    port_os.c
 * @brief   HAL_OS: task, mutex, hang doi, tick, bo nho, nguon / ngu, watchdog -- SIMCom A7677S OpenSDK
 *
 * Chi cac file trong port/ duoc goi sAPI_*. Ham SDK dung o day: SDK_API_LIST.md.
 */

#include "port_sdk.h"

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

int HAL_OS_HwWatchdogStart(uint8_t periodReg)
{
    /* Trinh tu theo sc_demo/src/demo_wtd.c: dat chu ky, bat fault wake, bat
     * watchdog, roi cho an 1 lan de chu ky moi co hieu luc */
    int ret = sAPI_SetWtdTimeOutPeriod(periodReg);
    ret |= sAPI_FalutWakeEnable(1);
    ret |= sAPI_SoftWtdEnable(1);
    ret |= sAPI_FeedWtd();
    HAL_LOG_Print("[WDT] hardware watchdog start period=0x%02X ret=%d", periodReg, ret);
    return (ret == 0) ? 0 : -1;
}

void HAL_OS_HwWatchdogFeed(void)
{
    (void)sAPI_FeedWtd();
}

int HAL_OS_SystemSleepEnable(int enable)
{
    int ret = sAPI_SystemSleepSet(enable ? SC_SYSTEM_SLEEP_ENABLE : SC_SYSTEM_SLEEP_DISABLE);
    HAL_LOG_Print("[SLEEP] system sleep %s ret=%d (now=%d, usb=%d)", enable ? "enable" : "disable",
                  ret, (int)sAPI_SystemSleepGet(), HAL_OS_UsbConnected());
    return (ret == 0) ? 0 : -1;
}

int HAL_OS_SystemSleepIsEnabled(void)
{
    return (sAPI_SystemSleepGet() == SC_SYSTEM_SLEEP_ENABLE) ? 1 : 0;
}

int HAL_OS_UsbConnected(void)
{
    return (sAPI_UsbVbusDetect() == USB_VBUS_HIGH) ? 1 : 0;
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

int HAL_OS_GetPowerUpRaw(void)
{
    return (int)sAPI_GetPowerUpEvent();
}

int HAL_OS_GetPowerDownRaw(void)
{
    return (int)sAPI_GetPowerDownEvent();
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
