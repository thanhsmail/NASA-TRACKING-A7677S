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
    return 0;
}

static int Backup_SaveHdr(void)
{
    SCFILE *fp = sAPI_fopen(NASA_BACKUP_FILE, "rb+");
    if (!fp) {
        fp = sAPI_fopen(NASA_BACKUP_FILE, "wb");
        if (!fp) return -1;
        sAPI_fwrite(&s_hdr, sizeof(s_hdr), 1, fp);
        sAPI_fclose(fp);
        return 0;
    }
    sAPI_fseek(fp, 0, FS_SEEK_BEGIN);
    sAPI_fwrite(&s_hdr, sizeof(s_hdr), 1, fp);
    sAPI_fclose(fp);
    return 0;
}

void Backup_Init(void)
{
    if (s_inited) return;
    Backup_LoadHdr();
    s_inited = 1;
    sAPI_Debug("[Backup] init count=%lu", (unsigned long)s_hdr.count);
}

void Backup_Push(const BackupRecord_t *rec)
{
    SCFILE *fp;
    long offset;
    BackupRecord_t tmp;

    if (!rec) return;
    Backup_Init();

    if (s_hdr.count >= NASA_BACKUP_MAX_RECORDS) {
        /* ring: drop oldest */
        s_hdr.tail = (s_hdr.tail + 1) % NASA_BACKUP_MAX_RECORDS;
        s_hdr.count--;
    }

    fp = sAPI_fopen(NASA_BACKUP_FILE, "rb+");
    if (!fp) {
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
    Backup_SaveHdr();
}

int Backup_Pop(BackupRecord_t *out)
{
    SCFILE *fp;
    long offset;
    BackupRecord_t tmp;

    if (!out) return 0;
    Backup_Init();
    if (s_hdr.count == 0) return 0;

    fp = sAPI_fopen(NASA_BACKUP_FILE, "rb");
    if (!fp) return 0;

    offset = (long)(sizeof(BackupHdr_t) + s_hdr.tail * sizeof(BackupRecord_t));
    sAPI_fseek(fp, offset, FS_SEEK_BEGIN);
    if (sAPI_fread(&tmp, sizeof(tmp), 1, fp) != 1) {
        sAPI_fclose(fp);
        return 0;
    }
    sAPI_fclose(fp);

    *out = tmp;
    s_hdr.tail = (s_hdr.tail + 1) % NASA_BACKUP_MAX_RECORDS;
    s_hdr.count--;
    Backup_SaveHdr();
    return 1;
}

int Backup_Count(void)
{
    Backup_Init();
    return (int)s_hdr.count;
}

void Backup_Clear(void)
{
    s_hdr.magic = BACKUP_HDR_MAGIC;
    s_hdr.head = s_hdr.tail = s_hdr.count = 0;
    sAPI_remove(NASA_BACKUP_FILE);
    Backup_SaveHdr();
    sAPI_Debug("[Backup] cleared");
}

void Backup_OnTick(const BackupRecord_t *rec, uint32_t nowTick)
{
    if (!rec) return;
    Backup_Init();
    if (s_lastStoreTick == 0 ||
        (nowTick - s_lastStoreTick) >= (uint32_t)(NASA_BACKUP_INTERVAL_SEC * SC_TICKS_PER_SECOND)) {
        Backup_Push(rec);
        s_lastStoreTick = nowTick;
    }
}
