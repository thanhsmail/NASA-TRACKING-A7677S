#include "simcom_api.h"
#include "sc_spi.h"
#include "simcom_gpio.h"
#include "app_config.h"
#include "app_utils.h"
#include "drv_en25qh64a.h"
#include <string.h>

static SC_SPI_DEV s_spiDev;
static int s_isInitialized = 0;
static sMutexRef s_spiMutex = NULL;

/*
 * Mạch CS Flash (NPN C1815 đảo logic) — theo schematic:
 *  - MCU GPIO_05 HIGH → transistor dẫn → Flash CS# = 0V (Active Low)
 *  - MCU GPIO_05 LOW  → transistor ngắt → Flash CS# = 3.3V (Inactive)
 *
 * Phải dùng CUS_CS_MODE + GPIO thủ công: GPIO_MODE/SSP_MODE của SDK kéo CS
 * phần cứng xuống LOW để chọn chip — không khớp mạch NPN đảo trên GPIO_05.
 */
static void EN25_CS_Assert(void)
{
    /* HIGH trên GPIO_05 → chọn flash */
    sAPI_GpioSetValue(FLASH_SPI_CS_PIN, 1);
    sAPI_SpiCustomCsSetLevel(s_spiDev.index, 1);
}

static void EN25_CS_Deassert(void)
{
    /* LOW trên GPIO_05 → bỏ chọn flash */
    sAPI_GpioSetValue(FLASH_SPI_CS_PIN, 0);
    sAPI_SpiCustomCsSetLevel(s_spiDev.index, 0);
}

static void EN25_Lock(void)
{
    if (s_spiMutex) sAPI_MutexLock(s_spiMutex, SC_SUSPEND);
}

static void EN25_Unlock(void)
{
    if (s_spiMutex) sAPI_MutexUnLock(s_spiMutex);
}

static void EN25_WriteEnable(void)
{
    uint8_t cmd = EN25_CMD_WRITE_ENABLE;
    EN25_CS_Assert();
    sAPI_SpiWriteBytesEx(&s_spiDev, &cmd, 1);
    EN25_CS_Deassert();
}

int EN25_IsBusy(void)
{
    uint8_t cmd = EN25_CMD_READ_STATUS1;
    uint8_t sr1 = 0xFF;

    EN25_CS_Assert();
    sAPI_SpiReadBytesEx(&s_spiDev, &cmd, 1, &sr1, 1);
    EN25_CS_Deassert();
    return (sr1 & EN25_SR1_WIP_BIT) ? 1 : 0;
}

int EN25_WaitReady(uint32_t timeoutMs)
{
    uint32_t startTick = GetTickNow();
    uint32_t timeoutTicks = (timeoutMs * SC_TICKS_PER_SECOND) / 1000;
    if (timeoutTicks == 0) timeoutTicks = 1;

    while (EN25_IsBusy()) {
        if ((GetTickNow() - startTick) >= timeoutTicks) {
            sAPI_Debug("[EN25] WaitReady Timeout!");
            return -1;
        }
        sAPI_TaskSleep(1);
    }
    return 0;
}

