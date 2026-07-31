/**
 * @file mock_hal_gpio.c
 * @brief Mock HAL GPIO implementation for Host Unit Testing
 */
#include "hal/hal_gpio.h"

static int s_mockAccPinState = 0; /* 0 = LOW (Active LOW -> ACC ON) */

void Mock_HAL_GPIO_SetAccPin(int lowActiveOn)
{
    s_mockAccPinState = lowActiveOn ? 0 : 1;
}

int HAL_GPIO_Config(int pin, HalGpioDir_t dir, HalGpioPull_t pull)
{
    (void)pin; (void)dir; (void)pull;
    return 0;
}

int HAL_GPIO_Write(int pin, int val)
{
    (void)pin; (void)val;
    return 0;
}

int HAL_GPIO_Read(int pin)
{
    if (pin == 121) return s_mockAccPinState; /* GPIO_ACC_IN */
    return 1;
}
