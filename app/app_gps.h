#ifndef APP_GPS_H
#define APP_GPS_H

#include <stdint.h>

/* Nguon cua lat / lon trong GpsSnapshot_t */
#define GPS_SRC_NONE  0  /* chua tung co vi tri: lat / lon = 0,0 */
#define GPS_SRC_GNSS  1  /* fix GNSS hien tai (valid = 1) */
#define GPS_SRC_HOLD  2  /* vi tri GNSS hop le gan nhat (co the doc lai tu file sau khoi dong) */
#define GPS_SRC_LBS   3  /* vi tri theo tram phat song, do chinh xac accM met */

typedef struct {
    double   lat;          /* Vi do (do thap phan, 0.0 = khong hop le) */
    double   lon;          /* Kinh do (do thap phan) */
    double   speedKph;     /* Van toc km/h */
    double   headingDeg;   /* Huong (do) */
    int      satellites;   /* So ve tinh */
    double   totalKm;      /* Quang duong trong ngay (km) */
    uint32_t fixTick;      /* Tick cua mau GNSS cuoi co vi tri */
    int      valid;        /* 1 = co fix GNSS hop le va con moi */
    int      source;       /* GPS_SRC_* */
    int      accM;         /* Do chinh xac (m) khi source = GPS_SRC_LBS, 0 neu khong biet */
} GpsSnapshot_t;

void GPS_Init(void);
void GPS_Snapshot(GpsSnapshot_t *out);
void GPS_SetAccOn(int on);
void GPS_ResetOdometer(void);
int GPS_GetSatellitesCount(void);
double GPS_GetTotalKm(void);
int GPS_IsMoving(void);
int GPS_IsParked(void);
int GPS_FormatLocalDateTime(char *dateTime, uint32_t dateTimeSize);
void GPS_OnUrcString(const char *gpsUrc);

/** Gọi mỗi vòng khi đang có phiên với Server: tải AGPS nếu lâu chưa có fix */
void GPS_AgpsPoll(void);

/** Gọi mỗi vòng khi đang có phiên với Server (GNSS đang bật): hỏi vị trí LBS
 *  khi mất GPS theo các điều kiện LBS_* trong app_config.h */
void GPS_LbsPoll(void);

#endif /* APP_GPS_H */
