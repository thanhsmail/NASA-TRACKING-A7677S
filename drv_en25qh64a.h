#ifndef _DRV_EN25QH64A_H_
#define _DRV_EN25QH64A_H_

#include <stdint.h>
#include <stddef.h>

/* Thông số phần cứng EN25QH64A (64 Mbit / 8 MByte) */
#define EN25_FLASH_SIZE         (8 * 1024 * 1024)  /* 8 MBytes */
#define EN25_TOTAL_SECTORS      2048               /* 2048 Sectors x 4KB */
#define EN25_SECTOR_SIZE        4096               /* 4 KB / Sector */
#define EN25_PAGE_SIZE          256                /* 256 Bytes / Page */
#define EN25_BLOCK_SIZE         65536              /* 64 KB / Block */

/* ID Nhà sản xuất & Thiết bị EN25QH64A / Winbond W25Q64JVSSIQ */
#define EN25_MFR_ID             0x1C               /* EON Flash */
#define WINBOND_MFR_ID          0xEF               /* Winbond Serial Flash */
#define FLASH_DEV_ID_W25Q64     0x4017             /* Winbond W25Q64JV Device ID */
#define FLASH_DEV_ID_EN25Q64    0x7017             /* EN25QH64A Device ID */

/* Tập lệnh SPI NOR Flash */
#define EN25_CMD_WRITE_ENABLE   0x06
#define EN25_CMD_WRITE_DISABLE  0x04
#define EN25_CMD_READ_STATUS1   0x05
#define EN25_CMD_WRITE_STATUS1  0x01
#define EN25_CMD_READ_DATA      0x03
#define EN25_CMD_FAST_READ      0x0B
#define EN25_CMD_PAGE_PROGRAM   0x02
#define EN25_CMD_SECTOR_ERASE   0x20               /* Erase 4KB */
#define EN25_CMD_BLOCK_ERASE    0xD8               /* Erase 64KB */
#define EN25_CMD_CHIP_ERASE     0xC7
#define EN25_CMD_READ_JEDEC_ID  0x9F

/* Bit Status Register 1 */
#define EN25_SR1_WIP_BIT        (1 << 0)           /* Write In Progress */
#define EN25_SR1_WEL_BIT        (1 << 1)           /* Write Enable Latch */

/* API Driver */
int EN25_Init(void);
int EN25_IsReady(void);
int EN25_ReadID(uint8_t *mfrId, uint16_t *devId);
int EN25_Read(uint32_t addr, uint8_t *buf, uint32_t len);
int EN25_WritePage(uint32_t addr, const uint8_t *buf, uint32_t len);
int EN25_Write(uint32_t addr, const uint8_t *buf, uint32_t len);
int EN25_EraseSector(uint32_t sectorAddr);
int EN25_EraseBlock64K(uint32_t blockAddr);
int EN25_IsBusy(void);
int EN25_WaitReady(uint32_t timeoutMs);

#endif /* _DRV_EN25QH64A_H_ */
