/**
 * @file    hal_adc.h
 * @brief   HAL - ADC Abstraction
 */
#ifndef HAL_ADC_H
#define HAL_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Đọc giá trị điện áp thực tế tại chân ADC (đơn vị: mV)
 * @param  channel Kênh ADC cần đọc (1 = ADC.PWR/ADC1)
 * @return Điện áp đọc được tại chân ADC tính bằng millivolt (mV)
 */
uint32_t HAL_ADC_ReadMv(int channel);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADC_H */
