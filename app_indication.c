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
#include "hal/hal_gnss.h"
#include "app_config.h"
#include "app_gps.h"
#include "app_cfg.h"
#include "app_network.h"
#include "app_nasa.h"
#include "app_indication.h"
#include "app_backup.h"
#include <stddef.h>

static HalTaskRef_t s_gpioStatusTaskRef = NULL;
static uint8_t      s_gpioStatusTaskStack[1024 * 4];

/* Ngắt cạnh xuống chân ACC (active-low: ACC bật) đánh thức module khi đang ngủ */
static void AccWakeIsr(void)
{
    NASA_WakeRequest("ACC interrupt");
}

static void sTask_GpioStatusIndication(void *argv)
{
    int blinkPhase    = 0;
    int blinkSlow     = 0;
    uint8_t last_csq  = 0;
    int csq_poll_cnt  = 300; /* đọc CSQ ngay vòng đầu */

    int lastRawAcc    = -1;
    int accDebounce   = 0;
    int s_accVal      = 0;
    int accReadErr    = 0;
    int hwWdtOn       = 0;
    int gnssHangLogged = 0;

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
    /* Chân không hỗ trợ đánh thức thì vẫn còn đọc chân mỗi 2 s khi ngủ (bên dưới) */
    if (HAL_GPIO_WakeupEnable(GPIO_ACC_IN, HAL_GPIO_EDGE_FALL, AccWakeIsr) != 0) {
        HAL_LOG("[ACC] wakeup interrupt not available on pin %d, polling only", GPIO_ACC_IN);
    }
    HAL_GPIO_Write(GPIO_DOUT, CFG_GetDout() ? 1 : 0);

    while (1) {
        int sleeping   = NASA_IsSleeping();
        int silent     = CFG_GetSilentMode() || sleeping; /* ngủ: tắt hết LED */
        int accLevel   = HAL_GPIO_Read(GPIO_ACC_IN);
        /* Active-low: GPIO_ACC_IN LOW = ACC ON */
        int currentAcc = (accLevel == 0) ? 1 : 0;

        /* Đang ngủ mà chân ACC báo bật: thức ngay, không chờ lọc nhiễu 6 vòng x 2 s */
        if (sleeping && accLevel >= 0 && currentAcc && !s_accVal) {
            NASA_WakeRequest("ACC pin");
        }

        if (accLevel < 0) {
            /* Đọc lỗi: giữ nguyên trạng thái ACC cũ, không coi là ACC OFF */
            if (accReadErr++ % 20 == 0) {
                HAL_LOG("[ACC] read pin %d fail (x%d)", GPIO_ACC_IN, accReadErr);
            }
        } else if (lastRawAcc == -1) {
            lastRawAcc = currentAcc;
            s_accVal   = currentAcc;
            NASA_SetAcc(s_accVal);
            GPS_SetAccOn(s_accVal);
            HAL_LOG("[ACC] init %d (pin %d level %d)", s_accVal, GPIO_ACC_IN, accLevel);
        } else if (currentAcc == lastRawAcc) {
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

        /* Có sóng: đọc CSQ mỗi 150 s; mất sóng: mỗi 5 s (trước đây mỗi vòng 0,5 s,
         * lệnh đọc CSQ chậm thì làm trễ việc nuôi watchdog) */
        if (++csq_poll_cnt >= ((last_csq == 0) ? 10 : 300)) {
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

            /* LED nguồn = trạng thái bộ nhớ backup: sáng = bộ nhớ trong (EFS),
             * nháy = bộ nhớ ngoài (flash SPI), tắt = cả hai lỗi */
            if (GPIO_LED_PWR != FLASH_SPI_CS_PIN) {
                int media = Backup_GetMedia();
                int pwrLed = (media == BACKUP_MEDIA_FLASH) ? blinkSlow :
                             (media == BACKUP_MEDIA_EFS)   ? 1 : 0;
                HAL_GPIO_Write(GPIO_LED_PWR, pwrLed);
            }
        }

        if (NASA_IsWatchdogTimeout()) {
            HAL_LOG("[WATCHDOG] Task nasa_reporter timeout >180s! SysReset...");
            HAL_OS_TaskSleep(200);
            HAL_OS_SysReset();
        }

#if HW_WDT_ENABLE
        /* Watchdog phần cứng: bắt trường hợp chính task này (hoặc cả ứng dụng)
         * treo, khi watchdog phần mềm ở trên không còn chạy được */
        if (!hwWdtOn) {
            if (NASA_IsStarted() && HAL_OS_HwWatchdogStart(HW_WDT_PERIOD_REG) == 0) {
                hwWdtOn = 1;
            }
        } else if (HAL_GNSS_GetTaskAliveAgeSec() < GNSS_TASK_HANG_SEC) {
            HAL_OS_HwWatchdogFeed();
        } else if (!gnssHangLogged) {
            /* gnss_task treo (không tự khởi động lại GNSS được): ngừng nuôi để reset */
            HAL_LOG("[WDT] gnss_task hang %lus -> stop feeding, hardware reset",
                    (unsigned long)HAL_GNSS_GetTaskAliveAgeSec());
            gnssHangLogged = 1;
        }
#endif

        HAL_GPIO_Write(GPIO_DOUT, CFG_GetDout() ? 1 : 0);
        /* Ngủ: 2 s / vòng, vẫn nuôi watchdog phần cứng (16 s) đủ dày */
        HAL_OS_TaskSleep(sleeping ? SLEEP_GPIO_LOOP_TICKS : 100);
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
