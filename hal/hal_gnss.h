/**
 * @file    hal_gnss.h
 * @brief   HAL - GNSS / GPS Abstraction
 */
#ifndef HAL_GNSS_H
#define HAL_GNSS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Struct thoi gian chuan hoa */
typedef struct {
    uint16_t tm_year;
    uint8_t  tm_mon;
    uint8_t  tm_mday;
    uint8_t  tm_hour;
    uint8_t  tm_min;
    uint8_t  tm_sec;
} HalDateTime_t;

/** Du lieu GNSS chuan hoa -- khong phu thuoc SDK */
typedef struct {
    double   lat;           /**< Vi do (do thap phan, + = Bac) */
    double   lon;           /**< Kinh do (do thap phan, + = Dong) */
    double   speedKnots;    /**< Van toc (knots) */
    double   headingDeg;    /**< Huong dich chuyen (do, 0-360) */
    int      satellites;    /**< So ve tinh lock duoc */
    int      valid;         /**< 1 = du lieu hop le, 0 = chua co fix */
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
} HalGnssInfo_t;

typedef void (*HalGnssUrcCb_t)(const HalGnssInfo_t *info, void *ctx);

int HAL_GNSS_GetInfo(HalGnssInfo_t *out);
void HAL_GNSS_RegisterUrc(HalGnssUrcCb_t cb, void *ctx);
void HAL_GNSS_UrcListenerStart(void);

/**
 * @brief  Chon he ve tinh: 1 = GPS+BeiDou+QZSS, 2 = BeiDou, 3 = GPS+QZSS.
 *         Goi truoc HAL_GNSS_UrcListenerStart thi ap dung luc bat GNSS;
 *         goi khi GNSS dang chay thi ap dung ngay.
 * @return 0 = OK (hoac da ghi nho de ap dung luc bat), -1 = sai gia tri / chip tu choi.
 */
int HAL_GNSS_SetMode(int mode);

/** @return che do chip dang dung (1..3), -1 neu GNSS chua bat */
int HAL_GNSS_GetMode(void);

/**
 * @brief  Che do giu tinh cua chip ($CFGDYN,h02,1,<nguong>): toc do duoi nguong
 *         thi chip giu nguyen vi tri, chong troi khi dung yen. 0 = tat.
 *         Chip UC6228CI khong luu lenh nay nen phai gui lai moi lan bat GNSS.
 * @param  cmPerSec nguong toc do (cm/s), 0..1000
 * @return 0 = OK / da ghi nho, -1 = sai gia tri hoac chip tu choi
 */
int HAL_GNSS_SetStaticHold(int cmPerSec);

/**
 * @brief  Yeu cau tai du lieu AGPS (chay nen, khong chan task goi).
 *         Can co mang va gio he thong hop le. Nha san xuat khuyen khong qua
 *         12 lan / ngay: goi day hon co the lam treo he thong.
 * @return 0 = da dua vao hang doi, -1 = GNSS chua bat / dang tai
 */
int HAL_GNSS_AgpsRequest(void);

/**
 * @brief  Giam sat GNSS: chip gui URC moi giay ke ca khi khong co fix; khong co
 *         URC nao qua `sec` giay thi gnss_task tat roi bat lai GNSS. Mac dinh 60.
 * @return 0 = OK, -1 = sec < 10
 */
int HAL_GNSS_SetNoUrcRestartSec(int sec);

/** @return so giay tu lan cuoi vong lap gnss_task chay (0 = chua chay); de canh task treo */
uint32_t HAL_GNSS_GetTaskAliveAgeSec(void);

/**
 * @brief  Yeu cau tat (0) / bat (1) GNSS de tiet kiem dien khi ngu. gnss_task
 *         thuc hien trong vong 10 giay; trong luc tat khong tu bat lai vi im
 *         lang. Bat lai thi cau hinh lai tu dau (he ve tinh, giu tinh, URC).
 */
void HAL_GNSS_SetPower(int on);

/** @return 1 = chip GNSS dang bat */
int  HAL_GNSS_IsPowered(void);

int HAL_GNSS_GetRtc(HalDateTime_t *rtc);
int HAL_GNSS_SetRtc(const HalDateTime_t *rtc);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GNSS_H */
