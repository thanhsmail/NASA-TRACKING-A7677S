#ifndef APP_CFG_H
#define APP_CFG_H

#include <stdint.h>

typedef struct {
    char     serverHost[64];
    int      serverPort;
    char     plate[24];
    int      periodMovingSec;
    int      periodStoppedSec;
    int      accMode;           /* 0=không đấu, 1=có đấu, 2=cả hai */
    int      speedThreshKph;    /* cmd 10 */
    int      silentMode;        /* cmd 11: 0=tắt im lặng, 1=bật */
    int      doutOn;            /* cmd 16 */
    char     phone1[20];
    char     phone2[20];
    char     phone3[20];
    int      parkConfirmSec;    /* cmd 26 */
    int      resetEveryDays;    /* cmd 32 A */
    int      resetAtHour;       /* cmd 32 B */
    int      configLocked;      /* cmd 40 */
    int      deviceEnabled;     /* cmd 38: 1=mở, 0=sleep */
    int      operateDays;       /* số ngày đã hoạt động */
    char     driverName[64];
    char     driverLicense[32];
    int      driverLoggedIn;
    int      lastMday;          /* Ngày reset odometer gần nhất */
} AppConfig_t;

typedef struct {
    char     imei[32];
    char     serialNum[32];
    char     hwVer[16];
    char     fwVer[16];
    char     activationCode[32];
    uint32_t calibVoltageMv;
} FactoryConfig_t;

void CFG_Init(void);
void CFG_Load(void);
int  CFG_Save(void);
void CFG_FactoryReset(void);

FactoryConfig_t *CFG_GetFactory(void);
int  CFG_SaveFactory(const FactoryConfig_t *fcfg);

AppConfig_t *CFG_Get(void);

const char *CFG_GetServerHost(void);// 
int         CFG_GetServerPort(void);
const char *CFG_GetPlate(void);
int         CFG_GetPeriodMoving(void);
int         CFG_GetPeriodStopped(void);
int         CFG_GetAccMode(void);
int         CFG_GetSpeedThresh(void);
int         CFG_GetSilentMode(void);
int         CFG_GetDout(void);
int         CFG_GetParkConfirmSec(void);
int         CFG_IsLocked(void);
int         CFG_IsDeviceEnabled(void);
int         CFG_IsDriverLoggedIn(void);
const char *CFG_GetDriverName(void);
const char *CFG_GetDriverLicense(void);
const char *CFG_GetPhone(int index); /* 1..3 */

int  CFG_IsAuthPhone(const char *phone);
int  CFG_CanChangeProtected(const char *src, const char *fromPhone);

void CFG_SetServer(const char *host, int port);
void CFG_SetPlate(const char *plate);
void CFG_SetPeriod(int moving, int stopped);
void CFG_SetAccMode(int mode);
void CFG_SetSpeedThresh(int kph);
void CFG_SetSilentMode(int on);
void CFG_SetDout(int on);
void CFG_SetPhone(int index, const char *phone);
void CFG_SetParkConfirmSec(int sec);
void CFG_SetResetSchedule(int days, int hour);
void CFG_SetLocked(int locked);
void CFG_SetDeviceEnabled(int enabled);
void CFG_SetDriver(const char *name, const char *license, int loggedIn);
void CFG_FlushDirty(void); /* flush deferred driver/config dirty flag */

#endif /* APP_CFG_H */
