/*** 
 * @Author: yangkui.zhang
 * @Date: 2024-08-01 20:19:30
 * @LastEditTime: 2024-08-02 10:28:07
 * @LastEditors: yangkui.zhang
 * @Description: 
 * @FilePath: \ASR1803S\simcom\userspace\sc_lib\inc\GPIO\A7602E_H_TAME_GPIO.h
 * @no error,no warning
 */
#ifndef __A7602E_H_TAME_GPIO_H__
#define __A7602E_H_TAME_GPIO_H__

/* A7670SA_V101 GPIO Resource Sheet  */
typedef enum
{
#ifndef WIFI_DISABLE
    SC_MODULE_GPIO_05 = 5,     /*  pin 45, pad name: GPIO_05 */
    SC_MODULE_GPIO_14 = 14,    /*  pin 51, pad name: GPIO_14 */
    SC_MODULE_GPIO_21 = 21,    /*  pin 49, pad name: GPIO_21 */
    SC_MODULE_GPIO_24 = 24,    /*  pin 70, pad name: GPIO_24 */
    SC_MODULE_GPIO_25 = 25,    /*  pin 76, pad name: GPIO_25 */
    SC_MODULE_GPIO_26 = 26,    /*  pin 75, pad name: GPIO_26 */
    SC_MODULE_GPIO_31 = 31,    /*  pin 67, pad name: GPIO_31 */
    SC_MODULE_GPIO_32 = 32,    /*  pin 66, pad name: GPIO_32 */
    SC_MODULE_GPIO_49 = 49,    /*  pin 55, pad name: GPIO_49 */
    SC_MODULE_GPIO_50 = 50,    /*  pin 56, pad name: GPIO_50 */
    SC_MODULE_GPIO_99 = 99,    /*  pin 16, pad name: GPIO_99 */
#else
    SC_MODULE_GPIO_00 = 0,     /*  pin 127, pad name: GPIO_00 */
    SC_MODULE_GPIO_01 = 1,     /*  pin 162, pad name: GPIO_01 */
    SC_MODULE_GPIO_02 = 2,     /*  pin 163, pad name: GPIO_02 */
    SC_MODULE_GPIO_03 = 3,     /*  pin 128, pad name: GPIO_03 */
    SC_MODULE_GPIO_05 = 5,     /*  pin 45, pad name: GPIO_05 */
    SC_MODULE_GPIO_06 = 6,     /*  pin 161, pad name: GPIO_06 */
    SC_MODULE_GPIO_07 = 7,     /*  pin 160, pad name: GPIO_07 */
    SC_MODULE_GPIO_12 = 12,    /*  pin 33, pad name: GPIO_12 */
    SC_MODULE_GPIO_14 = 14,    /*  pin 51, pad name: GPIO_14 */
    SC_MODULE_GPIO_15 = 15,    /*  pin 126, pad name: GPIO_15 */
    SC_MODULE_GPIO_16 = 16,    /*  pin 120, pad name: GPIO_16 */
    SC_MODULE_GPIO_17 = 17,    /*  pin 125, pad name: GPIO_17 */
    SC_MODULE_GPIO_18 = 18,    /*  pin 129, pad name: GPIO_18 */
    SC_MODULE_GPIO_21 = 21,    /*  pin 49, pad name: GPIO_21 */
    SC_MODULE_GPIO_24 = 24,    /*  pin 70, pad name: GPIO_24 */
    SC_MODULE_GPIO_31 = 31,    /*  pin 67, pad name: GPIO_31 */
    SC_MODULE_GPIO_32 = 32,    /*  pin 66, pad name: GPIO_32 */
    SC_MODULE_GPIO_48 = 48,    /*  pin 31, pad name: GPIO_48 */
    SC_MODULE_GPIO_49 = 49,    /*  pin 55, pad name: GPIO_49 */
    SC_MODULE_GPIO_50 = 50,    /*  pin 56, pad name: GPIO_50 */
    SC_MODULE_GPIO_55 = 55,    /*  pin 28, pad name: GPIO_55 */
    SC_MODULE_GPIO_56 = 56,    /*  pin 27, pad name: GPIO_56 */
    SC_MODULE_GPIO_57 = 57,    /*  pin 30, pad name: GPIO_57 */
    SC_MODULE_GPIO_58 = 58,    /*  pin 29, pad name: GPIO_58 */
    SC_MODULE_GPIO_59 = 59,    /*  pin 32, pad name: GPIO_59 */
    SC_MODULE_GPIO_99 = 99,    /*  pin 16, pad name: GPIO_99 */
    SC_MODULE_GPIO_125 = 125,    /*  pin 115, pad name: GPIO_125 */
#endif

    SC_MODULE_GPIO_MAX = 128,
} SC_Module_GPIONumbers;

#endif


