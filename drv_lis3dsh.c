/**
 * @file drv_lis3dsh.c
 * @brief Cam bien gia toc 3 truc LIS3DSH (ST) qua I2C
 *
 * Luu y: File nay KHONG #include simcom_api.h truc tiep.
 * Moi tuong tac phan cung di qua HAL layer.
 *
 * Cau hinh mac dinh: +-2 g (0.06 mg/LSB), loc chong rang cua 50 Hz, BDU,
 * ODR = LIS3DSH_ODR (app_config.h). Chua dung ngat / state machine.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_i2c.h"
#include "app_config.h"
#include "drv_lis3dsh.h"
#include <string.h>

#define LIS3DSH_MG_NUM 6   /* +-2 g: 0.06 mg/LSB = 6/100 */
#define LIS3DSH_MG_DEN 100

static uint8_t s_addr = 0;
static int     s_isInitialized = 0;
static int     s_fifoOn = 0;      /* FIFO stream (LIS3DSH_ReadAvgXYZ); tat khi self-test / nap SM */
static int     s_stRc = -2;       /* ket qua self-test gan nhat, -2 = chua chay */
static int16_t s_stDelta[3] = {0, 0, 0};

static int ReadReg(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_ReadReg(LIS3DSH_I2C_CHANNEL, s_addr, reg, buf, len);
}

static int WriteReg(uint8_t reg, uint8_t val)
{
    return HAL_I2C_WriteReg(LIS3DSH_I2C_CHANNEL, s_addr, reg, &val, 1);
}

/** Thu WHO_AM_I tren dia chi addr7; @return 0 neu dung LIS3DSH */
static int Probe(uint8_t addr7)
{
    uint8_t id = 0;

    if (HAL_I2C_ReadReg(LIS3DSH_I2C_CHANNEL, addr7, LIS3DSH_REG_WHO_AM_I, &id, 1) != 0) {
        HAL_LOG("[LIS3DSH] addr 0x%02X: no ACK", addr7);
        return -1;
    }
    if (id != LIS3DSH_WHO_AM_I_VAL) {
        HAL_LOG("[LIS3DSH] addr 0x%02X: WHO_AM_I=0x%02X (expect 0x%02X)", addr7, id, LIS3DSH_WHO_AM_I_VAL);
        return -1;
    }
    return 0;
}

/** Bat / tat FIFO stream; @return 0 neu thanh cong */
static int FifoSet(int on)
{
    if (on == s_fifoOn) return 0;
    if (on) {
        if (WriteReg(LIS3DSH_REG_FIFO_CTRL, LIS3DSH_FIFO_STREAM) != 0 ||
            WriteReg(LIS3DSH_REG_CTRL_REG6, LIS3DSH_CTRL6_ADD_INC | LIS3DSH_CTRL6_FIFO_EN) != 0) return -1;
    } else {
        /* Bypass xoa FIFO; OUT_x tro lai la mau moi nhat (self-test / SM doc STATUS) */
        if (WriteReg(LIS3DSH_REG_FIFO_CTRL, LIS3DSH_FIFO_BYPASS) != 0 ||
            WriteReg(LIS3DSH_REG_CTRL_REG6, LIS3DSH_CTRL6_ADD_INC) != 0) return -1;
    }
    s_fifoOn = on;
    return 0;
}

/** Cho mot bo XYZ moi (STATUS.ZYXDA) roi doc; @return 0 neu thanh cong */
static int ReadFreshXYZ(int16_t mg[3])
{
    int i;
    uint8_t st = 0;

    for (i = 0; i < 100; i++) { /* toi da 500 ms, du cho ODR >= 3.125 Hz */
        if (ReadReg(LIS3DSH_REG_STATUS, &st, 1) != 0) return -1;
        if (st & LIS3DSH_STATUS_ZYXDA) return LIS3DSH_ReadXYZ(mg);
        HAL_OS_TaskSleep(1);
    }
    return -1;
}

/** Trung binh n mau moi; @return 0 neu thanh cong */
static int AverageXYZ(int n, int32_t avg[3])
{
    int i, k;
    int16_t mg[3];

    avg[0] = avg[1] = avg[2] = 0;
    for (i = 0; i < n; i++) {
        if (ReadFreshXYZ(mg) != 0) return -1;
        for (k = 0; k < 3; k++) avg[k] += mg[k];
    }
    for (k = 0; k < 3; k++) avg[k] /= n;
    return 0;
}

