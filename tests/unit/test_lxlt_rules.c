/**
 * @file test_lxlt_rules.c
 * @brief Unit Tests cho Module Quy tắc Lái xe liên tục LXLT / Phiên làm việc (app_lxlt_rules)
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "app_lxlt_rules.h"

static void test_LxltRules_BasicFlow(void)
{
    LxltState_t state;
    GpsSnapshot_t gps = { .lat = 10.751293, .lon = 106.698273, .speedKph = 35.0, .totalKm = 10.0, .valid = 1 };
    LxltEvent_t evt;
    uint32_t nowTick = 1000;
    uint32_t ticksPerSec = 200;
    int ret;

    printf("[TEST] Running test_LxltRules_BasicFlow...\n");
    LxltRules_Init(&state);

    /* 1. Xe bắt đầu di chuyển (chưa có phiên) -> Khởi tạo phiên 5-1 */
    ret = LxltRules_Process(&state, &gps, 1 /* isMoving */, nowTick, ticksPerSec, "2026-07-31 08:00:00", &evt);
    assert(ret == 1);
    assert(evt.type == LXLT_EVENT_SEND_TYPE1);
    assert(state.workActive == 1);
    assert(strcmp(state.workLoginDT, "2026-07-31 08:00:00") == 0);

    /* 2. Sau 60s di chuyển -> Báo 5-2 định kỳ */
    nowTick += 60 * ticksPerSec;
    gps.totalKm += 0.5;
    ret = LxltRules_Process(&state, &gps, 1, nowTick, ticksPerSec, "2026-07-31 08:01:00", &evt);
    assert(ret == 1);
    assert(evt.type == LXLT_EVENT_SEND_TYPE2);

    /* 3. Giả lập lái xe liên tục > 4 tiếng (241 phút = 14460 giây) -> Kiểm tra cờ vi phạm LXLT */
    nowTick += (240 * 60) * ticksPerSec;
    ret = LxltRules_Process(&state, &gps, 1, nowTick, ticksPerSec, "2026-07-31 12:01:00", &evt);
    assert(state.lxltOver4hCount == 1);
    assert(state.lxltCounted4h == 1);

    /* 4. Dừng xe liên tục 15 phút (900s) -> Tự động đăng xuất phiên (5-3) */
    gps.speedKph = 0.0;
    nowTick += 901 * ticksPerSec;
    ret = LxltRules_Process(&state, &gps, 0 /* isMoving = 0 */, nowTick, ticksPerSec, "2026-07-31 12:16:01", &evt);
    assert(ret == 1);
    assert(evt.type == LXLT_EVENT_SEND_TYPE3);
    assert(state.workActive == 0);

    printf("[TEST] PASS: test_LxltRules_BasicFlow\n");
}

/* Phiên lái chạy qua 0 giờ: km ngày về 0 nhưng quãng đường phiên giữ phần trước 0 giờ */
static void test_LxltRules_DriveAcrossMidnight(void)
{
    LxltState_t state;
    GpsSnapshot_t gps = { .lat = 10.75, .lon = 106.69, .speedKph = 40.0, .totalKm = 120.0, .valid = 1 };
    LxltEvent_t evt;
    uint32_t nowTick = 1000;
    uint32_t ticksPerSec = 200;

    printf("[TEST] Running test_LxltRules_DriveAcrossMidnight...\n");
    LxltRules_Init(&state);

    /* Bắt đầu phiên lúc 23:50, km ngày đang là 120 */
    (void)LxltRules_Process(&state, &gps, 1, nowTick, ticksPerSec, "2026-11-22 23:50:00", &evt);
    assert(state.workActive == 1);

    /* Chạy 7,5 km tới 0 giờ */
    gps.totalKm = 127.5;
    assert(LxltRules_WorkLxltDistMeters(&state, gps.totalKm) == 7500);

    /* Sang ngày: km ngày về 0, phiên vẫn chạy */
    LxltRules_ResetDailyCount(&state, gps.totalKm);
    gps.totalKm = 0.0;
    assert(state.workActive == 1);
    assert(state.driveMinToday == 0);
    assert(LxltRules_WorkLxltDistMeters(&state, gps.totalKm) == 7500);

    /* Chạy tiếp 2 km sau 0 giờ: phiên = 9,5 km, km ngày = 2 km */
    gps.totalKm = 2.0;
    assert(LxltRules_WorkLxltDistMeters(&state, gps.totalKm) == 9500);

    /* Phiên mới trong ngày mới: tính từ km ngày lúc bắt đầu */
    LxltRules_Init(&state);
    (void)LxltRules_Process(&state, &gps, 1, nowTick, ticksPerSec, "2026-11-23 06:00:00", &evt);
    gps.totalKm = 3.0;
    assert(LxltRules_WorkLxltDistMeters(&state, gps.totalKm) == 1000);

    /* Không có phiên lúc 0 giờ, km ngày 0: không đổi mốc */
    LxltRules_Init(&state);
    LxltRules_ResetDailyCount(&state, 0.0);
    assert(state.workOdomStartKm == 0.0);

    printf("[TEST] PASS: test_LxltRules_DriveAcrossMidnight\n");
}

int main(void)
{
    printf("=========================================\n");
    printf("   LXLT RULES UNIT TEST SUITE (PURE C)  \n");
    printf("=========================================\n");

    test_LxltRules_BasicFlow();
    test_LxltRules_DriveAcrossMidnight();

    printf("ALL LXLT RULES TESTS PASSED!\n");
    return 0;
}
