#include "simcom_api.h"
#include "app_config.h"
#include "app_cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AppConfig_t s_cfg;

/* Bảo vệ ghi/đọc file config: CFG_Save gọi từ cả task SMS lẫn task NASA (lệnh server) */
static sMutexRef s_cfgMutex = NULL;

static void CfgLock(void)
{
    if (s_cfgMutex) sAPI_MutexLock(s_cfgMutex, SC_SUSPEND);
}

static void CfgUnlock(void)
{
    if (s_cfgMutex) sAPI_MutexUnLock(s_cfgMutex);
}

static void CFG_SetDefaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    strncpy(s_cfg.serverHost, NASA_SERVER_HOST_DEFAULT, sizeof(s_cfg.serverHost) - 1);
    s_cfg.serverPort       = NASA_SERVER_PORT_DEFAULT;
    strncpy(s_cfg.plate, "51A-12345", sizeof(s_cfg.plate) - 1);
    s_cfg.periodMovingSec  = TRACKING_PERIOD_MOVING_DEFAULT;
    s_cfg.periodStoppedSec = TRACKING_PERIOD_STOPPED_DEFAULT;
    s_cfg.accMode          = 1; /* mặc định có đấu dây ACC */
    s_cfg.speedThreshKph   = TRACKING_SPEED_THRESHOLD_DEFAULT;
    s_cfg.silentMode       = 0;
    s_cfg.doutOn           = 0;
    s_cfg.parkConfirmSec   = PARK_REPORT_PERIOD_SEC_DEFAULT;
    s_cfg.resetEveryDays   = 0;
    s_cfg.resetAtHour      = 0;
    s_cfg.configLocked     = 0;
    s_cfg.deviceEnabled    = 1;
    s_cfg.operateDays      = 0;
    strncpy(s_cfg.driverName, "", sizeof(s_cfg.driverName) - 1);
    strncpy(s_cfg.driverLicense, "", sizeof(s_cfg.driverLicense) - 1);
    s_cfg.driverLoggedIn   = 0;
}

static void TrimInPlace(char *s)
{
    char *e;
    if (!s) return;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        memmove(s, s + 1, strlen(s));
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
}

static void ApplyKeyValue(const char *key, const char *val)
{
    if (!key || !val) return;
    if (strcmp(key, "host") == 0) {
        strncpy(s_cfg.serverHost, val, sizeof(s_cfg.serverHost) - 1);
    } else if (strcmp(key, "port") == 0) {
        s_cfg.serverPort = atoi(val);
    } else if (strcmp(key, "plate") == 0) {
        strncpy(s_cfg.plate, val, sizeof(s_cfg.plate) - 1);
    } else if (strcmp(key, "pmove") == 0) {
        s_cfg.periodMovingSec = atoi(val);
    } else if (strcmp(key, "pstop") == 0) {
        s_cfg.periodStoppedSec = atoi(val);
    } else if (strcmp(key, "acc") == 0) {
        s_cfg.accMode = atoi(val);
    } else if (strcmp(key, "spdth") == 0) {
        s_cfg.speedThreshKph = atoi(val);
    } else if (strcmp(key, "silent") == 0) {
        s_cfg.silentMode = atoi(val);
    } else if (strcmp(key, "dout") == 0) {
        s_cfg.doutOn = atoi(val);
    } else if (strcmp(key, "ph1") == 0) {
        strncpy(s_cfg.phone1, val, sizeof(s_cfg.phone1) - 1);
    } else if (strcmp(key, "ph2") == 0) {
        strncpy(s_cfg.phone2, val, sizeof(s_cfg.phone2) - 1);
    } else if (strcmp(key, "ph3") == 0) {
        strncpy(s_cfg.phone3, val, sizeof(s_cfg.phone3) - 1);
    } else if (strcmp(key, "park") == 0) {
        s_cfg.parkConfirmSec = atoi(val);
    } else if (strcmp(key, "rsd") == 0) {
        s_cfg.resetEveryDays = atoi(val);
    } else if (strcmp(key, "rsh") == 0) {
        s_cfg.resetAtHour = atoi(val);
    } else if (strcmp(key, "lock") == 0) {
        s_cfg.configLocked = atoi(val);
    } else if (strcmp(key, "en") == 0) {
        s_cfg.deviceEnabled = atoi(val);
    } else if (strcmp(key, "days") == 0) {
        s_cfg.operateDays = atoi(val);
    } else if (strcmp(key, "dname") == 0) {
        strncpy(s_cfg.driverName, val, sizeof(s_cfg.driverName) - 1);
    } else if (strcmp(key, "dlic") == 0) {
        strncpy(s_cfg.driverLicense, val, sizeof(s_cfg.driverLicense) - 1);
    } else if (strcmp(key, "dlog") == 0) {
        s_cfg.driverLoggedIn = atoi(val);
    }
}

void CFG_Init(void)
{
    if (s_cfgMutex == NULL) {
        sAPI_MutexCreate(&s_cfgMutex, SC_FIFO);
    }
    CFG_SetDefaults();
    CFG_Load();
}