int LIS3DSH_Init(void)
{
    uint8_t ctrl4 = (uint8_t)((LIS3DSH_ODR << 4) | LIS3DSH_CTRL4_BDU | LIS3DSH_CTRL4_XYZ_EN);
    uint8_t rb = 0;

    s_isInitialized = 0;
    s_fifoOn = 0; /* soft reset ben duoi dua FIFO ve bypass */
    if (HAL_I2C_Init(LIS3DSH_I2C_CHANNEL, 1) != 0) {
        HAL_LOG("[LIS3DSH] HAL_I2C_Init failed! channel=%d", LIS3DSH_I2C_CHANNEL);
        return -1;
    }

    /* Board RV26: SEL noi GND (0x1E, da xac nhan tren log). Van thu 0x1D cho board khac */
    if (Probe(LIS3DSH_ADDR_SEL_LOW) == 0) {
        s_addr = LIS3DSH_ADDR_SEL_LOW;
    } else if (Probe(LIS3DSH_ADDR_SEL_HIGH) == 0) {
        s_addr = LIS3DSH_ADDR_SEL_HIGH;
    } else {
        HAL_LOG("[LIS3DSH] not found on I2C channel %d", LIS3DSH_I2C_CHANNEL);
        return -1;
    }

    /* Soft reset (POR) de xoa cau hinh / state machine con sot lai */
    (void)WriteReg(LIS3DSH_REG_CTRL_REG3, LIS3DSH_CTRL3_STRT);
    HAL_OS_TaskSleep(10);

    if (WriteReg(LIS3DSH_REG_CTRL_REG5, LIS3DSH_CTRL5_BW_50HZ) != 0 ||  /* +-2 g, 4-wire, ST off */
        WriteReg(LIS3DSH_REG_CTRL_REG6, LIS3DSH_CTRL6_ADD_INC) != 0 || /* FIFO off, tu tang dia chi */
        WriteReg(LIS3DSH_REG_CTRL_REG3, 0x00) != 0 ||                  /* chua dung ngat */
        WriteReg(LIS3DSH_REG_CTRL_REG4, ctrl4) != 0) {
        HAL_LOG("[LIS3DSH] config write failed");
        return -1;
    }
    if (ReadReg(LIS3DSH_REG_CTRL_REG4, &rb, 1) != 0 || rb != ctrl4) {
        HAL_LOG("[LIS3DSH] CTRL_REG4 readback 0x%02X != 0x%02X", rb, ctrl4);
        return -1;
    }

    s_isInitialized = 1;
    HAL_LOG("[LIS3DSH] OK addr=0x%02X ch=%d CTRL4=0x%02X", s_addr, LIS3DSH_I2C_CHANNEL, ctrl4);
    return 0;
}

int LIS3DSH_PowerDown(void)
{
    s_isInitialized = 0;
    s_fifoOn = 0;
    if (s_addr == 0) { /* chua khoi tao lan nao (tat tu luc khoi dong) */
        if (HAL_I2C_Init(LIS3DSH_I2C_CHANNEL, 1) != 0) return -1;
        if (Probe(LIS3DSH_ADDR_SEL_LOW) == 0) s_addr = LIS3DSH_ADDR_SEL_LOW;
        else if (Probe(LIS3DSH_ADDR_SEL_HIGH) == 0) s_addr = LIS3DSH_ADDR_SEL_HIGH;
        else return -1;
    }
    (void)WriteReg(LIS3DSH_REG_CTRL_REG1, 0x00); /* tat SM1 */
    if (WriteReg(LIS3DSH_REG_CTRL_REG3, LIS3DSH_CTRL3_STRT) != 0) return -1; /* soft reset */
    HAL_OS_TaskSleep(10);
    /* Sau reset CTRL_REG4 = 0x07 (ODR power-down); ghi lai cho chac */
    if (WriteReg(LIS3DSH_REG_CTRL_REG4, LIS3DSH_CTRL4_XYZ_EN) != 0) return -1;
    return 0;
}

int LIS3DSH_IsReady(void)
{
    return s_isInitialized;
}

int LIS3DSH_ReadXYZ(int16_t mg[3])
{
    uint8_t raw[6];
    int k;

    if (!s_isInitialized || !mg) return -1;
    /* ADD_INC = 1: doc 6 byte lien tiep OUT_X_L..OUT_Z_H */
    if (ReadReg(LIS3DSH_REG_OUT_X_L, raw, sizeof(raw)) != 0) return -1;
    for (k = 0; k < 3; k++) {
        int16_t v = (int16_t)((uint16_t)raw[2 * k] | ((uint16_t)raw[2 * k + 1] << 8));
        mg[k] = (int16_t)((int32_t)v * LIS3DSH_MG_NUM / LIS3DSH_MG_DEN);
    }
    return 0;
}

uint8_t LIS3DSH_GetAddr(void)
{
    return s_addr;
}

/**
 * Kiem tra nhanh luc chay: chip con tra loi, dung WHO_AM_I, CTRL_REG4 con
 * cau hinh cua LIS3DSH_Init (sut ap lam chip tu reset ve power-down thi
 * CTRL_REG4 = 0x07), va doc 1 mau XYZ. Khi FIFO dang bat, mau doc ra la mau
 * cu nhat trong FIFO (TurnPoll mat 1 mau, khong anh huong).
 */
int LIS3DSH_Check(Lis3dshCheck_t *out)
{
    uint8_t ctrl4 = (uint8_t)((LIS3DSH_ODR << 4) | LIS3DSH_CTRL4_BDU | LIS3DSH_CTRL4_XYZ_EN);
    uint8_t id = 0, rb = 0, raw[6];
    Lis3dshCheck_t c;
    int k;

    memset(&c, 0, sizeof(c));
    c.whoAmI = -1;
    c.ctrl4 = -1;
    if (out) *out = c;
    if (!s_isInitialized || s_addr == 0) return LIS3DSH_CHECK_I2C;
    if (ReadReg(LIS3DSH_REG_WHO_AM_I, &id, 1) != 0) return LIS3DSH_CHECK_I2C;
    c.whoAmI = id;
    if (out) *out = c;
    if (id != LIS3DSH_WHO_AM_I_VAL) return LIS3DSH_CHECK_ID;
    if (ReadReg(LIS3DSH_REG_CTRL_REG4, &rb, 1) != 0) return LIS3DSH_CHECK_I2C;
    c.ctrl4 = rb;
    if (out) *out = c;
    if (rb != ctrl4) return LIS3DSH_CHECK_CONFIG;
    if (ReadReg(LIS3DSH_REG_OUT_X_L, raw, sizeof(raw)) != 0) return LIS3DSH_CHECK_I2C;
    for (k = 0; k < 3; k++) {
        c.raw[k] = (int16_t)((uint16_t)raw[2 * k] | ((uint16_t)raw[2 * k + 1] << 8));
        c.mg[k] = (int16_t)((int32_t)c.raw[k] * LIS3DSH_MG_NUM / LIS3DSH_MG_DEN);
    }
    if (out) *out = c;
    return LIS3DSH_CHECK_OK;
}

int LIS3DSH_GetLastSelfTest(int16_t deltaMg[3])
{
    if (deltaMg) memcpy(deltaMg, s_stDelta, sizeof(s_stDelta));
    return s_stRc;
}

/**
 * Trung binh cac mau dang co trong FIFO (toi da 32 mau = 1.28 s o 25 Hz), roi
 * xa het. Goi deu moi ~1 s cho mot gia tri da loc rung xoc mat duong.
 * Lan goi dau (hoac sau self-test / nap SM) chi bat FIFO, tra count = 0.
 * @return 0 neu thanh cong (count = so mau, mg chi hop le khi count > 0)
 */
