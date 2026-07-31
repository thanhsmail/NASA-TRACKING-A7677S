/**
 * @file    hal_os.h
 * @brief   HAL - OS Abstraction: Task, Sleep, Mutex, MsgQueue, Memory, Reset
 */
#ifndef HAL_OS_H
#define HAL_OS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opaque handles ---- */
typedef void *HalTaskRef_t;
typedef void *HalMutexRef_t;
typedef void *HalMsgQRef_t;

/* ---- Power up reason ---- */
typedef enum {
    HAL_POWER_UP_NORMAL = 0,
    HAL_POWER_UP_SOFTWARE_RESET,
    HAL_POWER_UP_RESET_KEY,
    HAL_POWER_UP_POWER_KEY
} HalPowerUpReason_t;

/* ---- Constants ---- */
#define HAL_OS_WAIT_FOREVER  0xFFFFFFFFu
#define HAL_OS_NO_WAIT       0u
#define HAL_TICKS_PER_SEC    200u          /* SIMCom OpenCPU: 200 tick/s */

/* ---- Task ---- */
int HAL_OS_TaskCreate(HalTaskRef_t *ref, void *stack, uint32_t stackSize,
                      int priority, const char *name,
                      void (*fn)(void *), void *arg);

void HAL_OS_TaskSleep(uint32_t ticks);
void HAL_OS_SysReset(void);
uint32_t HAL_OS_GetTick(void);
HalPowerUpReason_t HAL_OS_GetPowerUpReason(void);

/* ---- Mutex ---- */
int  HAL_OS_MutexCreate(HalMutexRef_t *m);
void HAL_OS_MutexLock(HalMutexRef_t m);
void HAL_OS_MutexUnlock(HalMutexRef_t m);

/* ---- Message Queue ---- */
int HAL_OS_MsgQCreate(HalMsgQRef_t *q, const char *name,
                      uint32_t msgSize, uint32_t depth);

int HAL_OS_MsgQSend(HalMsgQRef_t q, const void *msg, uint32_t timeoutTicks);
int HAL_OS_MsgQRecv(HalMsgQRef_t q,       void *msg, uint32_t timeoutTicks);

/* ---- Memory ---- */
void *HAL_OS_Malloc(uint32_t size);
void  HAL_OS_Free(void *ptr);

/* ---- URC Registration ---- */
void HAL_OS_UrcRegister(HalMsgQRef_t q, uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif /* HAL_OS_H */
