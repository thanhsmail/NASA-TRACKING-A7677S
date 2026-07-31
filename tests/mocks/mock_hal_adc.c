/**
 * @file mock_hal_adc.c
 * @brief Mock HAL ADC implementation for Host Unit Testing
 */
#include "hal/hal_adc.h"

static uint32_t s_mockAdcMv = 522; /* ~12V (522mV * 23 = 12006mV) */

void Mock_HAL_ADC_SetMv(uint32_t mv)
{
    s_mockAdcMv = mv;
}

uint32_t HAL_ADC_ReadMv(int channel)
{
    (void)channel;
    return s_mockAdcMv;
}
