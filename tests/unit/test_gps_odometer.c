/**
 * @file test_gps_odometer.c
 * @brief Unit test odometer (app_gps.c): mất GPS ngắn / dài, có GPS lại khi
 *        đang dừng, lưu và đọc lại km ngày qua khởi động lại.
 *
 * Dữ liệu vào là dòng URC +CGNSSINFO qua GPS_OnUrcString(), 1 mẫu / giây.
 * File hệ thống và RTC là bản giả trong bộ nhớ (cuối file).
 */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "app_gps.h"
#include "app_utils.h"
#include "hal_fs.h"
#include "hal_gnss.h"
#include "hal_os.h"
#include "mock_hal_os.h"

#define LAT0 10.75
#define M_PER_DEG_LON (111194.9 * cos(LAT0 * 3.14159265358979323846 / 180.0))

static double s_lon = 106.70;

static void Tick1s(void) { Mock_HAL_OS_AdvanceTicks(HAL_TICKS_PER_SEC); }

/** Một mẫu có fix tại (LAT0, s_lon), 17 vệ tinh */
static void SendFix(double speedKph)
{
    char line[200];
    double latAbs = LAT0, lonAbs = s_lon;
    double latNmea = floor(latAbs) * 100.0 + (latAbs - floor(latAbs)) * 60.0;
    double lonNmea = floor(lonAbs) * 100.0 + (lonAbs - floor(lonAbs)) * 60.0;
    snprintf(line, sizeof(line),
             "+CGNSSINFO: 2,09,05,00,03,%.6f,N,%.6f,E,161026,070000.0,10.0,%.3f,90.0,1.2,0.9,0.8\r\n",
             latNmea, lonNmea, speedKph / 1.852);
    GPS_OnUrcString(line);
    Tick1s();
}

/** Dòng không fix, đủ trường nhưng rỗng */
static void SendNoFixFull(void)
{
    GPS_OnUrcString("+CGNSSINFO: ,,,,,,,,,,,,,,,,\r\n");
    Tick1s();
}

/** Dòng không fix dạng ngắn (nf < 13) */
static void SendNoFixShort(void)
{
    GPS_OnUrcString("+CGNSSINFO: ,,,,,,,,\r\n");
    Tick1s();
}

static void Move(double meters) { s_lon += meters / M_PER_DEG_LON; }

/** Chạy n giây ở 36 km/h (10 m/s) */
static void Drive(int n)
{
    int i;
    for (i = 0; i < n; i++) { Move(10.0); SendFix(36.0); }
}

/** Đứng yên n giây */
static void Stand(int n)
{
    int i;
    for (i = 0; i < n; i++) SendFix(0.0);
}

static void AssertKm(double expected, double tolKm, const char *what)
{
    double km = GPS_GetTotalKm();
    printf("  %-52s km=%.3f (ky vong %.3f)\n", what, km, expected);
    assert(fabs(km - expected) <= tolKm);
}

/* Bắt đầu mỗi ca: xe đứng yên ở chỗ mới, km = 0 */
static void Begin(const char *name)
{
    printf("[TEST] %s\n", name);
    Move(5000.0);
    Stand(15);
    GPS_ResetOdometer();
    Stand(2);
}

static void test_ShortFullNoFix_IsBridged(void)
{
    int i;
    Begin("Mat fix 10s (dong rong du truong) -> noi lai");
    Drive(20);                     /* 3s xác nhận chạy, sau đó cộng */
    for (i = 0; i < 10; i++) { Move(10.0); SendNoFixFull(); }
    Drive(10);
    /* chạy 40 s x 10 m, trừ 3 mẫu đầu chưa xác nhận chạy */
    AssertKm(0.37, 0.02, "sau 10s mat fix");
}

static void test_LongFullNoFix_DropsReference(void)
{
    int i;
    Begin("Mat fix 60s (dong rong du truong) -> bo diem tham chieu");
    Drive(20);
    for (i = 0; i < 60; i++) { Move(10.0); SendNoFixFull(); }
    Drive(10);
    /* 600 m trong lúc mất GPS không được cộng */
    AssertKm(0.26, 0.02, "sau 60s mat fix");
}

static void test_ShortShortNoFix_IsBridged(void)
{
    int i;
    Begin("Mat fix 10s (dong ngan nf<13) -> noi lai");
    Drive(20);
    for (i = 0; i < 10; i++) { Move(10.0); SendNoFixShort(); }
    Drive(10);
    AssertKm(0.37, 0.02, "sau 10s mat fix");
}

