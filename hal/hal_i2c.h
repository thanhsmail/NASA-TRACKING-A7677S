/**
 * @file    hal_i2c.h
 * @brief   HAL - I2C Bus Abstraction (dung cho cam bien gia toc LIS3DSH)
 *
 * drv_lis3dsh.c chi goi HAL_I2C_*. Implementation: hal_impl_simcom.c
 * dung sAPI_I2CConfigInit / sAPI_I2CRead / sAPI_I2CWrite.
 */
#ifndef HAL_I2C_H
#define HAL_I2C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Khoi tao kenh I2C (master).
 * @param  channel  Kenh I2C cua module (0 = CI2C pin37/38 tren ho A767x).
 * @param  fast     1 = 400 kHz, 0 = 100 kHz.
 * @return 0 neu thanh cong, -1 neu that bai.
 */
int HAL_I2C_Init(int channel, int fast);

/**
 * @brief  Doc lien tiep tu thanh ghi 8 bit (ghi dia chi, repeated start, doc).
 * @param  channel  Kenh I2C.
 * @param  addr7    Dia chi slave 7 bit (vi du 0x1D).
 * @param  reg      Dia chi thanh ghi.
 * @param  buf      Buffer nhan.
 * @param  len      So byte can doc.
 * @return 0 neu thanh cong, -1 neu loi bus / NACK.
 */
int HAL_I2C_ReadReg(int channel, uint8_t addr7, uint8_t reg, uint8_t *buf, uint16_t len);

/**
 * @brief  Ghi lien tiep vao thanh ghi 8 bit.
 * @return 0 neu thanh cong, -1 neu loi bus / NACK.
 */
int HAL_I2C_WriteReg(int channel, uint8_t addr7, uint8_t reg, const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_H */
