/**
  ******************************************************************************
  * @file    sc_application.c
  * @brief   Main Application Entry Point -- NASA Tracking
  *
  * Luu y: File nay KHONG #include simcom_api.h / simcom_common.h.
  * Moi tuong tac phan cung di qua HAL layer.
  ******************************************************************************
  */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "api_map.h"

/* HAL layer */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_net.h"
#include "hal/hal_gnss.h"
#include "hal/hal_sms.h"

#include "drv_en25qh64a.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include "app_sms.h"
#include "app_cfg.h"
#include "app_nasa.h"
#include "app_indication.h"

/* --- SMS URC task --- */
static HalTaskRef_t gSmsRecvTask = NULL;
static uint8_t      gSmsRecvTaskStack[1024 * 8];
static HalSmsRspQ_t gSmsMsgQueue = NULL;
// Task nhận SMS
static void SmsReceiverTask(void *argv)
{
    HalSmsUrcEvent_t evt;
    (void)argv;

    HAL_LOG("SmsReceiverTask start");

    /* Tao URC queue noi bo */
    if (HAL_SMS_CreateUrcQueue(&gSmsMsgQueue) != 0) return;

    /* Doi SIM san sang va cau hinh CNMI */
    if (SMS_EnsureReady() != 0) {
        HAL_LOG("SmsReceiverTask: SMS modem config fail (continue)");
    }

    /* Dang ky nhan URC SMS */
    HAL_SMS_RegisterUrc(gSmsMsgQueue, HAL_SMS_URC_MASK_NEW | HAL_SMS_URC_MASK_FLASH);
    HAL_OS_TaskSleep(200);

    while (1) {
        if (HAL_SMS_RecvUrc(gSmsMsgQueue, &evt, HAL_TICKS_PER_SEC) != 0) continue;

        if (evt.type == HAL_SMS_URC_NEW_MSG && evt.index > 0) {
            char smsBuf[256] = {0};
            if (HAL_SMS_Read(evt.index, smsBuf, sizeof(smsBuf)) == 0) {
                SMS_ExtractAndExecuteSmsBody(smsBuf, "SMS");
            }
            HAL_SMS_Delete(evt.index);
        } else if (evt.type == HAL_SMS_URC_FLASH_MSG) {
            SMS_ExtractAndExecuteSmsBody(evt.body, "SMS_FLASH");
        }
    }
}

/* --- NASA reporter task --- */
static HalTaskRef_t s_nasaReportTaskRef = NULL;
static uint8_t      s_nasaReportTaskStack[1024 * 10];

static void sTask_NasaReport(void *argv)
{
    (void)argv;
    NASA_Init();
    for (;;) {
        NASA_RunStep();
    }
}

/**
 * OpenSDK A7677S app entry (thay cho Application/get_sAPI trên A7672S).
 */
void userSpace_Main(void *arg)
{
    ApiMapInit(arg);
    HAL_LOG("ApiMapInit OK (A7677S)");

    HAL_OS_TaskSleep(APP_STARTUP_DELAY_SEC * HAL_TICKS_PER_SEC);
    HAL_LOG("==== NASA FW %s HW %s A7677S ====", NASA_FW_CODE, NASA_HW_CODE);

    EN25_Init();    // Khoi tao Flash
    CFG_Init();     // Khoi tao cau hinh
    GPS_Init();     // Khoi tao GPS
    SMS_Init();     // Khoi tao SMS

    /* Khoi tao mang qua HAL -- khong goi sAPI_NetworkInit truc tiep */
    HAL_NET_Init(); // Khoi tao mang
    HAL_NET_NitzEnable();// Khoi tao NITZ
    HAL_GNSS_UrcListenerStart(); // Khoi tao GPS URC

    INDICATION_TaskStart(); // Khoi tao LED

    if (s_nasaReportTaskRef == NULL) {
        if (HAL_OS_TaskCreate(&s_nasaReportTaskRef, s_nasaReportTaskStack,
                              sizeof(s_nasaReportTaskStack), 15, "nasa_reporter",
                              sTask_NasaReport, NULL) != 0) {
            s_nasaReportTaskRef = NULL;
            HAL_LOG("[NASA Reporter] create task fail");
        } else {
            HAL_LOG("[NASA Reporter] create task OK");
        }
    }
    // Task nhận SMS
    if (HAL_OS_TaskCreate(&gSmsRecvTask, gSmsRecvTaskStack, sizeof(gSmsRecvTaskStack),
                          18, "SmsRecvTask", SmsReceiverTask, NULL) != 0) {
        gSmsRecvTask = NULL;
        HAL_LOG("SmsRecvTask create fail");
    } else {
        HAL_LOG("SmsRecvTask create OK");
    }
}
// Task báo cáo NASA
void abort(void)
{
    /* Thiet bi khong nguoi truc: tu reset thay vi treo vinh vien */
    HAL_LOG("abort!!! -> SysReset");
    HAL_OS_TaskSleep(200);
    HAL_OS_SysReset();
    while (1);
}

#define _appRegTable_attr_ __attribute__((unused, section(".userSpaceRegTable")))
#define appMainStackSize (1024 * 10)

#ifndef APP_VERSION
#define APP_VERSION ""
#endif

userSpaceEntry_t userSpaceEntry _appRegTable_attr_ = {
    NULL, NULL, appMainStackSize, 30, "userSpaceMain", userSpace_Main, APP_VERSION
};
