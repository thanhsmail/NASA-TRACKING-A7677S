/**
 * @file mock_hal_os.c
 * @brief Mock HAL OS implementation for Host Unit Testing
 */
#include "mock_hal_os.h"
#include <stdlib.h>
#include <string.h>

static uint32_t s_mockTick = 1000;

void Mock_HAL_OS_SetTick(uint32_t tick)
{
    s_mockTick = tick;
}

void Mock_HAL_OS_AdvanceTicks(uint32_t count)
{
    s_mockTick += count;
}

uint32_t HAL_OS_GetTick(void)
{
    return s_mockTick;
}

void HAL_OS_TaskSleep(uint32_t ticks)
{
    s_mockTick += ticks;
}

void HAL_OS_SysReset(void)
{
}

HalPowerUpReason_t HAL_OS_GetPowerUpReason(void)
{
    return HAL_POWER_UP_NORMAL;
}

int HAL_OS_TaskCreate(HalTaskRef_t *ref, void *stack, uint32_t stackSize,
                      int priority, const char *name,
                      void (*fn)(void *), void *arg)
{
    (void)stack; (void)stackSize; (void)priority; (void)name; (void)fn; (void)arg;
    if (ref) *ref = (void*)0x1234;
    return 0;
}

int HAL_OS_MutexCreate(HalMutexRef_t *m)
{
    if (m) *m = (void*)0x5678;
    return 0;
}

void HAL_OS_MutexLock(HalMutexRef_t m) { (void)m; }
void HAL_OS_MutexUnlock(HalMutexRef_t m) { (void)m; }

int HAL_OS_MsgQCreate(HalMsgQRef_t *q, const char *name, uint32_t msgSize, uint32_t depth)
{
    (void)name; (void)msgSize; (void)depth;
    if (q) *q = (void*)0x9ABC;
    return 0;
}

int HAL_OS_MsgQSend(HalMsgQRef_t q, const void *msg, uint32_t timeoutTicks)
{
    (void)q; (void)msg; (void)timeoutTicks;
    return 0;
}

int HAL_OS_MsgQRecv(HalMsgQRef_t q, void *msg, uint32_t timeoutTicks)
{
    (void)q; (void)msg; (void)timeoutTicks;
    return -1;
}

void *HAL_OS_Malloc(uint32_t size) { return malloc(size); }
void HAL_OS_Free(void *ptr) { free(ptr); }
void HAL_OS_UrcRegister(HalMsgQRef_t q, uint32_t mask) { (void)q; (void)mask; }
