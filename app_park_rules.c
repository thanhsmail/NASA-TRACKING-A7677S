/**
 * @file app_park_rules.c
 * @brief Logic quy tắc đỗ xe (Bản tin !NASA,6) -- Pure C Implementation
 */
#include "app_park_rules.h"
#include "app_utils.h"
#include <stdio.h>
#include <string.h>

void ParkRules_Init(ParkState_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

void ParkRules_ResetDailyCount(ParkState_t *state)
{
    if (!state) return;
    state->parkDailyCount = 0;
}

static void AppendChecksumAndEndLocal(char *out, size_t size, const char *payload)
{
    uint32_t checksum = Buffer_GetChecksum((const uint8_t *)payload, (uint32_t)strlen(payload));
    (void)snprintf(out, size, "%s*%lu\r", payload, (unsigned long)checksum);
}
// Xử lý tạo chuỗi dữ liệu đỗ xe
void BuildParkingFramePayload(char *out, size_t size, const char *msgId,
                               const char *dateTime, int msgType,
                               const char *endDateTime, const char *driverName,
                               const char *driverLicense, const char *parkStartDateTime,
                               double lat, double lon, uint32_t stopSec, int dailyCount)
{
    static char payload[360];
    const char *endStr = (endDateTime != NULL) ? endDateTime : "";
    const char *dName  = (driverName != NULL) ? driverName : "";
    const char *dLic   = (driverLicense != NULL) ? driverLicense : "";
    const char *pStart = (parkStartDateTime != NULL) ? parkStartDateTime : "";

    (void)snprintf(payload, sizeof(payload),
                   "!NASA,6,%s,%s,%s,%s,%s,%d,%s,%.6f,%.6f,%lu,%d,",
                   msgId, dateTime, dName, dLic,
                   pStart, msgType, endStr,
                   lat, lon, (unsigned long)stopSec, dailyCount);
    AppendChecksumAndEndLocal(out, size, payload);
}

// Xử lý logic đỗ xe
int ParkRules_Process(ParkState_t *state, const GpsSnapshot_t *gps,
                      int isParked, int isMoving,
                      uint32_t nowTick, uint32_t ticksPerSec,
                      int parkConfirmSec, const char *dateTime,
                      ParkEvent_t *outEvent)
{
    int parkPeriodSec;

    if (!state || !outEvent) return 0;
    memset(outEvent, 0, sizeof(*outEvent));

    if (ticksPerSec == 0) ticksPerSec = 1;
    parkPeriodSec = (parkConfirmSec <= 0) ? PARK_REPORT_PERIOD_SEC_DEFAULT : parkConfirmSec;

    /* 1. Xả bản tin 6-3 hoãn (pending 5s guard) nếu đã đủ thời gian guard */
    if (state->pendingSend63 && (nowTick - state->park61Tick) >= (uint32_t)(PARK_FRAME_GUARD_SEC * ticksPerSec)) {
        state->pendingSend63 = 0;
        outEvent->type = PARK_EVENT_SEND_TYPE3;
        outEvent->lat = state->parkLat;
        outEvent->lon = state->parkLon;
        outEvent->dailyCount = state->parkDailyCount;
        if (state->parkStartTick > 0 && nowTick >= state->parkStartTick) {
            outEvent->stopSec = (nowTick - state->parkStartTick) / ticksPerSec;
        }
        if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);
        return 1;
    }

    if (!state->isParkStopped) {
        if (isParked) {
            /* Nếu có 6-3 cũ đang chờ -> Xả 6-3 cũ trước */
            if (state->pendingSend63) {
                state->pendingSend63 = 0;
                outEvent->type = PARK_EVENT_SEND_TYPE3;
                outEvent->lat = state->parkLat;
                outEvent->lon = state->parkLon;
                outEvent->dailyCount = state->parkDailyCount;
                if (state->parkStartTick > 0 && nowTick >= state->parkStartTick) {
                    outEvent->stopSec = (nowTick - state->parkStartTick) / ticksPerSec;
                }
                if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);
                return 1;
            }

            state->isParkStopped = 1;
            state->parkStartTick = nowTick;
            state->parkLastReportTick = nowTick;
            state->parkDailyCount++;
            if (gps) {
                state->parkLat = gps->lat;
                state->parkLon = gps->lon;
            }
            if (dateTime) snprintf(state->parkStartDateTime, sizeof(state->parkStartDateTime), "%s", dateTime);
            state->park61Tick = nowTick;

            outEvent->type = PARK_EVENT_SEND_TYPE1;
            outEvent->lat = state->parkLat;
            outEvent->lon = state->parkLon;
            outEvent->stopSec = 0;
            outEvent->dailyCount = state->parkDailyCount;
            if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);
            return 1;
        }
    } else {
        if (!isParked && isMoving) {
            state->isParkStopped = 0;

            outEvent->type = PARK_EVENT_SEND_TYPE3;
            outEvent->lat = state->parkLat;
            outEvent->lon = state->parkLon;
            outEvent->dailyCount = state->parkDailyCount;
            if (state->parkStartTick > 0 && nowTick >= state->parkStartTick) {
                outEvent->stopSec = (nowTick - state->parkStartTick) / ticksPerSec;
            }
            if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);

            state->parkStartTick = 0;

            /* Guard 5s giữa 6-1 và 6-3 */
            if ((nowTick - state->park61Tick) >= (uint32_t)(PARK_FRAME_GUARD_SEC * ticksPerSec)) {
                state->pendingSend63 = 0;
            } else {
                state->pendingSend63 = 1; /* Hoãn gửi 6-3 cho tới khi đủ 5s */
                outEvent->type = PARK_EVENT_NONE; /* Chưa gửi ngay */
                return 0;
            }
            return 1;
        } else if (isParked) {
            if ((nowTick - state->parkLastReportTick) >= (uint32_t)(parkPeriodSec * ticksPerSec)) {
                state->parkLastReportTick = nowTick;
                outEvent->type = PARK_EVENT_SEND_TYPE2;
                outEvent->lat = state->parkLat;
                outEvent->lon = state->parkLon;
                outEvent->dailyCount = state->parkDailyCount;
                if (state->parkStartTick > 0 && nowTick >= state->parkStartTick) {
                    outEvent->stopSec = (nowTick - state->parkStartTick) / ticksPerSec;
                }
                if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);
                return 1;
            }
        }
    }

    return 0;
}