void CFG_Load(void)
{
    CfgLock();
    SCFILE *fp = sAPI_fopen(NASA_CFG_FILE, "rb");
    if (!fp) {
        sAPI_Debug("[CFG] No config file, using defaults");
        CfgUnlock();
        return;
    }

    static char fileBuf[1024];
    int bytesRead = sAPI_fread(fileBuf, 1, sizeof(fileBuf) - 1, fp);
    sAPI_fclose(fp);

    if (bytesRead <= 0) {
        sAPI_Debug("[CFG] Empty config file or read error");
        CfgUnlock();
        return;
    }
    fileBuf[bytesRead] = '\0';

    char *lineStart = fileBuf;
    char *next = NULL;
    char line[160];

    while (lineStart && *lineStart) {
        char *lineEnd = strchr(lineStart, '\n');
        if (lineEnd) {
            next = lineEnd + 1;
            *lineEnd = '\0';
        } else {
            next = NULL;
        }

        size_t len = strlen(lineStart);
        if (len > 0 && lineStart[len - 1] == '\r') {
            lineStart[len - 1] = '\0';
        }

        if (strlen(lineStart) < sizeof(line)) {
            strcpy(line, lineStart);
        } else {
            strncpy(line, lineStart, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
        }

        TrimInPlace(line);
        if (line[0] && line[0] != '#') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                TrimInPlace(line);
                TrimInPlace(eq + 1);
                ApplyKeyValue(line, eq + 1);
            }
        }

        lineStart = next;
    }
    sAPI_Debug("[CFG] Loaded host=%s port=%d plate=%s", s_cfg.serverHost, s_cfg.serverPort, s_cfg.plate);
    CfgUnlock();
}

int CFG_Save(void)
{
    char buf[768];
    int len;
    SCFILE *fp;

    CfgLock();
    len = snprintf(buf, sizeof(buf),
                   "host=%s\nport=%d\nplate=%s\npmove=%d\npstop=%d\nacc=%d\nspdth=%d\n"
                   "silent=%d\ndout=%d\nph1=%s\nph2=%s\nph3=%s\npark=%d\nrsd=%d\nrsh=%d\n"
                   "lock=%d\nen=%d\ndays=%d\ndname=%s\ndlic=%s\ndlog=%d\n",
                   s_cfg.serverHost, s_cfg.serverPort, s_cfg.plate,
                   s_cfg.periodMovingSec, s_cfg.periodStoppedSec, s_cfg.accMode, s_cfg.speedThreshKph,
                   s_cfg.silentMode, s_cfg.doutOn, s_cfg.phone1, s_cfg.phone2, s_cfg.phone3,
                   s_cfg.parkConfirmSec, s_cfg.resetEveryDays, s_cfg.resetAtHour,
                   s_cfg.configLocked, s_cfg.deviceEnabled, s_cfg.operateDays,
                   s_cfg.driverName, s_cfg.driverLicense, s_cfg.driverLoggedIn);
    if (len <= 0) {
        CfgUnlock();
        return -1;
    }

    sAPI_remove(NASA_CFG_FILE);
    fp = sAPI_fopen(NASA_CFG_FILE, "wb");
    if (!fp) {
        sAPI_Debug("[CFG] Save open failed");
        CfgUnlock();
        return -1;
    }
    sAPI_fwrite(buf, 1, (size_t)len, fp);
    sAPI_fclose(fp);
    sAPI_Debug("[CFG] Saved OK");
    CfgUnlock();
    return 0;
}

void CFG_FactoryReset(void)
{
    CfgLock();
    CFG_SetDefaults();
    sAPI_remove(NASA_CFG_FILE);
    CfgUnlock();
    CFG_Save();
}

AppConfig_t *CFG_Get(void) { return &s_cfg; }

const char *CFG_GetServerHost(void) { return s_cfg.serverHost; }
int CFG_GetServerPort(void) { return s_cfg.serverPort; }
const char *CFG_GetPlate(void) { return s_cfg.plate; }
int CFG_GetPeriodMoving(void) { return s_cfg.periodMovingSec; }
int CFG_GetPeriodStopped(void) { return s_cfg.periodStoppedSec; }
int CFG_GetAccMode(void) { return s_cfg.accMode; }
int CFG_GetSpeedThresh(void) { return s_cfg.speedThreshKph; }
int CFG_GetSilentMode(void) { return s_cfg.silentMode; }
int CFG_GetDout(void) { return s_cfg.doutOn; }
int CFG_GetParkConfirmSec(void) { return s_cfg.parkConfirmSec; }
int CFG_IsLocked(void)
{
    /* Tính năng khóa cấu hình / SĐT trung tâm chưa bật — luôn mở */
#if NASA_LOCK_FEATURE_ENABLE
    if (s_cfg.configLocked) return 1;
    if (s_cfg.operateDays >= NASA_LOCK_AFTER_DAYS) return 1;
    return 0;
#else
    (void)s_cfg.configLocked;
    return 0;
#endif
}
int CFG_IsDeviceEnabled(void) { return s_cfg.deviceEnabled; }
int CFG_IsDriverLoggedIn(void) { return s_cfg.driverLoggedIn; }
const char *CFG_GetDriverName(void) { return s_cfg.driverName; }
const char *CFG_GetDriverLicense(void) { return s_cfg.driverLicense; }

