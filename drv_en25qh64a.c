/**
 * @file drv_en25qh64a.c
 * @brief SPI NOR Flash driver (EN25QH64A / W25Q64)
 *
 * Luu y: File nay KHONG #include simcom_api.h truc tiep.
 * Moi tuong tac phần cứng di qua HAL layer.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_gpio.h"
#include "hal/hal_spi.h"
#include "app_config.h"
#include "app_utils.h"
#include "drv_en25qh64a.h"
#include <string.h>

static HalSpiDev_t  s_spiDev;
static int          s_isInitialized = 0;
static HalMutexRef_t s_spiMutex = NULL;

/*
 * Mach CS Flash (NPN C1815 dao logic) -- theo schematic:
 *  - MCU GPIO_05 HIGH -> transistor dan -> Flash CS# = 0V (Active Low)
 *  - MCU GPIO_05 LOW  -> transistor ngat -> Flash CS# = 3.3V (Inactive)
 *
 * Dung HAL_GPIO_Write (khong goi sAPI_GpioSetValue truc tiep).
 */
static void EN25_CS_Assert(void)
{
    HAL_GPIO_Write(FLASH_SPI_CS_PIN, 1);
    HAL_SPI_CustomCsSetLevel(&s_spiDev, 1);
}

static void EN25_CS_Deassert(void)
{
    HAL_GPIO_Write(FLASH_SPI_CS_PIN, 0);
    HAL_SPI_CustomCsSetLevel(&s_spiDev, 0);
}

static void EN25_Lock(void)
{
    HAL_OS_MutexLock(s_spiMutex);
}

static void EN25_Unlock(void)
{
    HAL_OS_MutexUnlock(s_spiMutex);
}

static void EN25_WriteEnable(void)
{
    uint8_t cmd = EN25_CMD_WRITE_ENABLE;
    EN25_CS_Assert();
    HAL_SPI_Write(&s_spiDev, &cmd, 1);
    EN25_CS_Deassert();
}

int EN25_IsBusy(void)
{
    uint8_t cmd = EN25_CMD_READ_STATUS1;
    uint8_t sr1 = 0xFF;
    int rc;

    EN25_CS_Assert();
    rc = HAL_SPI_Read(&s_spiDev, &cmd, 1, &sr1, 1);
    EN25_CS_Deassert();
    if (rc != 0) return -1;
    return (sr1 & EN25_SR1_WIP_BIT) ? 1 : 0;
}

int EN25_WaitReady(uint32_t timeoutMs)
{
    uint32_t startTick    = GetTickNow();
    uint32_t timeoutTicks = (timeoutMs * HAL_TICKS_PER_SEC) / 1000;
    if (timeoutTicks == 0) timeoutTicks = 1;

    while (1) {
        int busy = EN25_IsBusy();
        if (busy == 0) return 0;
        if (busy <  0) return -1;
        if ((GetTickNow() - startTick) >= timeoutTicks) {
            HAL_LOG("[EN25] WaitReady Timeout!");
            return -1;
        }
        HAL_OS_TaskSleep(1);
    }
}

int EN25_ReadID(uint8_t *mfrId, uint16_t *devId)
{
    uint8_t cmd   = EN25_CMD_READ_JEDEC_ID;
    uint8_t idBuf[3] = {0};
    int rc;

    if (!mfrId || !devId) return -1;

    EN25_CS_Assert();
    rc = HAL_SPI_Read(&s_spiDev, &cmd, 1, idBuf, 3);
    EN25_CS_Deassert();

    HAL_LOG("[EN25/RawID] rc=%d Byte0=0x%02X, Byte1=0x%02X, Byte2=0x%02X (spi=%d csPin=%d)",
            rc, idBuf[0], idBuf[1], idBuf[2], s_spiDev.index, FLASH_SPI_CS_PIN);

    if (rc != 0) return -1;

    *mfrId = idBuf[0];
    *devId = ((uint16_t)idBuf[1] << 8) | idBuf[2];
    return 0;
}

