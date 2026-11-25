#ifndef APP_BACKUP_H
#define APP_BACKUP_H

#include <stdint.h>

typedef struct {
    char     datetime[24];
    double   lat;
    double   lon;
    double   speedKph;
    uint32_t deviceStatus;
    uint8_t  csq;
    int      sats;
    double   totalKm;
    double   headingDeg;
    float    voltage;
    float    fuelRs232;
    float    fuelAF;
    float    temperature;
    uint8_t  used;
} BackupRecord_t;

/* Gia tri truong `used`: ban ghi la 1 manh cua khung van ban (ban tin 5/6 luu khi mat mang) */
#define BACKUP_KIND_TEXT   0xA5
#define BACKUP_TEXT_MAX    460   /* do dai toi da 1 khung van ban */

void Backup_Init(void);
void Backup_Push(const BackupRecord_t *rec);
int  Backup_Pop(BackupRecord_t *out);      /* lấy + xoá bản ghi cũ nhất (legacy) */
int  Backup_Peek(BackupRecord_t *out);     /* đọc bản ghi cũ nhất, KHÔNG xoá */
int  Backup_CommitPop(void);               /* xoá bản ghi đầu sau khi gửi OK */
int  Backup_CommitPopN(int n);            /* xoá n ô đầu hàng đợi */
int  Backup_Count(void);

/**
 * Lưu 1 khung văn bản (bản tin đã đóng gói sẵn) vào hàng đợi, giữ đúng thứ tự
 * thời gian với các bản ghi vị trí.
 * @return 0 nếu đã lưu, -1 nếu lỗi / quá dài
 */
int  Backup_PushText(const char *text);

/**
 * Ghép khung văn bản ở đầu hàng đợi (gọi khi Backup_Peek trả bản ghi có
 * used == BACKUP_KIND_TEXT).
 * @param slots [out] số ô cần Backup_CommitPopN sau khi xử lý xong
 * @return 1: out chứa khung hoàn chỉnh; 0: khung hỏng/thiếu, chỉ cần bỏ `slots` ô;
 *         -1: lỗi đọc, thử lại sau
 */
int  Backup_PeekText(char *out, uint32_t outSize, int *slots);
void Backup_Clear(void);
/** Trang thai de chan doan: noi luu (1 = SPI flash, 0 = EFS), so o cho gui, so lan ghi flash loi */
void Backup_GetStatus(int *useFlash, int *count, uint32_t *writeErrors);
void Backup_Flush(void);

/** Noi luu backup dang dung (LED nguon hien thi theo gia tri nay) */
#define BACKUP_MEDIA_NONE   0   /* noi luu dang dung ghi loi (khong flash + EFS loi) */
#define BACKUP_MEDIA_EFS    1   /* bo nho trong: file C:/rv26_bak.dat */
#define BACKUP_MEDIA_FLASH  2   /* bo nho ngoai: flash SPI */
int  Backup_GetMedia(void);

/** Lưu định kỳ snapshot (mỗi 10s) khi tracking */
void Backup_OnTick(const BackupRecord_t *rec, uint32_t nowTick);

#endif /* APP_BACKUP_H */
