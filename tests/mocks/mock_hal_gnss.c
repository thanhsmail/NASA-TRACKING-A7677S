/**
 * @file mock_hal_gnss.c
 * @brief Mock HAL GNSS implementation for Host Unit Testing
 */
#include "hal/hal_gnss.h"
#include <string.h>

static HalDateTime_t s_mockRtc = {
    .tm_year = 2026,
    .tm_mon  = 7,
    .tm_mday = 31,
    .tm_hour = 10,
    .tm_min  = 0,
    .tm_sec  = 0
};

void Mock_HAL_GNSS_SetRtc(int year, int mon, int mday, int hour, int min, int sec)
{
    s_mockRtc.tm_year = year;
    s_mockRtc.tm_mon  = mon;
    s_mockRtc.tm_mday = mday;
    s_mockRtc.tm_hour = hour;
    s_mockRtc.tm_min  = min;
    s_mockRtc.tm_sec  = sec;
}

int HAL_GNSS_GetRtc(HalDateTime_t *dt)
{
    if (!dt) return -1;
    *dt = s_mockRtc;
    return 0;
}

int HAL_GNSS_SetRtc(const HalDateTime_t *dt)
{
    if (!dt) return -1;
    s_mockRtc = *dt;
    return 0;
}

int HAL_GNSS_PowerOn(void) { return 0; }
int HAL_GNSS_PowerOff(void) { return 0; }
int HAL_GNSS_IsPowerOn(void) { return 1; }
int HAL_GNSS_UrcListenerStart(HalMsgQRef_t q) { (void)q; return 0; }
