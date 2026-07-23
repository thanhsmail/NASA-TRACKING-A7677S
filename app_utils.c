#include "simcom_api.h"
#include "simcom_common.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sTaskRef s_ntpSyncTaskRef = NULL;
static UINT8 s_ntpSyncTaskStack[1024 * 3];
static int s_nitzEnabled = 0;
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
    return (uint32_t)sAPI_GetTicks();
}

int IsDigitChar(char c)
{
    return (c >= '0') && (c <= '9');
}

int Parse2Digits(const char *s)
{
    return (s[0] - '0') * 10 + (s[1] - '0');
}

int ParseDateTimeFromUrcTokens(const char *dateToken, const char *timeToken, SCsysTime_t *out)
{
    int day;
    int mon;
    int yy;
    int hh;
    int mm;
    int ss;
    int i;
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
    yy = Parse2Digits(dateToken + 4);
    hh = Parse2Digits(timeToken);
    mm = Parse2Digits(timeToken + 2);
    ss = Parse2Digits(timeToken + 4);
    if ((day < 1) || (day > 31) ||
        (mon < 1) || (mon > 12) ||
        (hh > 23) || (mm > 59) || (ss > 59))
    {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->tm_year = 2000 + yy;
    out->tm_mon = mon;
    out->tm_mday = day;
    out->tm_hour = hh;
    out->tm_min = mm;
    out->tm_sec = ss;
    return 1;
}

void BuildDateTimeAutoFallback(char *dateTime, uint32_t dateTimeSize)
{
    t_rtc rtc;
    if ((dateTime == NULL) || (dateTimeSize == 0))
    {
        return;
    }
    sAPI_GetRealTimeClock(&rtc);
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

/* Thực hiện một đợt sync NTP (tối đa NASA_NTP_MAX_ATTEMPTS lần thử) */
static void NtpSyncOnce(sMsgQRef ntp_msgq)
{
    int attempt = 0;
    while (attempt < NASA_NTP_MAX_ATTEMPTS)
    {
        attempt++;
        sAPI_Debug("[NTP] Sync attempt %d/%d...", attempt, NASA_NTP_MAX_ATTEMPTS);

        sAPI_NtpUpdate(SC_NTP_OP_SET, NASA_NTP_SERVER, NASA_NTP_TIMEZONE_PARAM, NULL);
        sAPI_NtpUpdate(SC_NTP_OP_EXC, NULL, 0, ntp_msgq);

        SIM_MSG_T ntp_result = {SC_SRV_NONE, -1, 0, NULL};
        if (sAPI_MsgQRecv(ntp_msgq, &ntp_result, 15000) == SC_SUCCESS)
        {
            if (ntp_result.msg_id == SC_SRV_NTP && ntp_result.arg1 == SC_NTP_OK)
            {
                sAPI_Debug("[NTP] Sync success!");
                if (ntp_result.arg3)
                {
                    sAPI_Free(ntp_result.arg3);
                }
                return;
            }
            sAPI_Debug("[NTP] Sync failed, error code: %d", (int)ntp_result.arg1);
            if (ntp_result.arg3)
            {
                sAPI_Free(ntp_result.arg3);
            }
        }
        else
        {
            sAPI_Debug("[NTP] Sync timeout.");
        }

        sAPI_TaskSleep(NASA_NTP_RETRY_INTERVAL);
    }
}

/*
 * Task NTP thường trú: chờ cờ yêu cầu thay vì tự xoá task (self-delete trên
 * stack tĩnh có cửa sổ race khi TriggerNtpSyncIfNeeded tạo lại task quá sớm).
 */
static void sTask_NtpSync(void *argv)
{
    sMsgQRef ntp_msgq = NULL;
    (void)argv;
    sAPI_Debug("[NTP] Task started.");

    while (sAPI_MsgQCreate(&ntp_msgq, "ntp_sync_q", sizeof(SIM_MSG_T), 4, SC_FIFO) != SC_SUCCESS)
    {
        sAPI_Debug("[NTP] Create MsgQ failed, retry...");
        sAPI_TaskSleep(10 * SC_TICKS_PER_SECOND);
    }

    for (;;)
    {
        if (!s_ntpSyncRequested)
        {
            sAPI_TaskSleep(SC_TICKS_PER_SECOND);
            continue;
        }
        s_ntpSyncRequested = 0;

        {
            t_rtc rtc;
            sAPI_GetRealTimeClock(&rtc);
            if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100)
            {
                sAPI_Debug("[NTP] Time already valid (%d-%d-%d), no sync needed.",
                           rtc.tm_year, rtc.tm_mon, rtc.tm_mday);
                continue;
            }
        }

        NtpSyncOnce(ntp_msgq);
    }
}

void TriggerNtpSyncIfNeeded(void)
{
    t_rtc rtc;
    sAPI_GetRealTimeClock(&rtc);
    if (rtc.tm_year >= 2020 && rtc.tm_year <= 2100)
    {
        return;
    }

    if (s_ntpSyncTaskRef == NULL)
    {
        if (sAPI_TaskCreate(&s_ntpSyncTaskRef, s_ntpSyncTaskStack, sizeof(s_ntpSyncTaskStack),
                            130, (char *)"ntp_sync", sTask_NtpSync, NULL) == SC_SUCCESS)
        {
            sAPI_Debug("[NTP] NTP sync task created.");
        }
        else
        {
            sAPI_Debug("[NTP] NTP sync task creation failed.");
            s_ntpSyncTaskRef = NULL;
            return;
        }
    }
    s_ntpSyncRequested = 1;
}

void NitzEnableFromNetwork(void)
{
    unsigned int ret;
    if (s_nitzEnabled)
    {
        return;
    }
    ret = sAPI_NetworkSetCtzu(NASA_NITZ_ENABLE_VALUE);
    sAPI_Debug("[NITZ] sAPI_NetworkSetCtzu(%d) ret=%u", NASA_NITZ_ENABLE_VALUE, ret);
    if (ret == 0)
    {
        s_nitzEnabled = 1;
    }
}
