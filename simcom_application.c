#include "simcom_api.h"
#include "simcom_common.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_utils.h"
#include "app_gps.h"
#include "app_sms.h"
#include "app_cfg.h"
#include "app_cmd.h"
#include "app_network.h"
#include "app_nasa.h"

static sTaskRef gSmsRecvTask = NULL;
static UINT8 gSmsRecvTaskStack[1024 * 4];
static sMsgQRef gSmsMsgQueue = NULL;
static sMsgQRef gSmsReadRspQueue = NULL;

static void SmsReceiverTask(void *argv)
{
    SC_STATUS status;
    (void)argv;

    sAPI_Debug("SmsReceiverTask start");
    status = sAPI_MsgQCreate(&gSmsReadRspQueue, "gSmsReadRspQueue", sizeof(SIM_MSG_T), 4, SC_FIFO);
    if (status != SC_SUCCESS) return;
    status = sAPI_MsgQCreate(&gSmsMsgQueue, "gSmsMsgQueue", sizeof(SIM_MSG_T), 10, SC_FIFO);
    if (status != SC_SUCCESS) return;

    sAPI_UrcRefRegister(gSmsMsgQueue, SC_URC_SMS_MASK);
    sAPI_TaskSleep(200);

    while (1) {
        SIM_MSG_T msg = {0};

        if (SMS_IsFotaDownloadReady() == 1) {
            SMS_SetFotaDownloadHandled();
            sAPI_TaskSleep(200);
            sAPI_SysReset();
        }

        if (sAPI_MsgQRecv(gSmsMsgQueue, &msg, SC_SUSPEND) != SC_SUCCESS) continue;
        if (msg.msg_id != SRV_URC) goto cleanup;

        if (msg.arg2 == SC_URC_NEW_MSG_IND) {
            char *p_idx = (char *)msg.arg3;
            if (p_idx && strrchr(p_idx, ',')) {
                int index = atoi(strrchr(p_idx, ',') + 1);
                SIM_MSG_T readRspMsg = {0};
                if (sAPI_SmsReadMsg(1, index, gSmsReadRspQueue) == SC_SMS_SUCESS &&
                    sAPI_MsgQRecv(gSmsReadRspQueue, &readRspMsg, 1000) == SC_SUCCESS) {
                    SMS_ExtractAndExecuteSmsBody((char *)readRspMsg.arg3, "SMS");
                    sAPI_Free(readRspMsg.arg3);
                }
                {
                    SIM_MSG_T delRspMsg = {0};
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

typedef void (*app_t)(void *argv);
typedef struct {
    app_t app_entry;
} appRegItem_t;
#define _appRegTable_attr_ __attribute__((unused, section(".appRegTable")))

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

static sTaskRef s_gpioStatusTaskRef = NULL;
static UINT8 s_gpioStatusTaskStack[1024 * 2];

/*
 * LED theo RV26 Technical doc:
 *  - GNSS: nhấp chậm = fix tốt; tắt = lỗi; nhấp nhanh = sleep (chưa làm sleep)
 *  - 4G/Net: nhấp chậm = đã kết nối server; sáng đứng = có mạng đang kết nối; tắt = không mạng
 *  - Power: sáng = OK
 * ACC: active-high trên chân RV26_GPIO_ACC_IN
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

    sAPI_GpioConfig(RV26_GPIO_LED_GNSS, outputConfig);
    sAPI_GpioConfig(RV26_GPIO_LED_NET, outputConfig);
    sAPI_GpioConfig(RV26_GPIO_LED_PWR, outputConfig);
    sAPI_GpioConfig(RV26_GPIO_DOUT, outputConfig);
    sAPI_GpioConfig(RV26_GPIO_ACC_IN, inputConfig);
    sAPI_GpioSetValue(RV26_GPIO_DOUT, CFG_GetDout() ? 1 : 0);
    sAPI_GpioSetValue(RV26_GPIO_LED_PWR, 1);

    while (1) {
        int silent = CFG_GetSilentMode();
        int currentAcc = (sAPI_GpioGetValue(RV26_GPIO_ACC_IN) == SC_GPIORC_LOW) ? 1 : 0;

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
            sAPI_GpioSetValue(RV26_GPIO_LED_GNSS, 0);
            sAPI_GpioSetValue(RV26_GPIO_LED_NET, 0);
            sAPI_GpioSetValue(RV26_GPIO_LED_PWR, 0);
        } else {
            int sats = GPS_GetSatellitesCount();
            int gpsOk = (sats >= 5);
            /* GNSS: fix tốt → nhấp chậm (sáng lâu); chưa fix → nhấp nhanh */
            sAPI_GpioSetValue(RV26_GPIO_LED_GNSS, gpsOk ? blinkSlow : (blinkPhase & 1));

            if (Network_IsConnected() && NASA_IsSessionActive()) {
                sAPI_GpioSetValue(RV26_GPIO_LED_NET, blinkSlow); /* server OK: slow blink */
            } else if (last_csq >= 1 && last_csq <= 31) {
                sAPI_GpioSetValue(RV26_GPIO_LED_NET, 1); /* registered: solid */
            } else {
                sAPI_GpioSetValue(RV26_GPIO_LED_NET, 0);
            }

            sAPI_GpioSetValue(RV26_GPIO_LED_PWR, 1);
        }

        sAPI_GpioSetValue(RV26_GPIO_DOUT, CFG_GetDout() ? 1 : 0);
        sAPI_TaskSleep(100); /* 500ms */
    }
}

static void Application(void *argv)
{
    unsigned long *apiTable = (unsigned long *)argv;
    if (apiTable != NULL) {
        get_sAPI(apiTable);
        sAPI_Debug("sc_Init OK");
    }

    sAPI_TaskSleep(APP_STARTUP_DELAY_SEC * SC_TICKS_PER_SECOND);
    sAPI_Debug("==== NASA FW %s HW %s BUILD_MARK=260714b ====", NASA_FW_CODE, NASA_HW_CODE);

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
        }
    }

    if (s_nasaReportTaskRef == NULL) {
        if (sAPI_TaskCreate(&s_nasaReportTaskRef, s_nasaReportTaskStack,
                            sizeof(s_nasaReportTaskStack), 120, (char *)"nasa_reporter",
                            sTask_NasaReport, NULL) != SC_SUCCESS) {
            s_nasaReportTaskRef = NULL;
        }
    }

    if (sAPI_TaskCreate(&gSmsRecvTask, gSmsRecvTaskStack, sizeof(gSmsRecvTaskStack),
                        90, "SmsRecvTask", SmsReceiverTask, NULL) != SC_SUCCESS) {
        gSmsRecvTask = NULL;
    }
}

appRegItem_t NASA_Tracking_app _appRegTable_attr_ = {.app_entry = Application};