static void test_RegainWhileStopped(void)
{
    int i;
    Begin("Dung khi mat GPS, ra khoi ham 200 m -> van noi");
    for (i = 0; i < 20; i++) { Move(10.0); SendNoFixFull(); }
    SendFix(0.0);                  /* trạng thái vẫn là dừng */
    AssertKm(0.20, 0.01, "co GPS lai, dang dung, doi 200 m");

    Begin("Dung khi mat GPS, troi vi tri 30 m -> khong cong");
    for (i = 0; i < 10; i++) SendNoFixFull();
    Move(30.0);
    SendFix(0.0);
    AssertKm(0.0, 0.0005, "co GPS lai, dang dung, troi 30 m");
}

/* --- File giả trong bộ nhớ --- */
static unsigned char s_file[64];
static int s_fileLen = -1;   /* -1 = chưa có file */
static int s_rtcMday = 16;

static void test_RestartRestoresKm(void)
{
    unsigned char saved[64];
    int savedLen;

    Begin("Khoi dong lai giua ngay -> doc lai km");
    Drive(50);
    Stand(15);                     /* chuyển sang dừng -> lưu */
    savedLen = s_fileLen;
    memcpy(saved, s_file, sizeof(saved));
    assert(savedLen > 0);
    {
        double kmBefore = GPS_GetTotalKm();
        printf("  km truoc khi khoi dong lai: %.3f\n", kmBefore);

        GPS_ResetOdometer();       /* giả lập RAM về 0 */
        memcpy(s_file, saved, sizeof(saved));
        s_fileLen = savedLen;
        GPS_Init();
        AssertKm(kmBefore, 0.0005, "sau GPS_Init cung ngay");

        GPS_ResetOdometer();
        memcpy(s_file, saved, sizeof(saved));
        s_fileLen = savedLen;
        s_rtcMday = 17;
        GPS_Init();
        AssertKm(0.0, 0.0, "sau GPS_Init sang ngay khac");
        s_rtcMday = 16;

        memcpy(s_file, saved, sizeof(saved));
        s_file[10] ^= 0xFF;        /* hỏng dữ liệu */
        s_fileLen = savedLen;
        GPS_Init();
        AssertKm(0.0, 0.0, "file hong");
    }
}

/** Giả lập khởi động lại: RAM km về 0, file giữ bản ghi đã lưu, đọc lại */
static void Reboot(const unsigned char *saved, int savedLen, int offSec)
{
    GPS_ResetOdometer();
    memcpy(s_file, saved, sizeof(s_file));
    s_fileLen = savedLen;
    Mock_HAL_OS_AdvanceTicks((uint32_t)offSec * HAL_TICKS_PER_SEC);
    GPS_Init();
}

static void test_RebootWhileDriving_BridgesKm(void)
{
    unsigned char saved[64];
    int savedLen;
    double kmBefore;

    /* Log thực tế: thiết bị khởi động lại giữa đường, km lùi 180..250 m mỗi
     * lần và không tính đoạn chạy trong lúc khởi động */
    Begin("Khoi dong lai khi dang chay 40 s / 400 m -> noi km");
    Drive(37);                     /* lần lưu cuối không trùng mẫu cuối */
    kmBefore = GPS_GetTotalKm();
    savedLen = s_fileLen;
    memcpy(saved, s_file, sizeof(saved));
    Move(400.0);                   /* chạy 400 m trong 40 s khởi động lại */
    Reboot(saved, savedLen, 40);
    SendFix(36.0);
    AssertKm(kmBefore + 0.40, 0.005, "fix dau sau khoi dong");
    Drive(10);
    AssertKm(kmBefore + 0.50, 0.005, "chay tiep 10 s");

    Begin("Khoi dong lai sau 15 phut -> khong noi");
    Drive(37);
    kmBefore = GPS_GetTotalKm();
    savedLen = s_fileLen;
    memcpy(saved, s_file, sizeof(saved));
    Move(3000.0);
    Reboot(saved, savedLen, 900);
    SendFix(36.0);
    /* chỉ còn km đã lưu (lưu mỗi 10 s nên thiếu < 10 s x 10 m) */
    assert(GPS_GetTotalKm() <= kmBefore + 0.0005);
    assert(GPS_GetTotalKm() >= kmBefore - 0.11);
    printf("  khong noi sau 900 s: km=%.3f\n", GPS_GetTotalKm());

    Begin("Khoi dong lai khi dang do, troi 30 m -> khong cong");
    Drive(20);
    Stand(15);
    kmBefore = GPS_GetTotalKm();
    savedLen = s_fileLen;
    memcpy(saved, s_file, sizeof(saved));
    Move(30.0);
    Reboot(saved, savedLen, 60);
    SendFix(0.0);
    AssertKm(kmBefore, 0.0005, "fix dau sau khoi dong, troi 30 m");
}

