/**
 * @file test_park_rules.c
 * @brief Unit Tests cho Module Quy tắc đỗ xe (app_park_rules)
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "app_park_rules.h"

static void test_ParkRules_BasicTransition(void)
{
    ParkState_t state;
    GpsSnapshot_t gps = { .lat = 10.751293, .lon = 106.698273, .speedKph = 0.0, .valid = 1 };
    ParkEvent_t evt;
    uint32_t nowTick = 1000;
    uint32_t ticksPerSec = 200;
    int ret;

    printf("[TEST] Running test_ParkRules_BasicTransition...\n");
    ParkRules_Init(&state);

    /* 1. Trạng thái ban đầu: chưa đỗ xe */
    assert(state.isParkStopped == 0);
    assert(state.parkDailyCount == 0);

    /* 2. Xe dừng đỗ (isParked = 1, isMoving = 0) -> Tạo Event 6-1 (Bắt đầu đỗ) */
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick, ticksPerSec, 60, "2026-07-31 10:00:00", &evt);
    assert(ret == 1);
    assert(evt.type == PARK_EVENT_SEND_TYPE1);
    assert(evt.dailyCount == 1);
    assert(state.isParkStopped == 1);
    assert(strcmp(state.parkStartDateTime, "2026-07-31 10:00:00") == 0);

    /* 3. Sau 30s: chưa đến chu kỳ 60s -> PARK_EVENT_NONE */
    nowTick += 30 * ticksPerSec;
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick, ticksPerSec, 60, "2026-07-31 10:00:30", &evt);
    assert(ret == 0);
    assert(evt.type == PARK_EVENT_NONE);

    /* 4. Sau 60s (tức t = 60s kể từ lúc đỗ) -> Tạo Event 6-2 (Báo đỗ định kỳ) */
    nowTick += 30 * ticksPerSec; /* Tổng trôi qua 60s */
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick, ticksPerSec, 60, "2026-07-31 10:01:00", &evt);
    assert(ret == 1);
    assert(evt.type == PARK_EVENT_SEND_TYPE2);
    assert(evt.stopSec == 60);

    /* 5. Xe bắt đầu di chuyển lại (isParked = 0, isMoving = 1) -> Tạo Event 6-3 (Di chuyển lại) */
    nowTick += 10 * ticksPerSec; /* Giờ là t = 70s, đã > 5s guard kể từ 6-1 */
    gps.speedKph = 25.0;
    ret = ParkRules_Process(&state, &gps, 0, 1, nowTick, ticksPerSec, 60, "2026-07-31 10:01:10", &evt);
    assert(ret == 1);
    assert(evt.type == PARK_EVENT_SEND_TYPE3);
    assert(state.isParkStopped == 0);

    printf("[TEST] PASS: test_ParkRules_BasicTransition\n");
}

/* Đang ngủ: 6-2 không theo chu kỳ, chỉ ra khi được yêu cầu (lần thức định kỳ) */
static void test_ParkRules_HoldPeriodicWhileSleeping(void)
{
    ParkState_t state;
    GpsSnapshot_t gps = { .lat = 10.751293, .lon = 106.698273, .speedKph = 0.0, .valid = 1 };
    ParkEvent_t evt;
    uint32_t nowTick = 1000;
    uint32_t ticksPerSec = 200;
    int ret;

    printf("[TEST] Running test_ParkRules_HoldPeriodicWhileSleeping...
");
    ParkRules_Init(&state);
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick, ticksPerSec, 60, "2026-07-31 10:00:00", &evt);
    assert(ret == 1 && evt.type == PARK_EVENT_SEND_TYPE1);

    /* Ngủ: quá nhiều chu kỳ 60 s vẫn không có 6-2 */
    state.holdPeriodic = 1;
    nowTick += 600 * ticksPerSec;
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick, ticksPerSec, 60, "2026-07-31 10:10:00", &evt);
    assert(ret == 0);

    /* Lần thức định kỳ: đúng một 6-2, số giây đỗ tính từ lúc bắt đầu đỗ */
    ParkRules_RequestReport(&state);
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick, ticksPerSec, 60, "2026-07-31 10:10:00", &evt);
    assert(ret == 1 && evt.type == PARK_EVENT_SEND_TYPE2);
    assert(evt.stopSec == 600);
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick + ticksPerSec, ticksPerSec, 60, "2026-07-31 10:10:01", &evt);
    assert(ret == 0);

    /* Thức hẳn: chu kỳ 60 s chạy lại */
    state.holdPeriodic = 0;
    nowTick += 60 * ticksPerSec;
    ret = ParkRules_Process(&state, &gps, 1, 0, nowTick, ticksPerSec, 60, "2026-07-31 10:11:00", &evt);
    assert(ret == 1 && evt.type == PARK_EVENT_SEND_TYPE2);
    assert(evt.stopSec == 660);

    printf("[TEST] PASS: test_ParkRules_HoldPeriodicWhileSleeping
");
}

int main(void)
{
    printf("=========================================\n");
    printf("   PARK RULES UNIT TEST SUITE (PURE C)  \n");
    printf("=========================================\n");

    test_ParkRules_BasicTransition();
    test_ParkRules_HoldPeriodicWhileSleeping();

    printf("ALL PARK RULES TESTS PASSED!\n");
    return 0;
}
