/**
  ******************************************************************************
  * @file    sc_application.c
  * @brief   A7677S OpenSDK entry — NASA Tracking (port từ simcom_application.c A7672S)
  ******************************************************************************
  */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "api_map.h"
#include "simcom_api.h"
#include "simcom_common.h"

#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include "app_sms.h"
#include "app_cfg.h"
#include "app_cmd.h"
#include "app_network.h"
#include "app_nasa.h"

/* --- SMS URC task --- */
static sTaskRef gSmsRecvTask = NULL;
static UINT8 gSmsRecvTaskStack[1024 * 4];
static sMsgQRef gSmsMsgQueue = NULL;
static sMsgQRef gSmsReadRspQueue = NULL;

/* Xả response trễ còn kẹt trong queue (free arg3) để read/del không lẫn nhau */
static void DrainSmsRspQueue(void)
{
    SIM_MSG_T stale;
    while (1) {
        memset(&stale, 0, sizeof(stale));
        if (sAPI_MsgQRecv(gSmsReadRspQueue, &stale, SC_NO_SUSPEND) != SC_SUCCESS) break;
        if (stale.arg3) sAPI_Free(stale.arg3);
    }
}

static void SmsReceiverTask(void *argv)
{
    SC_STATUS status;
    (void)argv;

    sAPI_Debug("SmsReceiverTask start");
    status = sAPI_MsgQCreate(&gSmsReadRspQueue, "gSmsReadRspQueue", sizeof(SIM_MSG_T), 4, SC_FIFO);
    if (status != SC_SUCCESS) return;
    status = sAPI_MsgQCreate(&gSmsMsgQueue, "gSmsMsgQueue", sizeof(SIM_MSG_T), 10, SC_FIFO);
    if (status != SC_SUCCESS) return;

    /* Đợi SIM rồi cấu hình CNMI — thiếu bước này thì A7677S không báo URC SMS */
    if (SMS_EnsureReady() != 0) {
        sAPI_Debug("SmsReceiverTask: SMS modem config fail (continue)");
    }

    sAPI_UrcRefRegister(gSmsMsgQueue, SC_URC_SMS_MASK);
    sAPI_TaskSleep(200);

    while (1) {
        SIM_MSG_T msg = {0};

        if (SMS_IsFotaDownloadReady() == 1) {
            SMS_SetFotaDownloadHandled();
            sAPI_TaskSleep(200);
            sAPI_SysReset();
        }

        /* Timeout 1s thay vì SC_SUSPEND để vòng lặp còn kiểm tra được cờ FOTA */
        if (sAPI_MsgQRecv(gSmsMsgQueue, &msg, SC_TICKS_PER_SECOND) != SC_SUCCESS) continue;
        if (msg.msg_id != SRV_URC) goto cleanup;

        if (msg.arg2 == SC_URC_NEW_MSG_IND) {
            char *p_idx = (char *)msg.arg3;
            if (p_idx && strrchr(p_idx, ',')) {
                int index = atoi(strrchr(p_idx, ',') + 1);
                SIM_MSG_T readRspMsg = {0};
                DrainSmsRspQueue();
                if (sAPI_SmsReadMsg(1, index, gSmsReadRspQueue) == SC_SMS_SUCESS &&
                    sAPI_MsgQRecv(gSmsReadRspQueue, &readRspMsg, 1000) == SC_SUCCESS) {
                    SMS_ExtractAndExecuteSmsBody((char *)readRspMsg.arg3, "SMS");
                    sAPI_Free(readRspMsg.arg3);
                }
                {
                    SIM_MSG_T delRspMsg = {0};
                    DrainSmsRspQueue();
                    if (sAPI_SmsDelOneMsg(index, gSmsReadRspQueue) == SC_SMS_SUCESS) {
                        if (sAPI_MsgQRecv(gSmsReadRspQueue, &delRspMsg, 1000) == SC_SUCCESS) {
                            if (delRspMsg.arg3) sAPI_Free(delRspMsg.arg3);
                        }
                    }
                }
            }
        } else if (msg.arg2 == SC_URC_FLASH_MSG) {
            SMS_ExtractAndExecuteSmsBody((char *)msg.arg3, "SMS_FLASH");
        }

    cleanup:
        if (msg.arg3) sAPI_Free(msg.arg3);
    }
}

/* --- NASA reporter task --- */
static sTaskRef s_nasaReportTaskRef = NULL;
static UINT8 s_nasaReportTaskStack[1024 * 6];

static void sTask_NasaReport(void *argv)
{
    (void)argv;
    NASA_Init();
    for (;;) {
        NASA_RunStep();
    }
}

/* --- GPIO / ACC / LED task --- */
static sTaskRef s_gpioStatusTaskRef = NULL;
static UINT8 s_gpioStatusTaskStack[1024 * 2];

/*
 * LED theo Technical doc:
 *  - GNSS: nhấp chậm = fix tốt; sáng đứng = chưa fix
 *  - 4G/Net: nhấp chậm = đã kết nối server; sáng đứng = có mạng; tắt = không mạng
 *  - Power: sáng = OK
 * Mọi LED nhấp cùng một chu kỳ (blinkSlow) để nhìn đồng nhất.
 * ACC: active-low trên GPIO_ACC_IN (LOW = ACC ON, pull-up nên dây hở = OFF)
 */
