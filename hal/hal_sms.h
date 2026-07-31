/**
 * @file    hal_sms.h
 * @brief   HAL - SMS Abstraction
 */
#ifndef HAL_SMS_H
#define HAL_SMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bitmask su kien SMS URC */
#define HAL_SMS_URC_MASK_NEW      (1u << 0)
#define HAL_SMS_URC_MASK_FLASH    (1u << 1)

typedef void *HalSmsRspQ_t;

typedef enum {
    HAL_SMS_URC_NONE      = 0,
    HAL_SMS_URC_NEW_MSG   = 1,
    HAL_SMS_URC_FLASH_MSG = 2
} HalSmsUrcType_t;

typedef struct {
    HalSmsUrcType_t type;
    int             index;
    char            body[256];
} HalSmsUrcEvent_t;

int HAL_SMS_EnsureReady(void);
int HAL_SMS_SetFormat(int format);
int HAL_SMS_SetNewMsgInd(int mode, int mt, int bm, int ds, int bfr, int dummy);
int HAL_SMS_Send(const char *phone, const char *text);
int HAL_SMS_RegisterUrc(HalSmsRspQ_t q, uint32_t mask);
int HAL_SMS_Read(int index, char *outBuf, uint32_t bufLen);
int HAL_SMS_Delete(int index);
int HAL_SMS_RecvUrc(HalSmsRspQ_t q, HalSmsUrcEvent_t *evt, uint32_t timeoutTicks);
int HAL_SMS_CreateUrcQueue(HalSmsRspQ_t *q);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SMS_H */
