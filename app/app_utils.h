#ifndef APP_UTILS_H
#define APP_UTILS_H

#include "hal/hal_gnss.h"
#include <stdint.h>

uint32_t Buffer_GetChecksum(const uint8_t *buffer, uint32_t length);
uint32_t GetTickNow(void);
/** Cộng / trừ số giây vào ngày giờ, tự sang ngày / tháng / năm */
void UTIL_AddSecondsToDateTime(HalDateTime_t *t, int32_t sec);
/** Giây từ 2000-01-01; 0 nếu ngày giờ ngoài 2020..2100 hoặc sai */
uint32_t UTIL_DateTimeToSec(const HalDateTime_t *t);
/** Đọc "YYYY-MM-DD HH:MM:SS"; @return 0 nếu hợp lệ */
int UTIL_ParseDateTime(const char *s, HalDateTime_t *out);
/** Đặt RTC = local (giờ Việt Nam) nếu RTC chưa có giờ hoặc lệch > RTC_MAX_DRIFT_SEC;
 *  src để log. @return 1 nếu đã ghi RTC */
int UTIL_CorrectRtc(const HalDateTime_t *local, const char *src);
int IsDigitChar(char c);
int Parse2Digits(const char *s);
int ParseDateTimeFromUrcTokens(const char *dateToken, const char *timeToken, HalDateTime_t *out);
void BuildDateTimeAutoFallback(char *dateTime, uint32_t dateTimeSize);
void TriggerNtpSyncIfNeeded(void);
void NitzEnableFromNetwork(void);
/** 1 = đang ngủ sâu: task ntp_sync chờ lâu hơn (không thức mỗi giây) */
void UTIL_SetLowPower(int on);

#endif /* APP_UTILS_H */
