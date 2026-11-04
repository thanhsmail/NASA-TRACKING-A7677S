#ifndef _DRV_LIS3DSH_H_
#define _DRV_LIS3DSH_H_

#include <stdint.h>

/* Dia chi I2C 7 bit: SEL/SDO noi Vdd -> 0x1D, noi GND -> 0x1E (CS phai noi Vdd_IO) */
#define LIS3DSH_ADDR_SEL_HIGH   0x1D
#define LIS3DSH_ADDR_SEL_LOW    0x1E
#define LIS3DSH_WHO_AM_I_VAL    0x3F

/* Thanh ghi (datasheet DocID022405 Rev 3, Table 15) */
#define LIS3DSH_REG_OUT_T       0x0C
#define LIS3DSH_REG_WHO_AM_I    0x0F
#define LIS3DSH_REG_CTRL_REG4   0x20 /* ODR[7:4] BDU Zen Yen Xen */
#define LIS3DSH_REG_CTRL_REG1   0x21 /* SM1 */
#define LIS3DSH_REG_CTRL_REG2   0x22 /* SM2 */
#define LIS3DSH_REG_CTRL_REG3   0x23 /* DR_EN IEA IEL INT2_EN INT1_EN VFILT - STRT */
#define LIS3DSH_REG_CTRL_REG5   0x24 /* BW[7:6] FSCALE[5:3] ST[2:1] SIM */
#define LIS3DSH_REG_CTRL_REG6   0x25 /* BOOT FIFO_EN WTM_EN ADD_INC ... */
#define LIS3DSH_REG_STATUS      0x27
#define LIS3DSH_REG_OUT_X_L     0x28 /* X/Y/Z L,H lien tiep 0x28..0x2D */
#define LIS3DSH_REG_ST1_1       0x40 /* SM1 code ST1_1..ST1_16 (0x40..0x4F, chi ghi) */
#define LIS3DSH_REG_THRS1_1     0x57
#define LIS3DSH_REG_MASK1_B     0x59
#define LIS3DSH_REG_MASK1_A     0x5A
#define LIS3DSH_REG_SETT1       0x5B
#define LIS3DSH_REG_OUTS1       0x5F /* doc = xoa ngat SM1 (che do chot) */

/* ODR (CTRL_REG4[7:4]) */
#define LIS3DSH_ODR_POWER_DOWN  0x0
#define LIS3DSH_ODR_3_125HZ     0x1
#define LIS3DSH_ODR_6_25HZ      0x2
#define LIS3DSH_ODR_12_5HZ      0x3
#define LIS3DSH_ODR_25HZ        0x4
#define LIS3DSH_ODR_50HZ        0x5
#define LIS3DSH_ODR_100HZ       0x6

#define LIS3DSH_CTRL4_BDU       0x08
#define LIS3DSH_CTRL4_XYZ_EN    0x07
#define LIS3DSH_CTRL3_STRT      0x01
#define LIS3DSH_CTRL3_INT1_EN   0x08 /* IEA = 0: tich cuc muc thap; IEL = 0: chot */
#define LIS3DSH_CTRL1_SM1_EN    0x01 /* SM1_PIN = 0: ra INT1 */
#define LIS3DSH_SETT1_SITR      0x01
#define LIS3DSH_MASK_XYZ        0xFC /* +-X +-Y +-Z, bo vector V */
#define LIS3DSH_CTRL5_BW_50HZ   0xC0
#define LIS3DSH_CTRL5_ST_POS    0x02 /* ST2:ST1 = 01: self-test duong */
#define LIS3DSH_CTRL6_ADD_INC   0x10
#define LIS3DSH_STATUS_ZYXDA    0x08

/* API Driver */
int LIS3DSH_Init(void);
int LIS3DSH_IsReady(void);
int LIS3DSH_ReadXYZ(int16_t mg[3]);
int LIS3DSH_SelfTest(int16_t deltaMg[3]);
int LIS3DSH_MotionArm(int deltaMg, int *thrMg);
int LIS3DSH_MotionDisarm(void);
int LIS3DSH_MotionClear(void);

#endif /* _DRV_LIS3DSH_H_ */
