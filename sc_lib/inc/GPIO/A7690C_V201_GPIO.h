/*** 
 * @Author: yangkui.zhang
 * @Date: 2024-01-12 15:16:59
 * @LastEditTime: 2024-04-02 17:37:31
 * @LastEditors: yangkui.zhang
 * @Description: 
 * @FilePath: \ASR160X\simcom\userspace\sc_lib\inc\GPIO\A7690C_V201_GPIO.h
 * @no error,no warning
 */
#ifndef __A7690C_LNXV_V201_GPIO_H__
#define __A7680C_LNXV_V201_GPIO_H__

/* A7680C_MNNV_LNNV GPIO Resource Sheet  */
typedef enum
{
#ifdef CUS_PUWELL
    SC_MODULE_GPIO_00 = 53,  /*  pin 23, pad name: GPIO_00/MAIN_RTS */
    SC_MODULE_GPIO_01 = 54,  /*  pin 22, pad name: GPIO_01/MAIN_CTS */
    SC_MODULE_GPIO_02 = 5,   /*  pin 21, pad name: GPIO_02 */
    SC_MODULE_GPIO_04 = 4,   /*  pin 20, pad name: GPIO_04 */
    SC_MODULE_GPIO_05 = 9,   /*  pin 82, pad name: GPIO_05 */
    SC_MODULE_GPIO_06 = 11,  /*  pin 66, pad name: GPIO_06/UART3_TX */
    SC_MODULE_GPIO_07 = 10,  /*  pin 67, pad name: GPIO_07/UART3_RX */
    SC_MODULE_GPIO_09 = 69,  /*  pin 16, pad name: GPIO_09/PWM1 */
    SC_MODULE_GPIO_15 = 14,  /*  pin 28, pad name: GPIO_15/SPI2_MISO */
    SC_MODULE_GPIO_16 = 15,  /*  pin 29, pad name: GPIO_16/SPI2_MOSI */
    SC_MODULE_GPIO_17 = 50,  /*  pin 69, pad name: GPIO_17/CI2C_SDA */
    SC_MODULE_GPIO_18 = 49,  /*  pin 68, pad name: GPIO_18/CI2C_SCL */
    SC_MODULE_GPIO_25 = 33,  /*  pin 30, pad name: GPIO_25/LCD_SPI_CLK */
    SC_MODULE_GPIO_26 = 34,  /*  pin 31, pad name: GPIO_26/LCD_SPI_CS */
    SC_MODULE_GPIO_28 = 35,  /*  pin 32, pad name: GPIO_28/LCD_SPI_RXD */
    SC_MODULE_GPIO_29 = 36,  /*  pin 33, pad name: GPIO_29/LCD_SPI_TXD */
    SC_MODULE_GPIO_30 = 70,  /*  pin 25, pad name: GPIO_30/PWM2 */
    SC_MODULE_GPIO_31 = 1,   /*  pin 85, pad name: GPIO_31 */
    SC_MODULE_GPIO_32 = 73,  /*  pin 86, pad name: GPIO_32 */
    SC_MODULE_GPIO_33 = 74,  /*  pin 87, pad name: GPIO_33 */
    SC_MODULE_GPIO_34 = 0,   /*  pin 84, pad name: GPIO_34 */
#else
    SC_MODULE_GPIO_01 = 69,  /*  pin 16, pad name: GPIO_01/PWM1 */
    SC_MODULE_GPIO_02 = 5,   /*  pin 21, pad name: GPIO_02 */
    SC_MODULE_GPIO_03 = 54,  /*  pin 22, pad name: GPIO_03/MAIN_CTS */
    SC_MODULE_GPIO_04 = 53,  /*  pin 23, pad name: GPIO_04/MAIN_RTS */
    SC_MODULE_GPIO_05 = 70,  /*  pin 25, pad name: GPIO_05/PWM2 */
    SC_MODULE_GPIO_06 = 33,  /*  pin 30, pad name: GPIO_06/SPI_CLK */
    SC_MODULE_GPIO_07 = 34,  /*  pin 31, pad name: GPIO_07/SPI_CS */
    SC_MODULE_GPIO_08 = 35,  /*  pin 32, pad name: GPIO_08/SPI_MISO */
    SC_MODULE_GPIO_09 = 36,  /*  pin 33, pad name: GPIO_09/SPI_MOSI */
    SC_MODULE_GPIO_10 = 11,  /*  pin 66, pad name: GPIO_10/I2C2_SDA */
    SC_MODULE_GPIO_11 = 10,  /*  pin 67, pad name: GPIO_11/I2C2_SCL */
    SC_MODULE_GPIO_12 = 49,  /*  pin 68, pad name: GPIO_12/I2C_SCL */
    SC_MODULE_GPIO_13 = 50,  /*  pin 69, pad name: GPIO_13/I2C_SDA */
    SC_MODULE_GPIO_15 = 9,   /*  pin 82, pad name: GPIO_15 */
    SC_MODULE_GPIO_16 = 0,   /*  pin 84, pad name: GPIO_16/MK_IN2 */
    SC_MODULE_GPIO_17 = 1,   /*  pin 85, pad name: GPIO_17/MK_OUT2 */
    SC_MODULE_GPIO_18 = 73,  /*  pin 86, pad name: GPIO_18/MK_IN0 */
    SC_MODULE_GPIO_19 = 74,  /*  pin 87, pad name: GPIO_19/MK_OUT0 */
    SC_MODULE_GPIO_20 = 14,  /*  pin 28, pad name: GPIO_20/UART3_RXD/MK_IN1*/
    SC_MODULE_GPIO_21 = 15,  /*  pin 29, pad name: GPIO_21/UART3_TXD/MK_OUT1 */
    SC_MODULE_GPIO_22 = 4,   /*  pin 20, pad name: GPIO_22 */
#endif

#ifdef FEATURE_SIMCOM_NODTR
    SC_MODULE_GPIO_35 = 6,   /*  pin 19, pad name: DTR  */
#endif

    SC_MODULE_GPIO_MAX = 128,
} SC_Module_GPIONumbers;

#endif
