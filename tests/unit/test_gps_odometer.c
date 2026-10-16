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

int main(void)
{
    GPS_Init();
    test_ShortFullNoFix_IsBridged();
    test_LongFullNoFix_DropsReference();
    test_ShortShortNoFix_IsBridged();
    test_RegainWhileStopped();
    test_RestartRestoresKm();
    printf("[PASS] test_gps_odometer\n");
    return 0;
}

/* ===================== Bản giả phụ thuộc của app_gps.c ===================== */

void HAL_LOG_Print(const char *fmt, ...) { (void)fmt; }
int CFG_GetSpeedThresh(void) { return 5; }
int CFG_GetAccMode(void) { return 0; }

int HAL_GNSS_GetRtc(HalDateTime_t *rtc)
{
    memset(rtc, 0, sizeof(*rtc));
    rtc->tm_year = 2026; rtc->tm_mon = 10; rtc->tm_mday = (uint8_t)s_rtcMday;
    return 0;
}
int HAL_GNSS_SetRtc(const HalDateTime_t *rtc) { (void)rtc; return 0; }
int HAL_GNSS_AgpsRequest(void) { return 0; }
void HAL_NET_NitzEnable(void) {}   /* app_utils.c */

static int s_filePos;
HalFile_t HAL_FS_Open(const char *path, const char *mode)
{
    (void)path;
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
