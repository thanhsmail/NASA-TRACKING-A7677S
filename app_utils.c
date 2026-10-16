/**
 * @file app_utils.c
 * @brief Utilities & Helpers -- NASA Tracking
 *
 * Luu y: File nay KHONG #include simcom_api.h / simcom_common.h truc tiep.
 * Moi tuong tac phan cung di qua HAL layer.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_gnss.h"
#include "hal/hal_net.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HalTaskRef_t s_ntpSyncTaskRef = NULL;
static uint8_t s_ntpSyncTaskStack[1024 * 3];
static volatile int s_ntpSyncRequested = 0;

uint32_t Buffer_GetChecksum(const uint8_t *buffer, uint32_t length)
{
    uint32_t checksum = 0;
    uint32_t i;
    for (i = 0; i < length; i++)
    {
        checksum += buffer[i];
    }
    return checksum;
}

uint32_t GetTickNow(void)
{
    return HAL_OS_GetTick();
}

/** Số ngày trong tháng (có năm nhuận); tháng sai trả 30 */
static int DaysInMonth(int year, int mon)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon < 1 || mon > 12) return 30;
    if (mon == 2 && (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0)) return 29;
    return days[mon - 1];
}

void UTIL_AddSecondsToDateTime(HalDateTime_t *t, int32_t sec)
{
    /* Tính trên int: các trường HalDateTime_t là số không dấu nên không giảm
     * xuống dưới 0 được (bản cũ trong app_gps.c sai khi sec âm) */
    int64_t total;
    int year, mon, day;
    int32_t secOfDay;

    if (t == NULL) return;
    year = t->tm_year;
    mon  = t->tm_mon;
    day  = t->tm_mday;
    total = (int64_t)t->tm_hour * 3600 + (int64_t)t->tm_min * 60 + t->tm_sec + sec;

    while (total < 0) {
        total += 86400;
        if (--day < 1) {
            if (--mon < 1) { mon = 12; year--; }
            day = DaysInMonth(year, mon);
        }
    }
    while (total >= 86400) {
        total -= 86400;
        if (++day > DaysInMonth(year, mon)) {
            day = 1;
            if (++mon > 12) { mon = 1; year++; }
        }
    }

    secOfDay = (int32_t)total;
    t->tm_year = (uint16_t)year;
    t->tm_mon  = (uint8_t)mon;
    t->tm_mday = (uint8_t)day;
    t->tm_hour = (uint8_t)(secOfDay / 3600);
    t->tm_min  = (uint8_t)((secOfDay / 60) % 60);
    t->tm_sec  = (uint8_t)(secOfDay % 60);
}

int IsDigitChar(char c)
{
    return (c >= '0') && (c <= '9');
}

int Parse2Digits(const char *s)
{
    return (s[0] - '0') * 10 + (s[1] - '0');
}

int ParseDateTimeFromUrcTokens(const char *dateToken, const char *timeToken, HalDateTime_t *out)
{
    int day, mon, yy, hh, mm, ss, i;
    if ((dateToken == NULL) || (timeToken == NULL) || (out == NULL))
    {
        return 0;
    }
    for (i = 0; i < 6; i++)
    {
        if (!IsDigitChar(dateToken[i]) || !IsDigitChar(timeToken[i]))
        {
            return 0;
        }
    }
    day = Parse2Digits(dateToken);
    mon = Parse2Digits(dateToken + 2);
    yy  = Parse2Digits(dateToken + 4);
    hh  = Parse2Digits(timeToken);
    mm  = Parse2Digits(timeToken + 2);
    ss  = Parse2Digits(timeToken + 4);
    if ((day < 1) || (day > 31) ||
        (mon < 1) || (mon > 12) ||
        (hh > 23) || (mm > 59) || (ss > 59))
    {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->tm_year = 2000 + yy;
    out->tm_mon  = mon;
    out->tm_mday = day;
    out->tm_hour = hh;
    out->tm_min  = mm;
    out->tm_sec  = ss;
    return 1;
}

void BuildDateTimeAutoFallback(char *dateTime, uint32_t dateTimeSize)
{
    HalDateTime_t rtc;
    if ((dateTime == NULL) || (dateTimeSize == 0))
    {
        return;
    }
    HAL_GNSS_GetRtc(&rtc);
    if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100)
    {
        (void)snprintf(dateTime, dateTimeSize, "%04d-%02d-%02d %02d:%02d:%02d",
                       rtc.tm_year, rtc.tm_mon, rtc.tm_mday, rtc.tm_hour, rtc.tm_min, rtc.tm_sec);
        return;
    }
    /* RTC chưa sync: lấy giờ từ GNSS (UTC + offset) */
    if (GPS_FormatLocalDateTime(dateTime, dateTimeSize))
    {
        return;
    }
    (void)snprintf(dateTime, dateTimeSize, "0000-00-00 00:00:00");
}

static void sTask_NtpSync(void *argv)
{
    (void)argv;
    HAL_LOG("[NTP] Task started.");

    for (;;)
    {
        if (!s_ntpSyncRequested)
        {
            HAL_OS_TaskSleep(HAL_TICKS_PER_SEC);
            continue;
        }
        s_ntpSyncRequested = 0;

        {
            HalDateTime_t rtc;
            HAL_GNSS_GetRtc(&rtc);
            if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100)
            {
                HAL_LOG("[NTP] Time already valid (%d-%d-%d), no sync needed.",
                        rtc.tm_year, rtc.tm_mon, rtc.tm_mday);
                continue;
            }
        }

        HAL_LOG("[NTP] Requesting NTP sync via network HAL...");
        /* Sync time via network HAL */
    }
}

void TriggerNtpSyncIfNeeded(void)
{
    HalDateTime_t rtc;
    HAL_GNSS_GetRtc(&rtc);
    if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100)
    {
        return;
    }

    if (s_ntpSyncTaskRef == NULL)
    {
        if (HAL_OS_TaskCreate(&s_ntpSyncTaskRef, s_ntpSyncTaskStack, sizeof(s_ntpSyncTaskStack),
                             130, "ntp_sync", sTask_NtpSync, NULL) == 0)
        {
            HAL_LOG("[NTP] NTP sync task created.");
        }
        else
        {
            HAL_LOG("[NTP] NTP sync task creation failed.");
            s_ntpSyncTaskRef = NULL;
            return;
        }
    }
    s_ntpSyncRequested = 1;
}

void NitzEnableFromNetwork(void)
{
    HAL_NET_NitzEnable();
}
