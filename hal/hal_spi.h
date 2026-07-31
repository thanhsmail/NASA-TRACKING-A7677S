/**
 * @file    hal_spi.h
 * @brief   HAL - SPI Bus Abstraction (dung cho Flash EN25QH64A / W25Q64)
 *
 * drv_en25qh64a.c chi goi HAL_SPI_*. Implementation: hal_impl_simcom.c
 * dung sAPI_SpiWriteBytesEx / sAPI_SpiReadBytesEx.
 */
#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle dai dien mot SPI bus/device */
typedef struct {
    int index;      /**< Chi so SPI (1 = SSP0 tren A7677S MANS) */
    int _reserved;  /**< Padding / mo rong tuong lai */
} HalSpiDev_t;

/**
 * @brief  Khoi tao SPI bus va cau hinh toc do, mode.
 * @param  dev       [out] Handle SPI duoc dien sau khi init thanh cong.
 * @param  spiIndex  Chi so bus SPI (1 tren A7677S MANS).
 * @return 0 neu thanh cong, -1 neu that bai.
 */
int HAL_SPI_Init(HalSpiDev_t *dev, int spiIndex);

/**
 * @brief  Ghi du lieu (khong doc phan hoi).
 * @param  dev   Handle SPI.
 * @param  data  Buffer du lieu can ghi.
 * @param  len   So byte can ghi.
 * @return 0 neu thanh cong.
 */
int HAL_SPI_Write(HalSpiDev_t *dev, const uint8_t *data, uint32_t len);

/**
 * @brief  Ghi lenh roi doc phan hoi (full-duplex theo kieu NOR Flash).
 * @param  dev     Handle SPI.
 * @param  cmd     Buffer lenh (tx).
 * @param  cmdLen  Do dai lenh (bytes).
 * @param  rxBuf   Buffer nhan du lieu tra ve.
 * @param  rxLen   So byte can doc.
 * @return 0 neu thanh cong.
 */
int HAL_SPI_Read(HalSpiDev_t *dev,
                 const uint8_t *cmd, uint32_t cmdLen,
                 uint8_t *rxBuf, uint32_t rxLen);

/**
 * @brief  Dat muc CS tren SPI custom (cho mach dao logic NPN C1815).
 * @param  dev    Handle SPI.
 * @param  level  0 = deassert, 1 = assert.
 */
int HAL_SPI_CustomCsSetLevel(HalSpiDev_t *dev, int level);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */
