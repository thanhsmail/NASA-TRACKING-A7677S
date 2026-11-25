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

uint32_t UTIL_DateTimeToSec(const HalDateTime_t *t)
{
    int32_t y, m, d, yoe, doy, doe, days;

    if (t == NULL || t->tm_year < 2020 || t->tm_year > 2100) return 0;
    if (t->tm_mon < 1 || t->tm_mon > 12 || t->tm_mday < 1 || t->tm_mday > 31) return 0;
    if (t->tm_hour > 23 || t->tm_min > 59 || t->tm_sec > 59) return 0;

    /* days_from_civil (Howard Hinnant), đủ cho năm 2020..2100 (một era 400 năm) */
    y = (int32_t)t->tm_year - ((t->tm_mon <= 2) ? 1 : 0);
    m = (int32_t)t->tm_mon;
    d = (int32_t)t->tm_mday;
    yoe = y - 2000;                                    /* era bắt đầu 2000-03-01 */
    doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = doe + 60;                                   /* 2000-01-01 -> 2000-03-01 = 60 ngày */
    return (uint32_t)days * 86400u + (uint32_t)t->tm_hour * 3600u +
           (uint32_t)t->tm_min * 60u + (uint32_t)t->tm_sec;
}

int UTIL_ParseDateTime(const char *s, HalDateTime_t *out)
{
    int y, mo, d, h, mi, se;
    char tail;

    if (s == NULL || out == NULL) return -1;
    if (sscanf(s, "%4d-%2d-%2d %2d:%2d:%2d%c", &y, &mo, &d, &h, &mi, &se, &tail) != 6) return -1;
    memset(out, 0, sizeof(*out));
    out->tm_year = (uint16_t)y;
    out->tm_mon = (uint8_t)mo;
    out->tm_mday = (uint8_t)d;
    out->tm_hour = (uint8_t)h;
    out->tm_min = (uint8_t)mi;
    out->tm_sec = (uint8_t)se;
    return (UTIL_DateTimeToSec(out) != 0) ? 0 : -1;
}

int UTIL_CorrectRtc(const HalDateTime_t *local, const char *src)
{
    HalDateTime_t rtc;
    uint32_t want, have, diff;

    want = UTIL_DateTimeToSec(local);
    if (want == 0) return 0;
    memset(&rtc, 0, sizeof(rtc));
    have = (HAL_GNSS_GetRtc(&rtc) == 0) ? UTIL_DateTimeToSec(&rtc) : 0;
    if (have != 0) {
        diff = (have > want) ? (have - want) : (want - have);
        if (diff <= (uint32_t)RTC_MAX_DRIFT_SEC) return 0;
    }
    if (HAL_GNSS_SetRtc(local) != 0) return 0;
    if (have == 0) {
        HAL_LOG("[RTC] set from %s: %04d-%02d-%02d %02d:%02d:%02d (RTC had no time)", src,
                local->tm_year, local->tm_mon, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
    } else {
        HAL_LOG("[RTC] corrected from %s: %04d-%02d-%02d %02d:%02d:%02d -> %04d-%02d-%02d %02d:%02d:%02d", src,
                rtc.tm_year, rtc.tm_mon, rtc.tm_mday, rtc.tm_hour, rtc.tm_min, rtc.tm_sec,
                local->tm_year, local->tm_mon, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
    }
    return 1;
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

static volatile int s_utilLowPower = 0;

void UTIL_SetLowPower(int on)
{
    s_utilLowPower = on ? 1 : 0;
}

static void sTask_NtpSync(void *argv)
{
    (void)argv;
    HAL_LOG("[NTP] Task started.");

    for (;;)
    {
        if (!s_ntpSyncRequested)
        {
            /* Ngủ sâu: không thức mỗi giây */
            HAL_OS_TaskSleep(s_utilLowPower ? SLEEP_IDLE_TASK_SEC * HAL_TICKS_PER_SEC : HAL_TICKS_PER_SEC);
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
