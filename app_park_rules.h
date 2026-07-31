/**
 * @file app_park_rules.h
 * @brief Module Quy tắc đỗ xe (Bản tin !NASA,6) -- Pure C Logic
 */
#ifndef APP_PARK_RULES_H
#define APP_PARK_RULES_H

#include <stdint.h>
#include <stddef.h>
#include "app_gps.h"

#define PARK_REPORT_PERIOD_SEC_DEFAULT 60
#define PARK_FRAME_GUARD_SEC           5

typedef enum {
    PARK_EVENT_NONE = 0,
    PARK_EVENT_SEND_TYPE1 = 1, /* Bắt đầu đỗ (6-1) */
    PARK_EVENT_SEND_TYPE2 = 2, /* Báo đỗ định kỳ (6-2) */
    PARK_EVENT_SEND_TYPE3 = 3  /* Bắt đầu di chuyển lại (6-3) */
} ParkEventType_t;

typedef struct {
    ParkEventType_t type;
    char            dateTime[40];
    double          lat;
    double          lon;
    uint32_t        stopSec;
    int             dailyCount;
} ParkEvent_t;

typedef struct {
    int      isParkStopped;
    uint32_t parkStartTick;
    char     parkStartDateTime[40];
    double   parkLat;
    double   parkLon;
    uint32_t parkLastReportTick;
    int      parkDailyCount;
    uint32_t park61Tick;
    int      pendingSend63;
} ParkState_t;

void ParkRules_Init(ParkState_t *state);
void ParkRules_ResetDailyCount(ParkState_t *state);

/**
 * @brief Xử lý quy tắc đỗ xe định kỳ & chuyển trạng thái
 * @return 1 nếu có sự kiện đỗ xe phát sinh (outEvent), 0 nếu không
 */
int ParkRules_Process(ParkState_t *state, const GpsSnapshot_t *gps,
                      int isParked, int isMoving,
                      uint32_t nowTick, uint32_t ticksPerSec,
                      int parkConfirmSec, const char *dateTime,
                      ParkEvent_t *outEvent);

void BuildParkingFramePayload(char *out, size_t size, const char *msgId,
                               const char *dateTime, int msgType,
                               const char *endDateTime, const char *driverName,
                               const char *driverLicense, const char *parkStartDateTime,
                               double lat, double lon, uint32_t stopSec, int dailyCount);

#endif /* APP_PARK_RULES_H */
