/**
 * @file    port_periph.c
 * @brief   HAL_GPIO, HAL_SPI, HAL_I2C, HAL_ADC: ngoai vi cua module -- SIMCom A7677S OpenSDK
 *
 * Chi cac file trong port/ duoc goi sAPI_*. Ham SDK dung o day: SDK_API_LIST.md.
 */

#include "port_sdk.h"

/* ========================== HAL_GPIO ====================================== */

void HAL_GPIO_Config(int pin, HalGpioDir_t dir, HalGpioPull_t pull)
{
    SC_GPIOConfiguration cfg;
    cfg.pinDir  = (dir == HAL_GPIO_OUT) ? SC_GPIO_OUT_PIN : SC_GPIO_IN_PIN;
    cfg.initLv  = (dir == HAL_GPIO_OUT) ? 0 : 1;
    cfg.pinPull = (pull == HAL_GPIO_PULL_UP)   ? SC_GPIO_PULLUP_ENABLE :
                  (pull == HAL_GPIO_PULL_DOWN) ? SC_GPIO_PULLDN_ENABLE :
                                                 SC_GPIO_PULL_DISABLE;
    cfg.pinEd   = SC_GPIO_NO_EDGE;
    cfg.isr     = NULL;
    cfg.wu      = NULL;
    {
        SC_GPIOReturnCode rc = sAPI_GpioConfig(pin, cfg);
        if (rc != SC_GPIORC_OK) {
            HAL_LOG_Print("[GPIO] config pin %d fail rc=%d", pin, (int)rc);
        }
    }
}

void HAL_GPIO_Write(int pin, int level)
{
    sAPI_GpioSetValue(pin, level);
}

int HAL_GPIO_Read(int pin)
{
    /* SDK tra ve muc 0/1 hoac ma loi am (SC_GPIORC_INVALID_*): khong de ma loi
     * bi hieu nham thanh muc HIGH */
    int rc = (int)sAPI_GpioGetValue(pin);
    if (rc == SC_GPIORC_LOW || rc == SC_GPIORC_HIGH) return rc;
    return -1;
}

/*
 * SDK goi isr / wu khong kem so chan, nen moi chan danh thuc can mot cap ham
 * trampoline rieng (slot). Hien co 2 slot: ACC va INT1 cam bien gia toc.
 */
#define HAL_GPIO_WAKE_SLOTS 2

static volatile HalGpioWakeCb_t s_gpioWakeCb[HAL_GPIO_WAKE_SLOTS] = {NULL};
static int s_gpioWakePin[HAL_GPIO_WAKE_SLOTS] = {-1, -1};

static void GpioIsrSlot(int slot)
{
    HalGpioWakeCb_t cb = s_gpioWakeCb[slot];
    if (cb) cb();
}

static void GpioWakeSlot(int slot)
{
    HalGpioWakeCb_t cb = s_gpioWakeCb[slot];
    /* Theo demo_gpio.c: muon o lai trang thai thuc sau khi duoc danh thuc thi
     * phai tat sleep he thong; task NASA se bat lai neu van du dieu kien ngu */
    sAPI_SystemSleepSet(SC_SYSTEM_SLEEP_DISABLE);
    if (cb) cb();
}

static void GpioIsrTrampoline0(void)  { GpioIsrSlot(0); }
static void GpioWakeTrampoline0(void) { GpioWakeSlot(0); }
static void GpioIsrTrampoline1(void)  { GpioIsrSlot(1); }
static void GpioWakeTrampoline1(void) { GpioWakeSlot(1); }

static void (*const s_gpioIsrTramp[HAL_GPIO_WAKE_SLOTS])(void)  = { GpioIsrTrampoline0, GpioIsrTrampoline1 };
static void (*const s_gpioWakeTramp[HAL_GPIO_WAKE_SLOTS])(void) = { GpioWakeTrampoline0, GpioWakeTrampoline1 };

int HAL_GPIO_WakeupEnable(int pin, HalGpioEdge_t edge, HalGpioWakeCb_t cb)
{
    SC_GPIOConfiguration cfg;
    SC_GPIOTransitionType ed = (edge == HAL_GPIO_EDGE_RISE) ? SC_GPIO_RISE_EDGE :
                               (edge == HAL_GPIO_EDGE_FALL) ? SC_GPIO_FALL_EDGE :
                                                              SC_GPIO_TWO_EDGE;
    SC_GPIOReturnCode rc1, rc2;
    int slot;

    /* Chan da dang ky thi dung lai slot cu, khong thi lay slot trong */
    for (slot = 0; slot < HAL_GPIO_WAKE_SLOTS; slot++) {
        if (s_gpioWakePin[slot] == pin) break;
    }
    if (slot == HAL_GPIO_WAKE_SLOTS) {
        for (slot = 0; slot < HAL_GPIO_WAKE_SLOTS; slot++) {
            if (s_gpioWakePin[slot] < 0) break;
        }
    }
    if (slot == HAL_GPIO_WAKE_SLOTS) {
        HAL_LOG_Print("[GPIO] wakeup pin %d: no free slot", pin);
        return -1;
    }
    s_gpioWakePin[slot] = pin;
    s_gpioWakeCb[slot] = cb;
    cfg.pinDir  = SC_GPIO_IN_PIN;
    cfg.initLv  = 1;
    cfg.pinPull = SC_GPIO_PULLUP_ENABLE;
    cfg.pinEd   = ed;
    cfg.isr     = s_gpioIsrTramp[slot];
    cfg.wu      = s_gpioWakeTramp[slot];
    rc1 = sAPI_GpioConfig(pin, cfg);
    rc2 = sAPI_GpioWakeupEnable(pin, ed);
    HAL_LOG_Print("[GPIO] wakeup pin %d edge %d config rc=%d wakeup rc=%d",
                  pin, (int)ed, (int)rc1, (int)rc2);
    return (rc1 == SC_GPIORC_OK && rc2 == SC_GPIORC_OK) ? 0 : -1;
}

