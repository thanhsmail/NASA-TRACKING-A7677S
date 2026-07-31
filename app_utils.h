#ifndef APP_UTILS_H
#define APP_UTILS_H

#include "hal/hal_gnss.h"
#include <stdint.h>

uint32_t Buffer_GetChecksum(const uint8_t *buffer, uint32_t length);
uint32_t GetTickNow(void);
int IsDigitChar(char c);
int Parse2Digits(const char *s);
int ParseDateTimeFromUrcTokens(const char *dateToken, const char *timeToken, HalDateTime_t *out);
void BuildDateTimeAutoFallback(char *dateTime, uint32_t dateTimeSize);
void TriggerNtpSyncIfNeeded(void);
void NitzEnableFromNetwork(void);

#endif /* APP_UTILS_H */