static int EN25_IdLooksValid(uint8_t mfrId, uint16_t devId)
{
    if (mfrId == 0x00 || mfrId == 0xFF) return 0;
    if (devId == 0x0000 || devId == 0xFFFF) return 0;
    /* Chấp nhận Winbond/EON/GigaDevice/Macronix/Micron và JEDEC ID khác 0/FF */
    (void)devId;
    return 1;
}

int EN25_Init(void)
{
    uint8_t  mfrId = 0;
    uint16_t devId = 0;
    int      attempt;

    if (s_spiMutex == NULL) {
        HAL_OS_MutexCreate(&s_spiMutex);
    }

    /* Cau hinh SPI qua HAL_SPI_Init */
    if (HAL_SPI_Init(&s_spiDev, FLASH_SPI_INDEX) != 0) {
        HAL_LOG("[EN25] HAL_SPI_Init failed! index=%d", FLASH_SPI_INDEX);
        s_isInitialized = 0;
        return -1;
    }

    /* GPIO_05 output, idle LOW = flash bo chon (NPN ngat, CS# = 3.3V) */
    HAL_GPIO_Config(FLASH_SPI_CS_PIN, HAL_GPIO_OUT, HAL_GPIO_PULL_DOWN);
    EN25_CS_Deassert();

    /* Cho flash on dinh nguon sau khi CS idle */
    HAL_OS_TaskSleep(HAL_TICKS_PER_SEC / 50); /* ~20 ms */

    for (attempt = 0; attempt < 6; attempt++) {
        mfrId = 0;
        devId = 0;

        if (EN25_ReadID(&mfrId, &devId) == 0 && EN25_IdLooksValid(mfrId, devId)) {
            HAL_LOG("[EN25/Flash] Flash Init OK! MfrID=0x%02X DevID=0x%04X (spi=%d cs=%d)",
                    mfrId, devId, FLASH_SPI_INDEX, FLASH_SPI_CS_PIN);
            s_isInitialized = 1;
            return 0;
        }
        HAL_LOG("[EN25] ReadID attempt %d fail MfrID=0x%02X DevID=0x%04X",
                attempt + 1, mfrId, devId);
        HAL_OS_TaskSleep(HAL_TICKS_PER_SEC / 20); /* ~50 ms */
    }

    HAL_LOG("[EN25/Flash] Flash Init Failed (MfrID=0x%02X, DevID=0x%04X) spi=%d csPin=%d",
            mfrId, devId, FLASH_SPI_INDEX, FLASH_SPI_CS_PIN);
    s_isInitialized = 0;
    return -1;
}

int EN25_IsReady(void)
{
    return s_isInitialized;
}

int EN25_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint8_t cmdBlock[4];
    int rc = -1;

    if (!s_isInitialized || !buf || len == 0 || (addr + len) > EN25_FLASH_SIZE) return -1;

    EN25_Lock();
    if (EN25_WaitReady(500) < 0) goto out;

    cmdBlock[0] = EN25_CMD_READ_DATA;
    cmdBlock[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmdBlock[2] = (uint8_t)((addr >> 8) & 0xFF);
    cmdBlock[3] = (uint8_t)(addr & 0xFF);

    EN25_CS_Assert();
    if (HAL_SPI_Read(&s_spiDev, cmdBlock, 4, buf, len) != 0) {
        EN25_CS_Deassert();
        goto out;
    }
    EN25_CS_Deassert();
    rc = 0;

out:
    EN25_Unlock();
    return rc;
}

