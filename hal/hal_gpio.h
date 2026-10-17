/**
 * @file    hal_gpio.h
 * @brief   HAL - GPIO Abstraction: Config / Write / Read
 *
 * Pin number dung so nguyen (gia tri pad cua SDK tuong ung).
 * App chi dung ham HAL_GPIO_*, khong goi sAPI_GpioConfig/SetValue/GetValue.
 */
#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

/** Huong cua chan GPIO */
typedef enum {
    HAL_GPIO_OUT = 0,   /**< Output */
    HAL_GPIO_IN  = 1    /**< Input  */
} HalGpioDir_t;

/** Che do pull cua chan GPIO */
typedef enum {
    HAL_GPIO_PULL_NONE = 0, /**< Khong pull */
    HAL_GPIO_PULL_UP,       /**< Pull-up    */
    HAL_GPIO_PULL_DOWN      /**< Pull-down  */
} HalGpioPull_t;

/**
 * @brief  Cau hinh chan GPIO.
 * @param  pin   So pin (pad number tuong ung SDK).
 * @param  dir   HAL_GPIO_OUT hoac HAL_GPIO_IN.
 * @param  pull  Che do pull.
 */
void HAL_GPIO_Config(int pin, HalGpioDir_t dir, HalGpioPull_t pull);

/**
 * @brief  Ghi muc logic vao chan GPIO output.
 * @param  pin   So pin.
 * @param  level 0 = LOW, 1 = HIGH.
 */
void HAL_GPIO_Write(int pin, int level);

/**
 * @brief  Doc muc logic tu chan GPIO input.
 * @param  pin  So pin.
 * @return 0 = LOW, 1 = HIGH, -1 = loi doc (pin chua cau hinh / sai pad).
 */
int HAL_GPIO_Read(int pin);

/** Canh tin hieu dung cho ngat / danh thuc */
typedef enum {
    HAL_GPIO_EDGE_RISE = 1,
    HAL_GPIO_EDGE_FALL = 2,
    HAL_GPIO_EDGE_BOTH = 3
} HalGpioEdge_t;

/** Ham goi lai trong ngu canh ngat: chi duoc dat co, khong log / khong cho */
typedef void (*HalGpioWakeCb_t)(void);

/**
 * @brief  Cau hinh chan input (pull-up) co ngat theo canh va cho phep danh thuc
 *         module khi dang sleep (sAPI_GpioWakeupEnable). Ngat danh thuc tat luon
 *         sleep he thong (theo demo SDK) de task kip xu ly.
 * @return 0 = OK, -1 = SDK tu choi (chan khong ho tro danh thuc)
 */
int HAL_GPIO_WakeupEnable(int pin, HalGpioEdge_t edge, HalGpioWakeCb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