int EN25_ReadID(uint8_t *mfrId, uint16_t *devId)
{
    uint8_t cmd = EN25_CMD_READ_JEDEC_ID;
    uint8_t idBuf[3] = {0};
    SC_SPI_ReturnCode rc;

    if (!mfrId || !devId) return -1;

    EN25_CS_Assert();
    rc = sAPI_SpiReadBytesEx(&s_spiDev, &cmd, 1, idBuf, 3);
    EN25_CS_Deassert();

    sAPI_Debug("[EN25/RawID] rc=%d Byte0=0x%02X, Byte1=0x%02X, Byte2=0x%02X (spi=%d csPin=%d)",
               (int)rc, idBuf[0], idBuf[1], idBuf[2],
               (int)s_spiDev.index, FLASH_SPI_CS_PIN);

    if (rc != SC_SPI_RC_OK) return -1;

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
    uint8_t mfrId = 0;
    uint16_t devId = 0;
    int attempt;
    SC_GPIOConfiguration gpioCfg;

    if (s_spiMutex == NULL) {
        sAPI_MutexCreate(&s_spiMutex, SC_FIFO);
    }

    memset(&s_spiDev, 0, sizeof(s_spiDev));
#ifdef SPIDRV_SUPPORT
    /* Clock chậm lúc bring-up — ổn định hơn với dây/BJT; có thể nâng sau khi OK */
    s_spiDev.clock = SC_SPI_CLK_6_5MHZ;
#else
    s_spiDev.clock = SPI_CLOCK_6MHz;
#endif
    s_spiDev.mode = SPI_MODE_PH0_PO0; /* Mode 0: CPOL=0 CPHA=0 — chuẩn NOR flash */
    s_spiDev.csMode = CUS_CS_MODE;    /* CS thủ công (đảo logic qua BJT) */
    s_spiDev.index = FLASH_SPI_INDEX;

    if (sAPI_SpiConfigInitEx(&s_spiDev) != SC_SPI_RC_OK) {
        sAPI_Debug("[EN25] SpiConfigInitEx failed! index=%d", FLASH_SPI_INDEX);
        s_isInitialized = 0;
        return -1;
    }

    /*
     * sAPI_SpiCustomCs(sspport, pin): tham số 2 = PAD GPIO (không phải cờ 0/1).
     * Schematic: CS = GPIO_05 → pad 120.
     */
    if (sAPI_SpiCustomCs(s_spiDev.index, FLASH_SPI_CS_PIN) != SC_SPI_RC_OK) {
        sAPI_Debug("[EN25] SpiCustomCs failed! pin=%d", FLASH_SPI_CS_PIN);
    }

    /* GPIO_05 output, idle LOW = flash bỏ chọn (NPN ngắt, CS# = 3.3V) */
    memset(&gpioCfg, 0, sizeof(gpioCfg));
    gpioCfg.pinDir = SC_GPIO_OUT_PIN;
    gpioCfg.initLv = 0;
    gpioCfg.pinPull = SC_GPIO_PULLDN_ENABLE;
    gpioCfg.pinEd = SC_GPIO_NO_EDGE;
    gpioCfg.isr = NULL;
    gpioCfg.wu = NULL;
    if (sAPI_GpioConfig(FLASH_SPI_CS_PIN, gpioCfg) != SC_GPIORC_OK) {
        sAPI_Debug("[EN25] GpioConfig CS pin=%d failed", FLASH_SPI_CS_PIN);
        s_isInitialized = 0;
        return -1;
    }
    EN25_CS_Deassert();

    /* Cho flash ổn định nguồn sau khi CS idle */
    sAPI_TaskSleep(SC_TICKS_PER_SECOND / 50); /* ~20 ms */

    for (attempt = 0; attempt < 6; attempt++) {
        mfrId = 0;
        devId = 0;

        /* Thử luân phiên cả CS 1=Select (mạch BJT đảo) và CS 0=Select (kết nối trực tiếp) */
        if (EN25_ReadID(&mfrId, &devId) == 0 && EN25_IdLooksValid(mfrId, devId)) {
            sAPI_Debug("[EN25/Flash] Flash Init Success! MfrID=0x%02X DevID=0x%04X (spi=%d cs=%d)",
                       mfrId, devId, FLASH_SPI_INDEX, FLASH_SPI_CS_PIN);
            s_isInitialized = 1;
            return 0;
        }
        sAPI_Debug("[EN25] ReadID attempt %d fail MfrID=0x%02X DevID=0x%04X",
                   attempt + 1, mfrId, devId);
        sAPI_TaskSleep(SC_TICKS_PER_SECOND / 20); /* ~50 ms */
    }

    sAPI_Debug("[EN25/Flash] Flash Init Failed or Unplugged (MfrID=0x%02X, DevID=0x%04X) spi=%d csPin=%d",
               mfrId, devId, FLASH_SPI_INDEX, FLASH_SPI_CS_PIN);
    s_isInitialized = 0;
    return -1;
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
    if (sAPI_SpiReadBytesEx(&s_spiDev, cmdBlock, 4, buf, len) != SC_SPI_RC_OK) {
        EN25_CS_Deassert();
        goto out;
    }
    EN25_CS_Deassert();
    rc = 0;

out:
    EN25_Unlock();
    return rc;
}

int EN25_WritePage(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint8_t cmdBlock[4];

    if (!s_isInitialized || !buf || len == 0 || len > EN25_PAGE_SIZE || (addr + len) > EN25_FLASH_SIZE) return -1;
    if (EN25_WaitReady(500) < 0) return -1;

    EN25_WriteEnable();

    cmdBlock[0] = EN25_CMD_PAGE_PROGRAM;
    cmdBlock[1] = (uint8_t)((addr >> 16) & 0xFF);
    cmdBlock[2] = (uint8_t)((addr >> 8) & 0xFF);
    cmdBlock[3] = (uint8_t)(addr & 0xFF);

    /* Giữ CS liên tục qua cả lệnh + data — bắt buộc với Page Program */
    EN25_CS_Assert();
    if (sAPI_SpiWriteBytesEx(&s_spiDev, cmdBlock, 4) != SC_SPI_RC_OK) {
        EN25_CS_Deassert();
        return -1;
    }
    if (sAPI_SpiWriteBytesEx(&s_spiDev, (unsigned char *)buf, len) != SC_SPI_RC_OK) {
        EN25_CS_Deassert();
        return -1;
    }
    EN25_CS_Deassert();

    return EN25_WaitReady(1000);
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

        if (EN25_WritePage(addr, buf, bytesToWrite) < 0) goto out;

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
    if (sAPI_SpiWriteBytesEx(&s_spiDev, cmdBlock, 4) != SC_SPI_RC_OK) {
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
    if (sAPI_SpiWriteBytesEx(&s_spiDev, cmdBlock, 4) != SC_SPI_RC_OK) {
        EN25_CS_Deassert();
        goto out;
    }
    EN25_CS_Deassert();

    rc = EN25_WaitReady(5000);

out:
    EN25_Unlock();
    return rc;
}
