/**
 * @file    port_entry.c
 * @brief   Điểm vào ứng dụng của SIMCom OpenSDK (A7677S)
 *
 * Kernel tìm bảng userSpaceEntry trong section .userSpaceRegTable rồi tạo task
 * userSpaceMain chạy userSpace_Main(). Ở đây chỉ nạp bảng hàm SDK (ApiMapInit)
 * rồi chuyển sang APP_Main() của ứng dụng.
 *
 * Khi nâng SDK: đối chiếu userSpaceEntry_t và ApiMapInit trong sc_lib/<module>/inc/api_map.h
 * (và mẫu sc_application.c của SDK mới) với file này.
 */

#include <stddef.h>

#include "api_map.h"
#include "app_main.h"

#define USERSPACE_REGTABLE_ATTR __attribute__((unused, section(".userSpaceRegTable")))
#define USERSPACE_MAIN_STACK    (1024 * 10)
#define USERSPACE_MAIN_PRIO     30

#ifndef APP_VERSION
#define APP_VERSION ""
#endif

void userSpace_Main(void *arg)
{
    ApiMapInit(arg);
    APP_Main();
}

userSpaceEntry_t userSpaceEntry USERSPACE_REGTABLE_ATTR = {
    NULL, NULL, USERSPACE_MAIN_STACK, USERSPACE_MAIN_PRIO, "userSpaceMain", userSpace_Main, APP_VERSION
};
