/**
 * @file app_lxlt_rules.h
 * @brief Module Quy tắc Lái xe liên tục LXLT / Phiên làm việc (Bản tin !NASA,5) -- Pure C Logic
 */
#ifndef APP_LXLT_RULES_H
#define APP_LXLT_RULES_H

#include <stdint.h>
#include <stddef.h>
#include "app_gps.h"

#define WORK_LXLT_LIMIT_MIN      240 /* 4 tiếng = 240 phút */
#define WORK_LOGOUT_STOP_SEC     900 /* Dừng 15 phút thì tự động đăng xuất */
#define WORK_PERIODIC_SEC        60  /* Báo 5-2 mỗi 60 giây khi đang trong phiên */

typedef enum {
    LXLT_EVENT_NONE = 0,
    LXLT_EVENT_SEND_TYPE1 = 1, /* Bắt đầu phiên làm việc (5-1) */
    LXLT_EVENT_SEND_TYPE2 = 2, /* Báo phiên làm việc định kỳ (5-2) */
    LXLT_EVENT_SEND_TYPE3 = 3  /* Kết thúc phiên làm việc (5-3) */
} LxltEventType_t;

typedef struct {
    LxltEventType_t type;
    char            dateTime[40];
    double          lat;
    double          lon;
} LxltEvent_t;

typedef struct {
    int      workActive;
    char     workLoginDT[40];
    double   workLoginLat;
    double   workLoginLon;
    uint32_t workLoginTick;
    uint32_t workLastReportTick;
    int      workStopPending;
    uint32_t workStopStartTick;
    int      workWasMoving;
    int      driveMinToday;       /* Tổng số phút lái xe tích lũy trong ngày (LXTN) */
    uint32_t driveAccumSecToday;
    int      lxltOver4hCount;    /* Số lần vi phạm lái xe liên tục quá 4h trong ngày */
    int      lxltCounted4h;
    double   workOdomStartKm;
    uint32_t workLastAccumTick;
} LxltState_t;

void LxltRules_Init(LxltState_t *state);
void LxltRules_ResetDailyCount(LxltState_t *state);

int  LxltRules_WorkLxltMinutes(const LxltState_t *state, uint32_t nowTick, uint32_t ticksPerSec);
int  LxltRules_WorkLxltDistMeters(const LxltState_t *state, double currentTotalKm);
void LxltRules_WorkAccumDriveTime(LxltState_t *state, uint32_t nowTick, uint32_t ticksPerSec);

void LxltRules_StartSession(LxltState_t *state, const char *dateTime, double lat, double lon,
                             double totalKm, uint32_t nowTick, int continuous, LxltEvent_t *outEvent);

void LxltRules_EndSession(LxltState_t *state, const char *dateTime, double lat, double lon,
                           uint32_t nowTick, uint32_t ticksPerSec, LxltEvent_t *outEvent);

/**
 * @brief Xử lý quy tắc phiên làm việc & kiểm tra vi phạm LXLT > 4h
 * @return 1 nếu có sự kiện LXLT phát sinh (outEvent), 0 nếu không
 */
int LxltRules_Process(LxltState_t *state, const GpsSnapshot_t *gps,
                      int isMoving, uint32_t nowTick, uint32_t ticksPerSec,
                      const char *dateTime, LxltEvent_t *outEvent);

void BuildWorkFramePayload(char *out, size_t size, const char *msgId,
                            const char *dateTime, int msgType,
                            const char *driverName, const char *driverLicense,
                            const LxltState_t *state, uint32_t nowTick, uint32_t ticksPerSec,
                            double currentTotalKm, const char *logoutDT,
                            double logoutLat, double logoutLon);

#endif /* APP_LXLT_RULES_H */