/* ========================== HAL_SPI ======================================= */

/*
 * Cau hinh SPI that su truyen cho SDK. HalSpiDev_t (8 byte) KHONG cung bo cuc
 * voi SC_SPI_DEV (index/clock/mode/csMode) nen khong duoc ep kieu truc tiep:
 * SDK se doc clock/mode/csMode tu vung nho ngoai handle.
 */
static SC_SPI_DEV s_spiCfg;

int HAL_SPI_Init(HalSpiDev_t *dev, int spiIndex)
{
    SC_SPI_ReturnCode rc;

    if (!dev) return -1;
    dev->index = spiIndex;

    memset(&s_spiCfg, 0, sizeof(s_spiCfg));
    s_spiCfg.index = (unsigned char)spiIndex;
    /* Clock thap: bus di qua IC chuyen muc dung dien tro keo len nen suon len cham */
    s_spiCfg.clock = SPI_CLOCK_812KHz;
    s_spiCfg.mode = SPI_MODE_PH0_PO0;   /* SPI mode 0: NOR flash lay mau o canh len */
    s_spiCfg.csMode = GPIO_MODE;        /* CS cua SDK giu suot chuoi byte; CS flash do driver tu lai */
    rc = sAPI_SpiConfigInitEx(&s_spiCfg);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_Write(HalSpiDev_t *dev, const uint8_t *data, uint32_t len)
{
    SC_SPI_ReturnCode rc;
    (void)dev;
    rc = sAPI_SpiWriteBytesEx(&s_spiCfg, (unsigned char *)data, (unsigned int)len);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_Read(HalSpiDev_t *dev,
                 const uint8_t *cmd, uint32_t cmdLen,
                 uint8_t *rxBuf, uint32_t rxLen)
{
    SC_SPI_ReturnCode rc;
    (void)dev;
    rc = sAPI_SpiReadBytesEx(&s_spiCfg, (unsigned char *)cmd, (unsigned int)cmdLen,
                             (unsigned char *)rxBuf, (unsigned int)rxLen);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

int HAL_SPI_CustomCsSetLevel(HalSpiDev_t *dev, int level)
{
    SC_SPI_ReturnCode rc;
    if (!dev) return -1;
    rc = sAPI_SpiCustomCsSetLevel(dev->index, level);
    return (rc == SC_SPI_RC_OK) ? 0 : -1;
}

/* ========================== HAL_I2C ======================================= */

/*
 * SDK nhan dia chi slave dang 8 bit (7 bit dich trai 1): demo_i2c.c goi NAU8810
 * voi 0x34, ma dia chi 7 bit cua NAU8810 la 0x1A.
 */
#ifndef HAL_I2C_SDK_ADDR
#define HAL_I2C_SDK_ADDR(a7) ((UINT8)((a7) << 1))
#endif

int HAL_I2C_Init(int channel, int fast)
{
    SC_I2C_DEV dev;

    memset(&dev, 0, sizeof(dev));
    dev.i2c_channel = (SC_I2C_CHANNEL)channel;
    dev.i2c_clock = fast ? SC_I2C_FAST_MODE : SC_I2C_STANDARD_MODE;
    return (sAPI_I2CConfigInit(&dev) == SC_I2C_RC_OK) ? 0 : -1;
}

int HAL_I2C_ReadReg(int channel, uint8_t addr7, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (!buf || len == 0) return -1;
    return (sAPI_I2CRead(channel, HAL_I2C_SDK_ADDR(addr7), reg, buf, len) == SC_I2C_RC_OK) ? 0 : -1;
}

int HAL_I2C_WriteReg(int channel, uint8_t addr7, uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return -1;
    return (sAPI_I2CWrite(channel, HAL_I2C_SDK_ADDR(addr7), reg, (UINT8 *)data, len) == SC_I2C_RC_OK) ? 0 : -1;
}

/* ========================== HAL_ADC ======================================= */

uint32_t HAL_ADC_ReadMv(int channel)
{
    /* sAPI_ReadAdc tra ve gia tri dien ap tai chan ADC tinh bang mV */
    unsigned int mv = sAPI_ReadAdc(channel);
    return (uint32_t)mv;
}
