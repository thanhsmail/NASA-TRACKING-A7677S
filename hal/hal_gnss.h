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

int HAL_GNSS_GetRtc(HalDateTime_t *rtc);
int HAL_GNSS_SetRtc(const HalDateTime_t *rtc);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GNSS_H */
