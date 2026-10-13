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
static int s_csActiveLevel = 1; /* muc GPIO de chon flash (1 = qua NPN dao) */

static void EN25_CS_Assert(void)
{
    HAL_GPIO_Write(FLASH_SPI_CS_PIN, s_csActiveLevel);
}

static void EN25_CS_Deassert(void)
{
    HAL_GPIO_Write(FLASH_SPI_CS_PIN, !s_csActiveLevel);
}

static int EN25_ReadStatus(uint8_t *sr1)
{
    uint8_t cmd = EN25_CMD_READ_STATUS1;
    int rc;

    EN25_CS_Assert();
    rc = HAL_SPI_Read(&s_spiDev, &cmd, 1, sr1, 1);
    EN25_CS_Deassert();
    return rc;
}

static void EN25_Lock(void)
{
    HAL_OS_MutexLock(s_spiMutex);
}

static void EN25_Unlock(void)
{
    HAL_OS_MutexUnlock(s_spiMutex);
}

/*
 * Gui lenh Write Enable va kiem tra bit WEL. CS# di qua transistor dao nen len
 * muc cao cham; neu flash chua nhan WREN thi lenh ghi/xoa ke tiep bi bo qua ma
 * khong bao loi -> gui lai toi da 3 lan. Van khong thay WEL thi chi canh bao va
 * de lop tren phat hien bang doc lai (khong chan han viec ghi).
 */
static void EN25_WriteEnable(void)
{
    uint8_t cmd = EN25_CMD_WRITE_ENABLE;
    uint8_t sr1 = 0;
    int attempt;

    for (attempt = 0; attempt < 3; attempt++) {
        EN25_CS_Assert();
        HAL_SPI_Write(&s_spiDev, &cmd, 1);
        EN25_CS_Deassert();

        if (EN25_ReadStatus(&sr1) != 0) sr1 = 0;
        if (sr1 & EN25_SR1_WEL_BIT) return;
    }
    HAL_LOG("[EN25] WriteEnable: WEL not set (SR1=0x%02X)", sr1);
}

int EN25_IsBusy(void)
{
    uint8_t sr1 = 0xFF;

    if (EN25_ReadStatus(&sr1) != 0) return -1;
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

/**
 * Thu doc JEDEC ID tren 1 kenh SPI voi 1 muc CS.
 * @return 0 neu flash tra loi ID hop le
 */
static int EN25_Probe(int spiIndex, int csActiveLevel, uint8_t *mfrId, uint16_t *devId)
{
    int attempt;

    if (HAL_SPI_Init(&s_spiDev, spiIndex) != 0) {
        HAL_LOG("[EN25] HAL_SPI_Init failed! index=%d", spiIndex);
        return -1;
    }

    s_csActiveLevel = csActiveLevel;
    HAL_GPIO_Config(FLASH_SPI_CS_PIN, HAL_GPIO_OUT,
                    csActiveLevel ? HAL_GPIO_PULL_DOWN : HAL_GPIO_PULL_UP);
    EN25_CS_Deassert();
    HAL_OS_TaskSleep(HAL_TICKS_PER_SEC / 50); /* ~20 ms */

    for (attempt = 0; attempt < 2; attempt++) {
        *mfrId = 0;
        *devId = 0;
        if (EN25_ReadID(mfrId, devId) == 0 && EN25_IdLooksValid(*mfrId, *devId)) return 0;
        HAL_OS_TaskSleep(HAL_TICKS_PER_SEC / 20); /* ~50 ms */
    }
    HAL_LOG("[EN25] Probe spi=%d csActive=%d: no flash (MfrID=0x%02X DevID=0x%04X)",
            spiIndex, csActiveLevel, *mfrId, *devId);
    return -1;
}

int EN25_Init(void)
{
    /* Kenh cau hinh truoc, sau do cac kenh con lai (SDK khong ghi ro chi so nao la SSP0) */
    static const int spiCandidates[] = { FLASH_SPI_INDEX, 0, 1, 2 };
    uint8_t  mfrId = 0;
    uint16_t devId = 0;
    int      csLevel;
    unsigned i;

    if (s_spiMutex == NULL) {
        HAL_OS_MutexCreate(&s_spiMutex);
    }

    /* Muc CS theo schematic (1) truoc; muc nguoc lai chi de chan doan mach */
    for (csLevel = 1; csLevel >= 0; csLevel--) {
        for (i = 0; i < sizeof(spiCandidates) / sizeof(spiCandidates[0]); i++) {
            if (i > 0 && spiCandidates[i] == FLASH_SPI_INDEX) continue;
            if (EN25_Probe(spiCandidates[i], csLevel, &mfrId, &devId) == 0) {
                uint8_t sr1 = 0xFF;
                (void)EN25_ReadStatus(&sr1);
                HAL_LOG("[EN25/Flash] Flash Init OK! MfrID=0x%02X DevID=0x%04X SR1=0x%02X (spi=%d cs=%d csActive=%d)",
                        mfrId, devId, sr1, spiCandidates[i], FLASH_SPI_CS_PIN, csLevel);
                s_isInitialized = 1;
                return 0;
            }
        }
    }

    HAL_LOG("[EN25/Flash] Flash Init Failed on all SPI channels (csPin=%d)", FLASH_SPI_CS_PIN);
    /* Tra CS ve muc nghi theo schematic */
    s_csActiveLevel = 1;
    HAL_GPIO_Config(FLASH_SPI_CS_PIN, HAL_GPIO_OUT, HAL_GPIO_PULL_DOWN);
    EN25_CS_Deassert();
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
    /* Lenh + dia chi + du lieu gui trong 1 lan truyen SPI duy nhat (goi khi da giu khoa) */
    static uint8_t txBuf[4 + EN25_PAGE_SIZE];

    if (!buf || len == 0 || len > EN25_PAGE_SIZE || (addr + len) > EN25_FLASH_SIZE) return -1;
    if ((addr % EN25_PAGE_SIZE) + len > EN25_PAGE_SIZE) return -1;
    if (EN25_WaitReady(500) < 0) return -1;

    EN25_WriteEnable();

    txBuf[0] = EN25_CMD_PAGE_PROGRAM;
    txBuf[1] = (uint8_t)((addr >> 16) & 0xFF);
    txBuf[2] = (uint8_t)((addr >> 8) & 0xFF);
    txBuf[3] = (uint8_t)(addr & 0xFF);
    memcpy(&txBuf[4], buf, len);

    EN25_CS_Assert();
    if (HAL_SPI_Write(&s_spiDev, txBuf, 4 + len) != 0) {
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