int LIS3DSH_ReadAvgXYZ(int32_t mg[3], int *count)
{
    uint8_t src = 0, raw[6];
    int32_t sum[3] = {0, 0, 0};
    int n, i, k;

    if (count) *count = 0;
    if (!s_isInitialized || !mg) return -1;
    if (!s_fifoOn) return FifoSet(1);
    if (ReadReg(LIS3DSH_REG_FIFO_SRC, &src, 1) != 0) return -1;
    n = src & LIS3DSH_FIFO_SRC_FSS;
    if (src & LIS3DSH_FIFO_SRC_OVRN) n = LIS3DSH_FIFO_DEPTH;
    for (i = 0; i < n; i++) {
        /* Moi lan doc 6 byte lay ra mau cu nhat trong FIFO */
        if (ReadReg(LIS3DSH_REG_OUT_X_L, raw, sizeof(raw)) != 0) return -1;
        for (k = 0; k < 3; k++) {
            sum[k] += (int16_t)((uint16_t)raw[2 * k] | ((uint16_t)raw[2 * k + 1] << 8));
        }
    }
    if (n == 0) return 0;
    for (k = 0; k < 3; k++) mg[k] = sum[k] / n * LIS3DSH_MG_NUM / LIS3DSH_MG_DEN;
    if (count) *count = n;
    return 0;
}

/**
 * Self-test duong: do chenh lech trung binh (ST bat) - (ST tat).
 * Datasheet chi cho gia tri dien hinh (+-2 g: X/Y ~140 mg, Z ~590 mg), khong
 * co min/max, nen o day chi coi la dat khi nam trong [1/2, 2] lan gia tri do.
 * Chay o ODR 100 Hz va cho >= 20 mau sau moi lan doi che do: o 25 Hz, cho
 * 100 ms (~2 mau) thi dX/dY con dao dong -200..-320 mg giua cac lan khoi dong.
 * @return 0 neu dat, 1 neu lech, -1 neu loi doc
 */
static int SelfTestRun(int16_t deltaMg[3])
{
    static const int32_t typ[3] = {140, 140, 590};
    uint8_t ctrl4 = (uint8_t)((LIS3DSH_ODR << 4) | LIS3DSH_CTRL4_BDU | LIS3DSH_CTRL4_XYZ_EN);
    uint8_t ctrl4St = (uint8_t)((LIS3DSH_ODR_100HZ << 4) | LIS3DSH_CTRL4_BDU | LIS3DSH_CTRL4_XYZ_EN);
    int32_t base[3], st[3];
    int k, rc = 0;

    if (!s_isInitialized) return -1;
    if (FifoSet(0) != 0) return -1;
    if (WriteReg(LIS3DSH_REG_CTRL_REG4, ctrl4St) != 0) return -1;
    HAL_OS_TaskSleep(40); /* 200 ms: bo cac mau dang chuyen tiep */
    if (AverageXYZ(10, base) != 0) rc = -1;
    if (rc == 0 && WriteReg(LIS3DSH_REG_CTRL_REG5, LIS3DSH_CTRL5_BW_50HZ | LIS3DSH_CTRL5_ST_POS) != 0) rc = -1;
    if (rc == 0) {
        HAL_OS_TaskSleep(40);
        if (AverageXYZ(10, st) != 0) rc = -1;
    }
    (void)WriteReg(LIS3DSH_REG_CTRL_REG5, LIS3DSH_CTRL5_BW_50HZ);
    HAL_OS_TaskSleep(40);
    (void)WriteReg(LIS3DSH_REG_CTRL_REG4, ctrl4);
    if (rc != 0) return -1;

    for (k = 0; k < 3; k++) {
        int32_t d = st[k] - base[k];
        if (deltaMg) deltaMg[k] = (int16_t)d;
        if (d < 0) d = -d;
        if (d < typ[k] / 2 || d > typ[k] * 2) rc = 1;
    }
    return rc;
}

/** Self-test, luu ket qua cho LIS3DSH_GetLastSelfTest; @return 0 dat, 1 lech, -1 loi */
int LIS3DSH_SelfTest(int16_t deltaMg[3])
{
    int16_t d[3] = {0, 0, 0};
    int rc = SelfTestRun(d);

    s_stRc = rc;
    memcpy(s_stDelta, d, sizeof(d));
    if (deltaMg) memcpy(deltaMg, d, sizeof(d));
    return rc;
}