static void sTask_GpioStatusIndication(void *argv)
{
    SC_GPIOConfiguration outputConfig = {SC_GPIO_OUT_PIN, 0, SC_GPIO_PULLUP_ENABLE, SC_GPIO_NO_EDGE, NULL, NULL};
    SC_GPIOConfiguration inputConfig = {SC_GPIO_IN_PIN, 1, SC_GPIO_PULLUP_ENABLE, SC_GPIO_NO_EDGE, NULL, NULL};
    int blinkSlow = 0;
    int blinkPhase = 0;
    UINT8 last_csq = 99;
    int csq_poll_count = 0;
    int lastRawAcc = -1;
    int accDebounceCount = 0;
    int s_accVal = 0;

    (void)argv;

    sAPI_GpioConfig(GPIO_LED_GNSS, outputConfig);
    sAPI_GpioConfig(GPIO_LED_NET, outputConfig);
    sAPI_GpioConfig(GPIO_LED_PWR, outputConfig);
    sAPI_GpioConfig(GPIO_DOUT, outputConfig);
    sAPI_GpioConfig(GPIO_ACC_IN, inputConfig);
    sAPI_GpioSetValue(GPIO_DOUT, CFG_GetDout() ? 1 : 0);
    sAPI_GpioSetValue(GPIO_LED_PWR, 1);

    while (1) {
        int silent = CFG_GetSilentMode();
        int currentAcc = (sAPI_GpioGetValue(GPIO_ACC_IN) == SC_GPIORC_LOW) ? 1 : 0;

        if (lastRawAcc == -1) {
            lastRawAcc = currentAcc;
            s_accVal = currentAcc;
            NASA_SetAcc(s_accVal);
        }

        if (currentAcc == lastRawAcc) {
            if (currentAcc != s_accVal) {
                accDebounceCount++;
                if (accDebounceCount >= ACC_DEBOUNCE_CYCLES) {
                    s_accVal = currentAcc;
                    accDebounceCount = 0;
                    NASA_SetAcc(s_accVal);
                    sAPI_Debug("[ACC] %d", s_accVal);
                }
            } else {
                accDebounceCount = 0;
            }
        } else {
            lastRawAcc = currentAcc;
            accDebounceCount = 1;
        }

        if (++csq_poll_count >= 10) {
            sAPI_NetworkGetCsq(&last_csq);
            csq_poll_count = 0;
        }

        blinkPhase++;
        if ((blinkPhase % 2) == 0) blinkSlow = !blinkSlow;

        if (silent) {
            sAPI_GpioSetValue(GPIO_LED_GNSS, 0);
            sAPI_GpioSetValue(GPIO_LED_NET, 0);
            sAPI_GpioSetValue(GPIO_LED_PWR, 0);
        } else {
            int sats = GPS_GetSatellitesCount();
            int gpsOk = (sats >= 5);
            /* Nhấp chậm (cùng nhịp blinkSlow với LED NET) khi fix tốt; sáng đứng khi chưa fix */
            sAPI_GpioSetValue(GPIO_LED_GNSS, gpsOk ? blinkSlow : 1);

            if (Network_IsConnected() && NASA_IsSessionActive()) {
                sAPI_GpioSetValue(GPIO_LED_NET, blinkSlow);
            } else if (last_csq >= 1 && last_csq <= 31) {
                sAPI_GpioSetValue(GPIO_LED_NET, 1);
            } else {
                sAPI_GpioSetValue(GPIO_LED_NET, 0);
            }

            sAPI_GpioSetValue(GPIO_LED_PWR, 1);
        }

        sAPI_GpioSetValue(GPIO_DOUT, CFG_GetDout() ? 1 : 0);
        sAPI_TaskSleep(100); /* 500ms @ 200 ticks/s */
    }
}

/**
 * OpenSDK A7677S app entry (thay cho Application/get_sAPI trên A7672S).
 */
void userSpace_Main(void *arg)
{
    ApiMapInit(arg);
    sAPI_Debug("ApiMapInit OK (A7677S)");

    sAPI_TaskSleep(APP_STARTUP_DELAY_SEC * SC_TICKS_PER_SECOND);
    sAPI_Debug("==== NASA FW %s HW %s BUILD_MARK=260714b A7677S ====", NASA_FW_CODE, NASA_HW_CODE);

    CFG_Init();
    SMS_Init();

    sAPI_NetworkInit();
    NitzEnableFromNetwork();
    GnssUrcListenerEnsureStarted();

    if (s_gpioStatusTaskRef == NULL) {
        if (sAPI_TaskCreate(&s_gpioStatusTaskRef, s_gpioStatusTaskStack,
                            sizeof(s_gpioStatusTaskStack), 200, (char *)"gpio_status",
                            sTask_GpioStatusIndication, NULL) != SC_SUCCESS) {
            s_gpioStatusTaskRef = NULL;
            sAPI_Debug("[GPIO Status] create task fail");
        } else {
            sAPI_Debug("[GPIO Status] create task OK");
        }
    }

    if (s_nasaReportTaskRef == NULL) {
        if (sAPI_TaskCreate(&s_nasaReportTaskRef, s_nasaReportTaskStack,
                            sizeof(s_nasaReportTaskStack), 120, (char *)"nasa_reporter",
                            sTask_NasaReport, NULL) != SC_SUCCESS) {
            s_nasaReportTaskRef = NULL;
            sAPI_Debug("[NASA Reporter] create task fail");
        } else {
            sAPI_Debug("[NASA Reporter] create task OK");
        }
    }

    if (sAPI_TaskCreate(&gSmsRecvTask, gSmsRecvTaskStack, sizeof(gSmsRecvTaskStack),
                        90, "SmsRecvTask", SmsReceiverTask, NULL) != SC_SUCCESS) {
        gSmsRecvTask = NULL;
        sAPI_Debug("SmsRecvTask create fail");
    } else {
        sAPI_Debug("SmsRecvTask create OK");
    }
}

void abort(void)
{
    /* Thiết bị không người trực: tự reset để phục hồi thay vì treo vĩnh viễn */
    sAPI_Debug("abort!!! -> SysReset");
    sAPI_TaskSleep(200);
    sAPI_SysReset();
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
