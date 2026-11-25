/**
 * @file    hal_log.h
 * @brief   HAL - Logging / Debug API Contract
 *
 * App layer chi goi HAL_LOG(...). Implementation thuc te nam trong
 * port/<chip>/port_log.c (sAPI_Debug) hoac bat ky backend nao khac.
 *
 * Quy tac: KHONG include simcom_api.h o day -- chi stdarg.h.
 */
#ifndef HAL_LOG_H
#define HAL_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  In chuoi debug. Prototype giong printf.
 * @param  fmt  Format string
 * @param  ...  Tham so format
 */
void HAL_LOG_Print(const char *fmt, ...);

/**
 * @brief  Bat (1) / tat (0) chep log ra cong COM ao USB (lenh debug,1).
 *         Chi giu trong RAM, khoi dong lai la tat. @return 0 = OK, -1 = loi
 */
int  HAL_LOG_SetUsbMirror(int on);
/** @return 1 neu dang chep log ra USB */
int  HAL_LOG_GetUsbMirror(void);

/** Macro tien loi -- dung thay sAPI_Debug trong toan bo app_*.c */
#define HAL_LOG(...)  HAL_LOG_Print(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* HAL_LOG_H */