/*
 * Phat hien chuyen dong bang state machine SM1 -> INT1 (chot, tich cuc thap).
 *
 * Datasheet khong mo ta opcode; chuong trinh lay theo vi du wake-up cua ST
 * (AN3393): ST1_1 = 0x05 (reset NOP, next GNTH1: mot truc |a| > THRS1_1),
 * ST1_2 = 0x11 (CONT: bao ngat roi chay lai), MASK = +-X +-Y +-Z, SETT1.SITR.
 * Nguong 8 bit so voi FS/128 (+-2 g: 15.6 mg/LSB). CHUA doi chieu tai lieu
 * goc: phai kiem chung tren board (go nhe vao board phai thay ngat).
 *
 * GNTH1 so tri tuyet doi gia toc ca trong luc, nen nguong dat = truc lon nhat
 * luc dung yen + deltaMg: ngat khi mot truc vuot muc trong luc hien tai.
 */
#define LIS3DSH_THRS_UMG 15625 /* micro-g / LSB nguong o +-2 g */

int LIS3DSH_MotionArm(int deltaMg, int *thrMg)
{
    static const uint8_t prog[2] = {0x05, 0x11};
    int32_t g[3];
    int32_t maxAbs = 0, thr;
    uint8_t dummy;
    int k;

    if (!s_isInitialized || deltaMg <= 0) return -1;
    if (FifoSet(0) != 0) return -1;
    if (AverageXYZ(4, g) != 0) return -1;
    for (k = 0; k < 3; k++) {
        int32_t a = g[k] < 0 ? -g[k] : g[k];
        if (a > maxAbs) maxAbs = a;
    }
    thr = ((maxAbs + deltaMg) * 1000 + LIS3DSH_THRS_UMG / 2) / LIS3DSH_THRS_UMG;
    if (thr > 127) thr = 127;
    if (thr < 1) thr = 1;

    /* Tat SM truoc khi nap chuong trinh */
    if (WriteReg(LIS3DSH_REG_CTRL_REG1, 0x00) != 0) return -1;
    if (WriteReg(LIS3DSH_REG_ST1_1, prog[0]) != 0 ||
        WriteReg(LIS3DSH_REG_ST1_1 + 1, prog[1]) != 0 ||
        WriteReg(LIS3DSH_REG_THRS1_1, (uint8_t)thr) != 0 ||
        WriteReg(LIS3DSH_REG_MASK1_B, LIS3DSH_MASK_XYZ) != 0 ||
        WriteReg(LIS3DSH_REG_MASK1_A, LIS3DSH_MASK_XYZ) != 0 ||
        WriteReg(LIS3DSH_REG_SETT1, LIS3DSH_SETT1_SITR) != 0 ||
        WriteReg(LIS3DSH_REG_CTRL_REG3, LIS3DSH_CTRL3_INT1_EN) != 0) {
        HAL_LOG("[LIS3DSH] SM1 program write failed");
        return -1;
    }
    (void)ReadReg(LIS3DSH_REG_OUTS1, &dummy, 1); /* xoa ngat cu */
    if (WriteReg(LIS3DSH_REG_CTRL_REG1, LIS3DSH_CTRL1_SM1_EN) != 0) return -1;

    if (thrMg) *thrMg = (int)(thr * LIS3DSH_THRS_UMG / 1000);
    HAL_LOG("[LIS3DSH] motion armed: g=(%ld,%ld,%ld) thr=%ld (%ld mg)",
            (long)g[0], (long)g[1], (long)g[2], (long)thr, (long)(thr * LIS3DSH_THRS_UMG / 1000));
    return 0;
}

int LIS3DSH_MotionDisarm(void)
{
    uint8_t dummy;

    if (!s_isInitialized) return -1;
    if (WriteReg(LIS3DSH_REG_CTRL_REG1, 0x00) != 0) return -1;
    (void)WriteReg(LIS3DSH_REG_CTRL_REG3, 0x00);
    (void)ReadReg(LIS3DSH_REG_OUTS1, &dummy, 1); /* nha chan INT1 ve muc nghi */
    return 0;
}

/** Doc OUTS1 de xoa ngat chot; @return gia tri OUTS1 (co truc nao vuot), -1 neu loi */
int LIS3DSH_MotionClear(void)
{
    uint8_t outs = 0;

    if (!s_isInitialized) return -1;
    if (ReadReg(LIS3DSH_REG_OUTS1, &outs, 1) != 0) return -1;
    return outs;
}
