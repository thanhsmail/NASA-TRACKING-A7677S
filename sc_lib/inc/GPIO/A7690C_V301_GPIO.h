#ifndef __A7690C_V301_GPIO_H__
#define __A7690C_V301_GPIO_H__

typedef enum
{

    SC_MODULE_GPIO_01 = 69, /*  pin 16, pad name: GPIO_01/PWM1     */
    SC_MODULE_GPIO_02 = 37, /*  pin 21, pad name: GPIO_02          */
    SC_MODULE_GPIO_03 = 5,  /*  pin 22, pad name: GPIO_03/MAIN_CTS */
    SC_MODULE_GPIO_04 = 4,  /*  pin 23, pad name: GPIO_04/MAIN_RTS */
    SC_MODULE_GPIO_05 = 70, /*  pin 25, pad name: GPIO_05/PWM2     */
    SC_MODULE_GPIO_06 = 33, /*  pin 30, pad name: GPIO_06/SPI_CLK  */
    SC_MODULE_GPIO_07 = 34, /*  pin 31, pad name: GPIO_07/SPI_CS   */
    SC_MODULE_GPIO_08 = 35, /*  pin 32, pad name: GPIO_08/SPI_MISO */
    SC_MODULE_GPIO_09 = 36, /*  pin 33, pad name: GPIO_09/SPI_MOSI */
    SC_MODULE_GPIO_10 = 50, /*  pin 66, pad name: GPIO_10/I2C_SDA  */
    SC_MODULE_GPIO_11 = 49, /*  pin 67, pad name: GPIO_11/I2C_SCL  */
    SC_MODULE_GPIO_15 = 9,  /*  pin 82, pad name: GPIO_15/USB_BOOT */
    SC_MODULE_GPIO_16 = 89, /*  pin 84, pad name: GPIO_16/MK_IN1   */
    SC_MODULE_GPIO_17 = 56, /*  pin 85, pad name: GPIO_17/MK_OUT1  */
    SC_MODULE_GPIO_18 = 85, /*  pin 86, pad name: GPIO_18/MK_IN0   */
    SC_MODULE_GPIO_19 = 88, /*  pin 87, pad name: GPIO_19/MK_OUT0  */
    SC_MODULE_GPIO_22 = 7,  /*  pin 20, pad name: GPIO_22          */

#ifdef FEATURE_SIMCOM_NODTR
    SC_MODULE_GPIO_23 = 6, /*pin 19, pad name: DTR                */
#endif

    SC_MODULE_GPIO_MAX = 128,
} SC_Module_GPIONumbers;

#endif
