/**
 * @file    board_pins.h
 * @brief   Chân / kênh phần cứng của board RV26 trên module SIMCom A7677S
 *
 * Mọi số pad GPIO, kênh SPI / I2C / ADC phụ thuộc module nằm ở đây, để
 * app/app_config.h không chứa số chân của module. Mỗi port (port/<chip>/) cung
 * cấp một board_pins.h riêng với cùng tên macro. Các giá trị có #ifndef nên
 * vẫn ghi đè được bằng -D khi build.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/*
 * GPIO board RV26 trên A7677S — pad từ A7670C_MANS_LANS_GPIO.h (cùng họ
 * ASR1606). SC_MODULE_GPIO_00 chỉ có khi FEATURE_SIMCOM_NORI; dùng pad số cố
 * định. Đổi khi schematic PCB khác.
 *
 * Theo schematic_TRACKING (KiCad, 15/06/2026):
 *  - LED_GPS  pin4  (GPIO_00, pad 80) / LED_MEM pin5 (GPIO_01, pad 32) / LED_NET pin7 (GPIO_03, pad 74)
 *  - ACC      pin52 (GPIO_09, pad 28): Q5 C1815 đảo, kéo lên 10K về 1V8 -> mức thấp = ACC bật
 *  - GPIO_OUT pin48 (GPIO_08, pad 77): Q4 C1815 hở collector ra J2 chân 4 (mức cao = đóng)
 *  - GSENSOR_INT pin36 (GPIO_14, pad 12) = INT1 LIS3DSH
 * CS flash ở pin12 (pad 17) — không được dùng chung với LED/DOUT.
 */
#ifndef GPIO_LED_NET
#define GPIO_LED_NET 74 /* SC_MODULE_GPIO_03 / pin7 (LED_NET) */
#endif
#ifndef GPIO_LED_PWR
#define GPIO_LED_PWR 32 /* SC_MODULE_GPIO_01 / pin5 (LED_MEM) */
#endif
#ifndef GPIO_LED_GNSS
#define GPIO_LED_GNSS 80 /* SC_MODULE_GPIO_00 / pin4 (LED_GPS) */
#endif
#ifndef GPIO_DOUT
#define GPIO_DOUT 77 /* SC_MODULE_GPIO_08 / pin48 (GPIO_OUT) */
#endif
#ifndef GPIO_ACC_IN
#define GPIO_ACC_IN 28 /* SC_MODULE_GPIO_09 / pin52 (ACC) */
#endif

/*
 * SPI flash rời EN25QH64A trên A7677S MANS (SC_SPI_1606_PIN):
 *  - SPI channel 0 = SSP0 data (da xac nhan tren board: JEDEC ID EF 40 17): pin11 CLK (pad16), pin13 MOSI (pad19), pin14 MISO (pad18)
 *  - CS# Flash: pin12 (pad 17, GPIO_35/SSP0_CS), driver tự lái bằng GPIO.
 *    Mức chọn chip FLASH_SPI_CS_ACTIVE_LEVEL được thử trước; EN25_Init vẫn thử
 *    mức ngược lại để chẩn đoán (board cũ: pin19 qua NPN C1815 đảo logic, mức 1)
 */
#ifndef FLASH_SPI_INDEX
#define FLASH_SPI_INDEX 0
#endif
#ifndef FLASH_SPI_CS_PIN
#define FLASH_SPI_CS_PIN 17 /* SC_MODULE_GPIO_35 / pin12 */
#endif
#ifndef FLASH_SPI_CS_ACTIVE_LEVEL
#define FLASH_SPI_CS_ACTIVE_LEVEL 0 /* nối thẳng CS# (active low) */
#endif

/* INT1 của LIS3DSH: GSENSOR_INT pin36 (pad 12). Đặt -1 nếu board không nối INT1 */
#ifndef LIS3DSH_INT1_PIN
#define LIS3DSH_INT1_PIN 12 /* SC_MODULE_GPIO_14 / pin36 */
#endif

#if (GPIO_LED_PWR == FLASH_SPI_CS_PIN) || (GPIO_LED_NET == FLASH_SPI_CS_PIN) || \
    (GPIO_LED_GNSS == FLASH_SPI_CS_PIN) || (GPIO_DOUT == FLASH_SPI_CS_PIN) || \
    (GPIO_ACC_IN == FLASH_SPI_CS_PIN) || (LIS3DSH_INT1_PIN == FLASH_SPI_CS_PIN)
#error "FLASH_SPI_CS_PIN xung đột với GPIO LED/DOUT/ACC/INT1 — Flash CS phải chân riêng"
#endif
#if (LIS3DSH_INT1_PIN >= 0) && ((GPIO_LED_PWR == LIS3DSH_INT1_PIN) || (GPIO_LED_NET == LIS3DSH_INT1_PIN) || \
    (GPIO_LED_GNSS == LIS3DSH_INT1_PIN) || (GPIO_DOUT == LIS3DSH_INT1_PIN) || (GPIO_ACC_IN == LIS3DSH_INT1_PIN))
#error "LIS3DSH_INT1_PIN xung đột với GPIO LED/DOUT/ACC"
#endif

/* Đo điện áp nguồn: ADC_VIN nối ADC2 (ADC1 thả nổi). sAPI_ReadAdc: 1 = ADC1, 2 = ADC2 */
#ifndef VOLT_ADC_CHANNEL
#define VOLT_ADC_CHANNEL 2
#endif

/* LIS3DSH: kênh 0 = CI2C (pin37 SDA / pin38 SCL trên họ A767x) — đã xác nhận trên board */
#ifndef LIS3DSH_I2C_CHANNEL
#define LIS3DSH_I2C_CHANNEL 0
#endif

#endif /* BOARD_PINS_H */