static void test_DateTimeUtils(void)
{
    HalDateTime_t t;

    printf("[TEST] UTIL_ParseDateTime / UTIL_DateTimeToSec\n");
    assert(UTIL_ParseDateTime("2026-11-18 10:46:01", &t) == 0);
    assert(t.tm_year == 2026 && t.tm_mon == 11 && t.tm_mday == 18 &&
           t.tm_hour == 10 && t.tm_min == 46 && t.tm_sec == 1);
    /* 2026-11-18 10:46:01 cách 2000-01-01 00:00:00 đúng 848313961 s (python datetime) */
    assert(UTIL_DateTimeToSec(&t) == 848313961u);
    assert(UTIL_ParseDateTime("0000-00-00 00:00:00", &t) != 0);
    assert(UTIL_ParseDateTime("2026-13-01 00:00:00", &t) != 0);
    assert(UTIL_ParseDateTime("2026-11-18", &t) != 0);
    assert(UTIL_ParseDateTime("2026-11-18 10:46:01x", &t) != 0);
    assert(UTIL_ParseDateTime(NULL, &t) != 0);
}

int main(void)
{
    test_DateTimeUtils();
    GPS_Init();
    test_ShortFullNoFix_IsBridged();
    test_LongFullNoFix_DropsReference();
    test_ShortShortNoFix_IsBridged();
    test_RegainWhileStopped();
    test_RestartRestoresKm();
    test_RebootWhileDriving_BridgesKm();
    printf("[PASS] test_gps_odometer\n");
    return 0;
}

/* ===================== Bản giả phụ thuộc của app_gps.c ===================== */

void HAL_LOG_Print(const char *fmt, ...) { (void)fmt; }
int CFG_GetSpeedThresh(void) { return 5; }
int CFG_GetAccMode(void) { return 0; }

int HAL_GNSS_GetRtc(HalDateTime_t *rtc)
{
    /* Giờ chạy theo tick giả, bắt đầu 08:00:00 */
    uint32_t sec = 8u * 3600u + GetTickNow() / (uint32_t)HAL_TICKS_PER_SEC;
    memset(rtc, 0, sizeof(*rtc));
    rtc->tm_year = 2026; rtc->tm_mon = 10; rtc->tm_mday = (uint8_t)s_rtcMday;
    rtc->tm_hour = (uint8_t)((sec / 3600u) % 24u);
    rtc->tm_min = (uint8_t)((sec / 60u) % 60u);
    rtc->tm_sec = (uint8_t)(sec % 60u);
    return 0;
}
int HAL_GNSS_SetRtc(const HalDateTime_t *rtc) { (void)rtc; return 0; }
int HAL_GNSS_AgpsRequest(void) { return 0; }
int HAL_GNSS_IsPowered(void) { return 1; }
int HAL_LBS_Request(uint32_t timeoutSec) { (void)timeoutSec; return -1; }
int HAL_LBS_GetResult(HalLbsResult_t *out) { (void)out; return 0; }
void HAL_NET_NitzEnable(void) {}   /* app_utils.c */

static int s_filePos;
HalFile_t HAL_FS_Open(const char *path, const char *mode)
{
    /* Chỉ giả lập file km ngày; file vị trí cuối (rv26_pos.dat) coi như không có */
    if (strstr(path, "_pos.") != NULL) return HAL_FS_INVALID_HANDLE;
    if (mode[0] == 'r' && s_fileLen < 0) return HAL_FS_INVALID_HANDLE;
    if (mode[0] == 'w') s_fileLen = 0;
    s_filePos = 0;
    return (HalFile_t)s_file;
}
int HAL_FS_Close(HalFile_t f) { (void)f; return 0; }
int HAL_FS_Read(HalFile_t f, void *buf, uint32_t size)
{
    int n = s_fileLen - s_filePos;
    (void)f;
    if (n > (int)size) n = (int)size;
    memcpy(buf, s_file + s_filePos, (size_t)n);
    s_filePos += n;
    return n;
}
int HAL_FS_Write(HalFile_t f, const void *buf, uint32_t size)
{
    (void)f;
    if (s_filePos + (int)size > (int)sizeof(s_file)) return -1;
    memcpy(s_file + s_filePos, buf, size);
    s_filePos += (int)size;
    if (s_filePos > s_fileLen) s_fileLen = s_filePos;
    return (int)size;
}
int HAL_FS_Seek(HalFile_t f, int32_t offset, int whence) { (void)f; (void)whence; s_filePos = offset; return offset; }
int HAL_FS_Size(HalFile_t f) { (void)f; return s_fileLen; }
int HAL_FS_Delete(const char *path) { (void)path; s_fileLen = -1; return 0; }
