#ifndef APP_GPS_H
#define APP_GPS_H

#include "simcom_api.h"

typedef struct {
    double   lat;          /* Vi do (do thap phan, 0.0 = khong hop le) */
    double   lon;          /* Kinh do (do thap phan) */
    double   speedKph;     /* Van toc km/h */
    double   headingDeg;   /* Huong (do) */
    int      satellites;   /* So ve tinh */
    double   totalKm;      /* Quang duong trong ngay (km) */
    uint32_t fixTick;      /* Tick cua fix GPS cuoi */
    int      valid;        /* 1 = co du lieu GPS hop le */
} GpsSnapshot_t;

void GPS_Init(void);
void GPS_Snapshot(GpsSnapshot_t *out);
/* Trạng thái dây ACC (đã debounce) từ GPIO task — dùng khoá trạng thái đỗ */
void GPS_SetAccOn(int on);
void GPS_ResetOdometer(void);
int GPS_GetSatellitesCount(void);
double GPS_GetLastSpeedKph(void);
double GPS_GetTotalKm(void);
void GnssUrcListenerEnsureStarted(void);

#endif /* APP_GPS_H */