static int EN25_WritePageInternal(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint8_t cmdBlock[4];

    if (!buf || len == 0 || len > EN25_PAGE_SIZE || (addr + len) > EN25_FLASH_SIZE) return -1;
    if ((addr % EN25_PAGE_SIZE) + len > EN25_PAGE_SIZE) return -1;
    if (EN25_WaitReady(500) < 0) return -1;

    EN25_WriteEnable();

    cmdBlock[0] = EN25_CMD_PAGE_PROGRAM;
    cmdBlock[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmdBlock[2] = (uint8_t)((addr >> 8) & 0xFF);
    cmdBlock[3] = (uint8_t)(addr & 0xFF);

    /* Giu CS lien tuc qua ca lenh + data -- bat buoc voi Page Program */
    EN25_CS_Assert();
    if (HAL_SPI_Write(&s_spiDev, cmdBlock, 4) != 0) {
        EN25_CS_Deassert();
        return -1;
    }
    if (HAL_SPI_Write(&s_spiDev, (const uint8_t *)buf, len) != 0) {
        EN25_CS_Deassert();
        return -1;
    }
    EN25_CS_Deassert();

    return EN25_WaitReady(1000);
}

int EN25_WritePage(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    int rc;
    if (!s_isInitialized) return -1;
    EN25_Lock();
    rc = EN25_WritePageInternal(addr, buf, len);
    EN25_Unlock();
    return rc;
}

int EN25_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t pageOffset, bytesToWrite;
    int rc = -1;

    if (!s_isInitialized || !buf || len == 0 || (addr + len) > EN25_FLASH_SIZE) return -1;

    EN25_Lock();
    while (len > 0) {
        pageOffset = addr % EN25_PAGE_SIZE;
        bytesToWrite = EN25_PAGE_SIZE - pageOffset;
        if (bytesToWrite > len) bytesToWrite = len;

        if (EN25_WritePageInternal(addr, buf, bytesToWrite) < 0) goto out;

        addr += bytesToWrite;
        buf += bytesToWrite;
        len -= bytesToWrite;
    }
    rc = 0;

out:
    EN25_Unlock();
    return rc;
}

int EN25_EraseSector(uint32_t sectorAddr)
{
    uint8_t cmdBlock[4];
    uint32_t addr = sectorAddr & ~(EN25_SECTOR_SIZE - 1);
    int rc = -1;

    if (!s_isInitialized || addr >= EN25_FLASH_SIZE) return -1;

    EN25_Lock();
    if (EN25_WaitReady(500) < 0) goto out;

    EN25_WriteEnable();

    cmdBlock[0] = EN25_CMD_SECTOR_ERASE;
    cmdBlock[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmdBlock[2] = (uint8_t)((addr >> 8) & 0xFF);
    cmdBlock[3] = (uint8_t)(addr & 0xFF);

    EN25_CS_Assert();
    if (HAL_SPI_Write(&s_spiDev, cmdBlock, 4) != 0) {
        EN25_CS_Deassert();
        goto out;
    }
    EN25_CS_Deassert();

    rc = EN25_WaitReady(3000); /* Maximum sector erase time ~3s */

out:
    EN25_Unlock();
    return rc;
}

int EN25_EraseBlock64K(uint32_t blockAddr)
{
    uint8_t cmdBlock[4];
    uint32_t addr = blockAddr & ~(EN25_BLOCK_SIZE - 1);
    int rc = -1;

    if (!s_isInitialized || addr >= EN25_FLASH_SIZE) return -1;

    EN25_Lock();
    if (EN25_WaitReady(500) < 0) goto out;

    EN25_WriteEnable();

    cmdBlock[0] = EN25_CMD_BLOCK_ERASE;
    cmdBlock[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmdBlock[2] = (uint8_t)((addr >> 8) & 0xFF);
    cmdBlock[3] = (uint8_t)(addr & 0xFF);

    EN25_CS_Assert();
    if (HAL_SPI_Write(&s_spiDev, cmdBlock, 4) != 0) {
        EN25_CS_Deassert();
        goto out;
    }
    EN25_CS_Deassert();

    rc = EN25_WaitReady(5000);

out:
    EN25_Unlock();
    return rc;
}
