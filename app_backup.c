#include "simcom_api.h"
#include "app_config.h"
#include "app_backup.h"
#include <stdio.h>
#include <string.h>

#define BACKUP_HDR_MAGIC  0x52323642u /* 'R26B' */

typedef struct {
    uint32_t magic;
    uint32_t head;   /* write index */
    uint32_t tail;   /* read index */
    uint32_t count;
} BackupHdr_t;

static BackupHdr_t s_hdr;
static uint32_t    s_lastStoreTick = 0;
static int         s_inited = 0;
static int         s_hdrDirty = 0;

/* Bảo vệ s_hdr + file backup: task NASA (push/pop) vs task SMS (na,34/35 clear) */
static sMutexRef   s_bakMutex = NULL;

static void BackupLock(void)
{
    if (s_bakMutex) sAPI_MutexLock(s_bakMutex, SC_SUSPEND);
}

static void BackupUnlock(void)
{
    if (s_bakMutex) sAPI_MutexUnLock(s_bakMutex);
}

static int Backup_LoadHdr(void)
{
    SCFILE *fp = sAPI_fopen(NASA_BACKUP_FILE, "rb");
    if (!fp) {
        s_hdr.magic = BACKUP_HDR_MAGIC;
        s_hdr.head = s_hdr.tail = s_hdr.count = 0;
        return -1;
    }
    if (sAPI_fread(&s_hdr, sizeof(s_hdr), 1, fp) != 1 || s_hdr.magic != BACKUP_HDR_MAGIC) {
        s_hdr.magic = BACKUP_HDR_MAGIC;
        s_hdr.head = s_hdr.tail = s_hdr.count = 0;
    }
    if (s_hdr.count > NASA_BACKUP_MAX_RECORDS) {
        s_hdr.head = s_hdr.tail = s_hdr.count = 0;
    }
    sAPI_fclose(fp);
    s_hdrDirty = 0;
    return 0;
}

static int Backup_SaveHdr(int force)
{
    if (!s_hdrDirty && !force) return 0;

    SCFILE *fp = sAPI_fopen(NASA_BACKUP_FILE, "rb+");
    if (!fp) {
        fp = sAPI_fopen(NASA_BACKUP_FILE, "wb");
        if (!fp) return -1;
        sAPI_fwrite(&s_hdr, sizeof(s_hdr), 1, fp);
        sAPI_fclose(fp);
        s_hdrDirty = 0;
        return 0;
    }
    sAPI_fseek(fp, 0, FS_SEEK_BEGIN);
    sAPI_fwrite(&s_hdr, sizeof(s_hdr), 1, fp);
    sAPI_fclose(fp);
    s_hdrDirty = 0;
    return 0;
}

void Backup_Flush(void)
{
    BackupLock();
    if (s_hdrDirty) {
        Backup_SaveHdr(1);
    }
    BackupUnlock();
}

void Backup_Init(void)
{
    if (s_inited) return;
    if (s_bakMutex == NULL) {
        sAPI_MutexCreate(&s_bakMutex, SC_FIFO);
    }
    Backup_LoadHdr();
    s_inited = 1;
    sAPI_Debug("[Backup] init count=%lu", (unsigned long)s_hdr.count);
}

/* Thân push — caller phải giữ mutex */
static void Backup_PushLocked(const BackupRecord_t *rec)
{
    SCFILE *fp;
    long offset;
    BackupRecord_t tmp;

    if (s_hdr.count >= NASA_BACKUP_MAX_RECORDS) {
        /* ring: drop oldest */
        s_hdr.tail = (s_hdr.tail + 1) % NASA_BACKUP_MAX_RECORDS;
        s_hdr.count--;
    }

    fp = sAPI_fopen(NASA_BACKUP_FILE, "rb+");
    if (!fp) {
        /* rb+ fail: chỉ tạo mới bằng wb khi file thực sự chưa tồn tại.
         * Nếu file có mà mở lỗi tạm thời (FS bận), bỏ qua lần push này
         * thay vì wb truncate xoá sạch toàn bộ backup. */
        SCFILE *probe = sAPI_fopen(NASA_BACKUP_FILE, "rb");
        if (probe) {
            sAPI_fclose(probe);
            sAPI_Debug("[Backup] rb+ fail nhung file ton tai, skip push");
            return;
        }
        fp = sAPI_fopen(NASA_BACKUP_FILE, "wb");
        if (!fp) return;
        sAPI_fwrite(&s_hdr, sizeof(s_hdr), 1, fp);
        /* pre-size not required; write at offset */
    }

    offset = (long)(sizeof(BackupHdr_t) + s_hdr.head * sizeof(BackupRecord_t));
    sAPI_fseek(fp, offset, FS_SEEK_BEGIN);
    tmp = *rec;
    tmp.used = 1;
    sAPI_fwrite(&tmp, sizeof(tmp), 1, fp);
    sAPI_fclose(fp);

    s_hdr.head = (s_hdr.head + 1) % NASA_BACKUP_MAX_RECORDS;
    s_hdr.count++;
    s_hdrDirty = 1;
    Backup_SaveHdr(1); /* Ghi đè tức thời khi push để bảo toàn dữ liệu */
}

void Backup_Push(const BackupRecord_t *rec)
{
    if (!rec) return;
    Backup_Init();
    BackupLock();
    Backup_PushLocked(rec);
    BackupUnlock();
}

int Backup_Pop(BackupRecord_t *out)
{
    SCFILE *fp;
    long offset;
    BackupRecord_t tmp;

    if (!out) return 0;
    Backup_Init();
    BackupLock();
    if (s_hdr.count == 0) {
        BackupUnlock();
        return 0;
    }

    fp = sAPI_fopen(NASA_BACKUP_FILE, "rb");
    if (!fp) {
        BackupUnlock();
        return 0;
    }

    offset = (long)(sizeof(BackupHdr_t) + s_hdr.tail * sizeof(BackupRecord_t));
    sAPI_fseek(fp, offset, FS_SEEK_BEGIN);
    if (sAPI_fread(&tmp, sizeof(tmp), 1, fp) != 1) {
        sAPI_fclose(fp);
        BackupUnlock();
        return 0;
    }
    sAPI_fclose(fp);

    *out = tmp;
    s_hdr.tail = (s_hdr.tail + 1) % NASA_BACKUP_MAX_RECORDS;
    s_hdr.count--;
    s_hdrDirty = 1;
    /* Không ghi header xuống đĩa tức thời ở đây để tối ưu hóa trong vòng lặp Replay Burst */
    BackupUnlock();
    return 1;
}

int Backup_Count(void)
{
    int count;
    Backup_Init();
    BackupLock();
    count = (int)s_hdr.count;
    BackupUnlock();
    return count;
}

void Backup_Clear(void)
{
    Backup_Init();
    BackupLock();
    s_hdr.magic = BACKUP_HDR_MAGIC;
    s_hdr.head = s_hdr.tail = s_hdr.count = 0;
    sAPI_remove(NASA_BACKUP_FILE);
    Backup_SaveHdr(1);
    BackupUnlock();
    sAPI_Debug("[Backup] cleared");
}

void Backup_OnTick(const BackupRecord_t *rec, uint32_t nowTick)
{
    if (!rec) return;
    Backup_Init();
    BackupLock();
    if (s_lastStoreTick == 0 ||
        (nowTick - s_lastStoreTick) >= (uint32_t)(NASA_BACKUP_INTERVAL_SEC * SC_TICKS_PER_SECOND)) {
        Backup_PushLocked(rec);
        s_lastStoreTick = nowTick;
    }
    BackupUnlock();
}