const char *CFG_GetPhone(int index)
{
    if (index == 1) return s_cfg.phone1;
    if (index == 2) return s_cfg.phone2;
    if (index == 3) return s_cfg.phone3;
    return "";
}

static int PhoneMatch(const char *a, const char *b)
{
    size_t la, lb;
    if (!a || !b || a[0] == '\0' || b[0] == '\0') return 0;
    la = strlen(a);
    lb = strlen(b);
    /* so khớp đuôi (bỏ mã quốc gia) */
    if (la >= 9 && lb >= 9) {
        return strcmp(a + la - 9, b + lb - 9) == 0;
    }
    return strcmp(a, b) == 0;
}

int CFG_IsAuthPhone(const char *phone)
{
    if (!phone || phone[0] == '\0') return 0;
    return PhoneMatch(phone, s_cfg.phone1) ||
           PhoneMatch(phone, s_cfg.phone2) ||
           PhoneMatch(phone, s_cfg.phone3);
}

int CFG_CanChangeProtected(const char *src, const char *fromPhone)
{
#if NASA_LOCK_FEATURE_ENABLE
    if (src && strcmp(src, "SERVER") == 0) return 1;
    if (src && strcmp(src, "COM") == 0) return 1;
    if (!CFG_IsLocked()) return 1;
    return CFG_IsAuthPhone(fromPhone);
#else
    (void)src;
    (void)fromPhone;
    return 1;
#endif
}

void CFG_SetServer(const char *host, int port)
{
    if (host && host[0]) {
        strncpy(s_cfg.serverHost, host, sizeof(s_cfg.serverHost) - 1);
        s_cfg.serverHost[sizeof(s_cfg.serverHost) - 1] = '\0';
    }
    if (port > 0 && port <= 65535) s_cfg.serverPort = port;
}

void CFG_SetPlate(const char *plate)
{
    if (!plate) return;
    strncpy(s_cfg.plate, plate, sizeof(s_cfg.plate) - 1);
    s_cfg.plate[sizeof(s_cfg.plate) - 1] = '\0';
}

void CFG_SetPeriod(int moving, int stopped)
{
    if (moving >= TRACKING_PERIOD_MIN_SEC && moving <= TRACKING_PERIOD_MAX_SEC)
        s_cfg.periodMovingSec = moving;
    if (stopped >= TRACKING_PERIOD_MIN_SEC && stopped <= TRACKING_PERIOD_MAX_SEC)
        s_cfg.periodStoppedSec = stopped;
}

void CFG_SetAccMode(int mode)
{
    if (mode >= 0 && mode <= 2) s_cfg.accMode = mode;
}

void CFG_SetSpeedThresh(int kph)
{
    if (kph >= 0 && kph <= 200) s_cfg.speedThreshKph = kph;
}

void CFG_SetSilentMode(int on) { s_cfg.silentMode = on ? 1 : 0; }
void CFG_SetDout(int on) { s_cfg.doutOn = on ? 1 : 0; }

void CFG_SetPhone(int index, const char *phone)
{
    char *dst = NULL;
    if (!phone) return;
    if (index == 1) dst = s_cfg.phone1;
    else if (index == 2) dst = s_cfg.phone2;
    else if (index == 3) dst = s_cfg.phone3;
    if (!dst) return;
    strncpy(dst, phone, 19);
    dst[19] = '\0';
}

void CFG_SetParkConfirmSec(int sec)
{
    if (sec < 0) sec = 0;
    if (sec > PARK_REPORT_PERIOD_SEC_MAX) sec = PARK_REPORT_PERIOD_SEC_MAX;
    s_cfg.parkConfirmSec = sec;
}

void CFG_SetResetSchedule(int days, int hour)
{
    s_cfg.resetEveryDays = (days < 0) ? 0 : days;
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    s_cfg.resetAtHour = hour;
}

void CFG_SetLocked(int locked) { s_cfg.configLocked = locked ? 1 : 0; }
void CFG_SetDeviceEnabled(int enabled) { s_cfg.deviceEnabled = enabled ? 1 : 0; }

void CFG_SetDriver(const char *name, const char *license, int loggedIn)
{
    if (name) {
        strncpy(s_cfg.driverName, name, sizeof(s_cfg.driverName) - 1);
        s_cfg.driverName[sizeof(s_cfg.driverName) - 1] = '\0';
    }
    if (license) {
        strncpy(s_cfg.driverLicense, license, sizeof(s_cfg.driverLicense) - 1);
        s_cfg.driverLicense[sizeof(s_cfg.driverLicense) - 1] = '\0';
    }
    s_cfg.driverLoggedIn = loggedIn ? 1 : 0;
}

void CFG_BumpOperateDayIfNeeded(int today_mday)
{
    static int last_mday = -1;
    if (today_mday < 1 || today_mday > 31) return;
    if (last_mday == -1) {
        last_mday = today_mday;
        return;
    }
    if (today_mday != last_mday) {
        s_cfg.operateDays++;
        last_mday = today_mday;
        CFG_Save();
    }
}
