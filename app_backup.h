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

void Backup_Init(void);
void Backup_Push(const BackupRecord_t *rec);
int  Backup_Pop(BackupRecord_t *out);      /* lấy + xoá bản ghi cũ nhất (legacy) */
int  Backup_Peek(BackupRecord_t *out);     /* đọc bản ghi cũ nhất, KHÔNG xoá */
int  Backup_CommitPop(void);               /* xoá bản ghi đầu sau khi gửi OK */
int  Backup_Count(void);
void Backup_Clear(void);
void Backup_Flush(void);

/** Lưu định kỳ snapshot (mỗi 10s) khi tracking */
void Backup_OnTick(const BackupRecord_t *rec, uint32_t nowTick);

#endif /* APP_BACKUP_H */
