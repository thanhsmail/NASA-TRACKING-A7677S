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
 * @return 0 = LOW, 1 = HIGH.
 */
int HAL_GPIO_Read(int pin);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
