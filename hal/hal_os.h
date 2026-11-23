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

/**
 * @brief  Bat watchdog phan cung cua module (simcom_wtd.h). Sau khi bat phai
 *         goi HAL_OS_HwWatchdogFeed() thuong xuyen hon chu ky da dat, neu
 *         khong module tu reset. Khong co cach tat lai trong luc chay app.
 * @param  periodReg gia tri thanh ghi chu ky (bit 3:0); 0x04 = 16 giay theo demo SDK
 * @return 0 = OK, -1 = loi
 */
int  HAL_OS_HwWatchdogStart(uint8_t periodReg);
void HAL_OS_HwWatchdogFeed(void);

/**
 * @brief  Cho phep (1) / cam (0) module vao sleep he thong (sAPI_SystemSleepSet).
 *         Khi cho phep, module tu ngu luc moi task deu dang cho va thuc khi het
 *         hen gio cua task, co du lieu mang / SMS hoac ngat GPIO danh thuc.
 *         Module khong ngu khi dang cam USB.
 * @return 0 = OK, -1 = loi
 */
int  HAL_OS_SystemSleepEnable(int enable);
/** @return co sleep he thong hien tai (sAPI_SystemSleepGet): 1 = cho phep */
int  HAL_OS_SystemSleepIsEnabled(void);
/** @return 1 neu dang cam USB (VBUS cao) -- module khong ngu duoc, 0 neu khong */
int  HAL_OS_UsbConnected(void);
uint32_t HAL_OS_GetTick(void);
HalPowerUpReason_t HAL_OS_GetPowerUpReason(void);
/**
 * Mã thô của PMIC (sc_power.h), dùng chẩn đoán khởi động lại.
 * Bật: 1 = RESET_KEY (chân reset / watchdog), 2 = POWER_KEY (cấp nguồn),
 *      3 = SOFTWARE_RESET, 4 = RTC_ALARM, 5 = ERR.
 * Tắt lần trước: 1 = giữ phím nguồn, 2 = VRTC thấp, 3 = quá nhiệt,
 *      4 = sụt áp VSYS, 5 = watchdog PMIC hết giờ, 6 = giữ phím reset,
 *      7 = quá áp VSYS, 8 = ERR (không rõ). 0 = không đọc được.
 */
int HAL_OS_GetPowerUpRaw(void);
int HAL_OS_GetPowerDownRaw(void);

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
