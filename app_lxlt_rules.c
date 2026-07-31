/**
 * @file app_lxlt_rules.c
 * @brief Logic quy tắc Lái xe liên tục LXLT / Phiên làm việc (Bản tin !NASA,5) -- Pure C Implementation
 */
#include "app_lxlt_rules.h"
#include "app_utils.h"
#include <stdio.h>
#include <string.h>

void LxltRules_Init(LxltState_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
}
// Reset đếm số km và thời gian
void LxltRules_ResetDailyCount(LxltState_t *state)
{
    if (!state) return;
    state->driveMinToday       = 0;
    state->driveAccumSecToday  = 0;
    state->lxltOver4hCount     = 0;
    state->workOdomStartKm     = 0.0;
}
// Xử lý đếm số km và thời gian 
int LxltRules_WorkLxltMinutes(const LxltState_t *state, uint32_t nowTick, uint32_t ticksPerSec)
{
    if (!state || !state->workActive || state->workLoginTick == 0) return 0;
    if (ticksPerSec == 0) ticksPerSec = 1;
    if (nowTick < state->workLoginTick) return 0;
    return (int)((nowTick - state->workLoginTick) / (ticksPerSec * 60u));
}
// Xử lý đếm số km
int LxltRules_WorkLxltDistMeters(const LxltState_t *state, double currentTotalKm)
{
    double deltaKm;
    if (!state) return 0;
    deltaKm = currentTotalKm - state->workOdomStartKm;
    if (deltaKm < 0.0) deltaKm = 0.0;
    return (int)(deltaKm * 1000.0 + 0.5);
}

// Xử lý đếm thời gian 
void LxltRules_WorkAccumDriveTime(LxltState_t *state, uint32_t nowTick, uint32_t ticksPerSec)
{
    uint32_t elapsedSec;
    if (!state) return;
    if (ticksPerSec == 0) ticksPerSec = 1;

    if (!state->workActive) {
        state->workLastAccumTick = nowTick;
        return;
    }
    if (state->workLastAccumTick == 0) {
        state->workLastAccumTick = nowTick;
        return;
    }
    if (nowTick <= state->workLastAccumTick) return;
    elapsedSec = (nowTick - state->workLastAccumTick) / ticksPerSec;
    if (elapsedSec == 0) return;
    state->workLastAccumTick = nowTick;
    state->driveAccumSecToday += elapsedSec;
    state->driveMinToday = (int)(state->driveAccumSecToday / 60u);
}

// Xử lý bắt đầu phiên làm việc
void LxltRules_StartSession(LxltState_t *state, const char *dateTime, double lat, double lon,
                             double totalKm, uint32_t nowTick, int continuous, LxltEvent_t *outEvent)
{
    if (!state) return;

    if (!continuous) {
        if (dateTime) {
            strncpy(state->workLoginDT, dateTime, sizeof(state->workLoginDT) - 1);
            state->workLoginDT[sizeof(state->workLoginDT) - 1] = '\0';
        }
        state->workLoginLat    = lat;
        state->workLoginLon    = lon;
        state->workLoginTick   = nowTick;
        state->workOdomStartKm = totalKm;
        state->lxltCounted4h   = 0;
    }
    state->workActive        = 1;
    state->workStopPending   = 0;
    state->workLastReportTick = nowTick;
    state->workLastAccumTick  = nowTick;

    if (outEvent) {
        memset(outEvent, 0, sizeof(*outEvent));
        outEvent->type = LXLT_EVENT_SEND_TYPE1;
        outEvent->lat  = lat;
        outEvent->lon  = lon;
        if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);
    }
}
// Xử lý kết thúc phiên làm việc
void LxltRules_EndSession(LxltState_t *state, const char *dateTime, double lat, double lon,
                           uint32_t nowTick, uint32_t ticksPerSec, LxltEvent_t *outEvent)
{
    if (!state || !state->workActive) return;

    LxltRules_WorkAccumDriveTime(state, nowTick, ticksPerSec);

    if (outEvent) {
        memset(outEvent, 0, sizeof(*outEvent));
        outEvent->type = LXLT_EVENT_SEND_TYPE3;
        outEvent->lat  = lat;
        outEvent->lon  = lon;
        if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);
    }

    state->workActive      = 0;
    state->workStopPending = 0;
    state->workLoginTick   = 0;
    state->lxltCounted4h   = 0;
}
// Xử lý quy tắc Lái xe liên tục LXLT / Phiên làm việc
int LxltRules_Process(LxltState_t *state, const GpsSnapshot_t *gps,
                      int isMoving, uint32_t nowTick, uint32_t ticksPerSec,
                      const char *dateTime, LxltEvent_t *outEvent)
{
    int lxltMin;
    double lat = (gps != NULL) ? gps->lat : 0.0;
    double lon = (gps != NULL) ? gps->lon : 0.0;
    double totalKm = (gps != NULL) ? gps->totalKm : 0.0;

    if (!state || !outEvent) return 0;
    memset(outEvent, 0, sizeof(*outEvent));
    if (ticksPerSec == 0) ticksPerSec = 1;

    LxltRules_WorkAccumDriveTime(state, nowTick, ticksPerSec);

    if (state->workActive) {
        lxltMin = LxltRules_WorkLxltMinutes(state, nowTick, ticksPerSec);
        if (lxltMin >= WORK_LXLT_LIMIT_MIN && !state->lxltCounted4h) {
            state->lxltOver4hCount++;
            state->lxltCounted4h = 1;
        }

        if (!isMoving) {
            if (!state->workStopPending) {
                state->workStopPending = 1;
                state->workStopStartTick = nowTick;
            } else if ((nowTick - state->workStopStartTick) >=
                       (uint32_t)WORK_LOGOUT_STOP_SEC * ticksPerSec) {
                LxltRules_EndSession(state, dateTime, lat, lon, nowTick, ticksPerSec, outEvent);
                return 1;
            }
        } else {
            /* Đang di chuyển: reset cờ pending */
            state->workStopPending = 0;
        }

        /* Gửi bản tin 5-2 định kỳ mỗi 60s */
        if (state->workActive && (nowTick - state->workLastReportTick) >=
            (uint32_t)WORK_PERIODIC_SEC * ticksPerSec) {
            state->workLastReportTick = nowTick;
            outEvent->type = LXLT_EVENT_SEND_TYPE2;
            outEvent->lat  = lat;
            outEvent->lon  = lon;
            if (dateTime) strncpy(outEvent->dateTime, dateTime, sizeof(outEvent->dateTime) - 1);
            return 1;
        }
    } else {
        /* Cạnh dừng -> chạy: bắt đầu chuyến mới (5-1) */
        if (isMoving && !state->workWasMoving) {
            LxltRules_StartSession(state, dateTime, lat, lon, totalKm, nowTick, 0, outEvent);
            state->workWasMoving = isMoving;
            return 1;
        }
    }

    state->workWasMoving = isMoving;
    return 0;
}

