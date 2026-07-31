/**
 * @file app_indication.c
 * @brief LED / ACC Status Indication Module -- NASA Tracking
 *
 * Quản lý status LEDs (GNSS, NET, PWR), đọc chân ACC in với debounce,
 * đồng bộ relay DOUT, và giám sát Watchdog timeout.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_gpio.h"
#include "hal/hal_net.h"
#include "app_config.h"
#include "app_gps.h"
#include "app_cfg.h"
#include "app_network.h"
#include "app_nasa.h"
#include "app_indication.h"
#include <stddef.h>

static HalTaskRef_t s_gpioStatusTaskRef = NULL;
static uint8_t      s_gpioStatusTaskStack[1024 * 4];

static void sTask_GpioStatusIndication(void *argv)
{
    int blinkPhase    = 0;
    int blinkSlow     = 0;
    uint8_t last_csq  = 0;
    int csq_poll_cnt  = 0;

    int lastRawAcc    = -1;
    int accDebounce   = 0;
    int s_accVal      = 0;

    (void)argv;

    /* Cau hinh GPIO qua HAL -- khong phu thuoc SC_GPIOConfiguration */
    HAL_GPIO_Config(GPIO_LED_GNSS, HAL_GPIO_OUT, HAL_GPIO_PULL_UP);
    HAL_GPIO_Config(GPIO_LED_NET,  HAL_GPIO_OUT, HAL_GPIO_PULL_UP);
    if (GPIO_LED_PWR != FLASH_SPI_CS_PIN) {
        HAL_GPIO_Config(GPIO_LED_PWR, HAL_GPIO_OUT, HAL_GPIO_PULL_UP);
        HAL_GPIO_Write(GPIO_LED_PWR, 1);
    }
    HAL_GPIO_Config(GPIO_DOUT,   HAL_GPIO_OUT, HAL_GPIO_PULL_UP);
    HAL_GPIO_Config(GPIO_ACC_IN, HAL_GPIO_IN,  HAL_GPIO_PULL_UP);
    HAL_GPIO_Write(GPIO_DOUT, CFG_GetDout() ? 1 : 0);

    while (1) {
        int silent     = CFG_GetSilentMode();
        /* Active-low: GPIO_ACC_IN LOW = ACC ON */
        int currentAcc = (HAL_GPIO_Read(GPIO_ACC_IN) == 0) ? 1 : 0;

        if (lastRawAcc == -1) {
            lastRawAcc = currentAcc;
            s_accVal   = currentAcc;
            NASA_SetAcc(s_accVal);
            GPS_SetAccOn(s_accVal);
        }

        if (currentAcc == lastRawAcc) {
            if (currentAcc != s_accVal) {
                accDebounce++;
                if (accDebounce >= ACC_DEBOUNCE_CYCLES) {
                    s_accVal    = currentAcc;
                    accDebounce = 0;
                    NASA_SetAcc(s_accVal);
                    GPS_SetAccOn(s_accVal);
                    HAL_LOG("[ACC] %d", s_accVal);
                }
            } else {
                accDebounce = 0;
            }
        } else {
            lastRawAcc  = currentAcc;
            accDebounce = 1;
        }

        if (last_csq == 0 || ++csq_poll_cnt >= 300) {
            HAL_NET_GetCsq(&last_csq);
            csq_poll_cnt = 0;
        }

        blinkPhase++;
        if ((blinkPhase % 2) == 0) blinkSlow = !blinkSlow;

        if (silent) {
            HAL_GPIO_Write(GPIO_LED_GNSS, 0);
            HAL_GPIO_Write(GPIO_LED_NET,  0);
            if (GPIO_LED_PWR != FLASH_SPI_CS_PIN) HAL_GPIO_Write(GPIO_LED_PWR, 0);
        } else {
            int sats  = GPS_GetSatellitesCount();
            int gpsOk = (sats >= 5);
            HAL_GPIO_Write(GPIO_LED_GNSS, gpsOk ? blinkSlow : 1);

            if (Network_IsConnected() && NASA_IsSessionActive()) {
                HAL_GPIO_Write(GPIO_LED_NET, blinkSlow);
            } else if (last_csq >= 1 && last_csq <= 31) {
                HAL_GPIO_Write(GPIO_LED_NET, 1);
            } else {
                HAL_GPIO_Write(GPIO_LED_NET, 0);
            }

            if (GPIO_LED_PWR != FLASH_SPI_CS_PIN) HAL_GPIO_Write(GPIO_LED_PWR, 1);
        }

        if (NASA_IsWatchdogTimeout()) {
            HAL_LOG("[WATCHDOG] Task nasa_reporter timeout >180s! SysReset...");
            HAL_OS_TaskSleep(200);
            HAL_OS_SysReset();
        }

        HAL_GPIO_Write(GPIO_DOUT, CFG_GetDout() ? 1 : 0);
        HAL_OS_TaskSleep(100);
    }
}

int INDICATION_TaskStart(void)
{
    if (s_gpioStatusTaskRef != NULL) return 0;

    if (HAL_OS_TaskCreate(&s_gpioStatusTaskRef, s_gpioStatusTaskStack,
                          sizeof(s_gpioStatusTaskStack), 20, "gpio_status",
                          sTask_GpioStatusIndication, NULL) != 0) {
        s_gpioStatusTaskRef = NULL;
        HAL_LOG("[GPIO Status] create task fail");
        return -1;
    }

    HAL_LOG("[GPIO Status] create task OK");
    return 0;
}