// Xử lýchecksum và kết thúc
static void AppendChecksumAndEndLocal(char *out, size_t size, const char *payload)
{
    uint32_t checksum = Buffer_GetChecksum((const uint8_t *)payload, (uint32_t)strlen(payload));
    (void)snprintf(out, size, "%s*%lu\r", payload, (unsigned long)checksum);
}
// Xử lý tạo chuỗi dữ liệu phiên làm việc   
void BuildWorkFramePayload(char *out, size_t size, const char *msgId,
                            const char *dateTime, int msgType,
                            const char *driverName, const char *driverLicense,
                            const LxltState_t *state, uint32_t nowTick, uint32_t ticksPerSec,
                            double currentTotalKm, const char *logoutDT,
                            double logoutLat, double logoutLon)
{
    static char payload[420];
    int lxltMin = LxltRules_WorkLxltMinutes(state, nowTick, ticksPerSec);
    int distM   = LxltRules_WorkLxltDistMeters(state, currentTotalKm);
    const char *dName = (driverName != NULL) ? driverName : "";
    const char *dLic  = (driverLicense != NULL) ? driverLicense : "";
    const char *wLoginDT = (state != NULL) ? state->workLoginDT : "";
    double wLoginLat = (state != NULL) ? state->workLoginLat : 0.0;
    double wLoginLon = (state != NULL) ? state->workLoginLon : 0.0;
    int over4h = (state != NULL) ? state->lxltOver4hCount : 0;
    int driveMin = (state != NULL) ? state->driveMinToday : 0;

    payload[0] = '\0';
    if (msgType == 1) {
        (void)snprintf(payload, sizeof(payload),
                       "!NASA,5,%s,%s,%d,%s,%s,%s,%.6f,%.6f,%d,%d,,,%d,%d,",
                       msgId, dateTime, msgType,
                       dName, dLic,
                       wLoginDT, wLoginLat, wLoginLon,
                       lxltMin, driveMin, over4h, distM);
    } else {
        const char *outDt = (logoutDT != NULL) ? logoutDT : dateTime;
        (void)snprintf(payload, sizeof(payload),
                       "!NASA,5,%s,%s,%d,%s,%s,%s,%.6f,%.6f,%d,%d,%s,%.6f,%.6f,%d,%d,",
                       msgId, dateTime, msgType,
                       dName, dLic,
                       wLoginDT, wLoginLat, wLoginLon,
                       lxltMin, driveMin,
                       outDt, logoutLat, logoutLon,
                       over4h, distM);
    }
    AppendChecksumAndEndLocal(out, size, payload);
}
