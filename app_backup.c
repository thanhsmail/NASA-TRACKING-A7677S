/**
 * @file app_backup.c
 * @brief Offline Backup Storage module -- NASA Tracking
 *
 * Luu y: File nay KHONG #include simcom_api.h truc tiep.
 * Moi tuong tac xuyet suot QOS / File / Mutex qua HAL layer.
 *
 * Bo cuc SPI Flash (EN25QH64A, 8MB):
 *   Sector 0..1 : cau hinh (app_cfg.c)
 *   Sector 2..3 : journal header (256 slot x 32 byte, 2 sector luan phien)
 *   Sector 4..  : vong ban ghi, moi o 128 byte = BackupRecord_t + magic + seq + CRC
 *
 * Moi ban ghi tu mo ta (seq + CRC) nen header chi la diem moc (checkpoint):
 * khi khoi dong, head duoc do tiep tu diem moc theo seq (roll-forward). Nho do
 * reset/mat dien luc dang mat mang khong lam mat ban ghi da ghi xuong flash.
 */
#include "hal/hal_log.h"
#include "hal/hal_os.h"
#include "hal/hal_fs.h"
#include "app_config.h"
#include "app_backup.h"
#include "app_gps.h"
#include "app_cfg.h"
#include "drv_en25qh64a.h"
#include "app_utils.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---- EFS (du phong khi khong co SPI flash) ---- */
#define EFS_HDR_MAGIC           0x52323643u /* 'R26C' -- v2: header 24 byte */
#define EFS_HDR_MAGIC_LEGACY    0x52323642u /* 'R26B' -- v1: header 20 byte */

/* ---- SPI Flash ---- */
#define FLASH_HDR_MAGIC         0x52323644u /* 'R26D' -- v3: ban ghi co seq + CRC */
#define FLASH_SLOT_MAGIC        0x52454331u /* 'REC1' */
#define FLASH_HDR_FIRST_SECTOR  2
#define FLASH_HDR_SECTORS       2
#define FLASH_HDR_BASE_ADDR     (FLASH_HDR_FIRST_SECTOR * EN25_SECTOR_SIZE)
#define FLASH_HDR_SLOT_SIZE     32
#define FLASH_HDR_SLOTS_PER_SEC (EN25_SECTOR_SIZE / FLASH_HDR_SLOT_SIZE)        /* 128 */
#define FLASH_HDR_SLOTS         (FLASH_HDR_SECTORS * FLASH_HDR_SLOTS_PER_SEC)   /* 256 */

#define FLASH_LOG_START_ADDR    ((FLASH_HDR_FIRST_SECTOR + FLASH_HDR_SECTORS) * EN25_SECTOR_SIZE)
#define FLASH_LOG_END_ADDR      EN25_FLASH_SIZE
#define FLASH_RECORD_SIZE       128         /* Size per record padded */
#define FLASH_MAX_RECORDS       ((FLASH_LOG_END_ADDR - FLASH_LOG_START_ADDR) / FLASH_RECORD_SIZE)
#define FLASH_RECS_PER_SECTOR   (EN25_SECTOR_SIZE / FLASH_RECORD_SIZE) /* 32 */
#define FLASH_LOG_SECTORS       (FLASH_MAX_RECORDS / FLASH_RECS_PER_SECTOR)

#define BACKUP_HDR_FLUSH_PUSHES 16  /* luu diem moc sau moi N ban ghi */
#define BACKUP_WRITE_RETRIES    3   /* so o hong lien tiep truoc khi bo ban ghi */
#define BACKUP_PEEK_MAX_SKIP    64  /* so o hong toi da bo qua trong 1 lan Peek */

typedef struct {
    uint32_t magic;
    uint32_t head;   /* write index */
    uint32_t tail;   /* read index */
    uint32_t count;
    uint32_t seq;
    uint32_t crc32;  /* checksum */
} EfsHdr_t;

typedef struct {
    uint32_t magic;
    uint32_t head;   /* write index */
    uint32_t tail;   /* read index */
    uint32_t count;  /* so o tu tail den head (ke ca o hong) */
    uint32_t seq;    /* so thu tu journal header */
    uint32_t recSeq; /* seq cua ban ghi ke tiep se ghi tai head */
    uint32_t crc32;
} FlashHdr_t;

typedef struct {
    BackupRecord_t rec;
    uint32_t magic;
    uint32_t seq;    /* tang dan theo tung ban ghi */
    uint32_t crc32;
} FlashSlot_t;

/* Manh khung van ban, nam de len vung du lieu cua BackupRecord_t (truoc truong `used`) */
#define TEXT_CHUNK_DATA         92
#define TEXT_MAX_PARTS          ((BACKUP_TEXT_MAX + TEXT_CHUNK_DATA - 1) / TEXT_CHUNK_DATA)
#define TEXT_PEEK_MAX_SLOTS     64

typedef struct {
    uint8_t part;    /* 0..parts-1 */
    uint8_t parts;
    uint8_t len;     /* so byte van ban trong manh nay */
    uint8_t rsv;
    char    text[TEXT_CHUNK_DATA];
} TextChunk_t;

#define EFS_HDR_SIZE            ((uint32_t)sizeof(EfsHdr_t))

typedef char BackupHdrFitsSlot[(sizeof(FlashHdr_t) <= FLASH_HDR_SLOT_SIZE) ? 1 : -1];
typedef char BackupRecFits128[(sizeof(FlashSlot_t) <= FLASH_RECORD_SIZE) ? 1 : -1];
typedef char BackupTextChunkFits[(sizeof(TextChunk_t) <= offsetof(BackupRecord_t, used)) ? 1 : -1];
typedef char BackupRingSectorAligned[(FLASH_MAX_RECORDS % FLASH_RECS_PER_SECTOR == 0) ? 1 : -1];

/* Trang thai hang doi (dung chung cho ca 2 che do luu) */
static uint32_t    s_head = 0;
static uint32_t    s_tail = 0;
static uint32_t    s_count = 0;

static uint32_t    s_lastStoreTick = 0;
static int         s_inited = 0;
static int         s_hdrDirty = 0;
static int         s_useFlashStorage = 0;

/* Rieng cho SPI flash */
static uint32_t    s_recSeq = 1;
static uint32_t    s_hdrSeq = 0;
static uint32_t    s_hdrSlot = FLASH_HDR_SLOTS - 1;
static uint32_t    s_pushSinceFlush = 0;
static uint32_t    s_writeErrCount = 0;

/* Rieng cho EFS */
static uint32_t    s_efsRecBase = EFS_HDR_SIZE;

static HalMutexRef_t s_bakMutex = NULL;

static void BackupLock(void)
{
    HAL_OS_MutexLock(s_bakMutex);
}

static void BackupUnlock(void)
{
    HAL_OS_MutexUnlock(s_bakMutex);
}

static uint32_t Crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* ============================ SPI Flash =================================== */

static uint32_t FlashRecordAddr(uint32_t index)
{
    return FLASH_LOG_START_ADDR + (index % FLASH_MAX_RECORDS) * FLASH_RECORD_SIZE;
}

static int SlotValid(const FlashSlot_t *slot)
{
    return (slot->magic == FLASH_SLOT_MAGIC &&
            slot->crc32 == Crc32((const uint8_t *)slot, offsetof(FlashSlot_t, crc32))) ? 1 : 0;
}

/** Doc 1 o. @param blank [out] 1 neu o con trong (toan 0xFF). @return 0 neu doc duoc */
static int ReadSlot(uint32_t index, FlashSlot_t *slot, int *blank)
{
    uint8_t raw[FLASH_RECORD_SIZE];
    uint32_t i;

    if (EN25_Read(FlashRecordAddr(index), raw, sizeof(raw)) != 0) return -1;
    memcpy(slot, raw, sizeof(*slot));
    if (blank) {
        *blank = 1;
        for (i = 0; i < sizeof(raw); i++) {
            if (raw[i] != 0xFF) { *blank = 0; break; }
        }
    }
    return 0;
}

static int Backup_LoadHeaderFromFlash(void)
{
    uint32_t slot;
    int foundBest = 0;
    FlashHdr_t h;
    FlashHdr_t best;

    memset(&best, 0, sizeof(best));
    for (slot = 0; slot < FLASH_HDR_SLOTS; slot++) {
        uint32_t addr = FLASH_HDR_BASE_ADDR + slot * FLASH_HDR_SLOT_SIZE;
        if (EN25_Read(addr, (uint8_t *)&h, sizeof(h)) != 0) continue;

        if (h.magic != FLASH_HDR_MAGIC) continue;
        if (h.crc32 != Crc32((const uint8_t *)&h, offsetof(FlashHdr_t, crc32))) continue;
        if (h.head >= FLASH_MAX_RECORDS || h.tail >= FLASH_MAX_RECORDS ||
            h.count > FLASH_MAX_RECORDS) continue;

        if (!foundBest || h.seq > best.seq) {
            best = h;
            s_hdrSlot = slot;
            foundBest = 1;
        }
    }

    if (!foundBest) return -1;

    s_head = best.head;
    s_tail = best.tail;
    s_count = best.count;
    s_hdrSeq = best.seq;
    s_recSeq = best.recSeq;
    HAL_LOG("[Backup] Loaded Header from SPI Flash OK (count=%lu, seq=%lu, slot=%lu)",
            (unsigned long)s_count, (unsigned long)s_hdrSeq, (unsigned long)s_hdrSlot);
    return 0;
}

static int Backup_SaveHeaderToFlash(void)
{
    int attempt;

    for (attempt = 0; attempt < BACKUP_WRITE_RETRIES; attempt++) {
        FlashHdr_t h;
        FlashHdr_t chk;
        uint32_t nextSlot = (s_hdrSlot + 1) % FLASH_HDR_SLOTS;
        uint32_t addr = FLASH_HDR_BASE_ADDR + nextSlot * FLASH_HDR_SLOT_SIZE;

        /* Sang sector moi thi xoa no; sector con lai van giu header hop le */
        if ((nextSlot % FLASH_HDR_SLOTS_PER_SEC) == 0) {
            if (EN25_EraseSector(addr) != 0) return -1;
        }

        memset(&h, 0, sizeof(h));
        h.magic = FLASH_HDR_MAGIC;
        h.head = s_head;
        h.tail = s_tail;
        h.count = s_count;
        h.seq = s_hdrSeq + 1;
        h.recSeq = s_recSeq;
        h.crc32 = Crc32((const uint8_t *)&h, offsetof(FlashHdr_t, crc32));

        s_hdrSlot = nextSlot;
        if (EN25_Write(addr, (const uint8_t *)&h, sizeof(h)) == 0 &&
            EN25_Read(addr, (uint8_t *)&chk, sizeof(chk)) == 0 &&
            memcmp(&h, &chk, sizeof(h)) == 0) {
            s_hdrSeq = h.seq;
            return 0;
        }
        HAL_LOG("[Backup] Header slot %lu write fail, try next", (unsigned long)nextSlot);
    }
    return -1;
}

/**
 * Do tiep head tu diem moc: nhan cac ban ghi da ghi sau lan luu header cuoi.
 * Ban ghi ke tiep phai co dung seq mong doi; cac o hong / o trong (lenh ghi bi
 * flash bo qua) nam truoc no duoc tinh la lo trong, toi da 1 sector.
 */
static void Backup_RollForward(void)
{
    uint32_t absorbed = 0;

    while (s_count < FLASH_MAX_RECORDS) {
        FlashSlot_t slot;
        uint32_t look;
        int matched = 0;

        for (look = 0; look < FLASH_RECS_PER_SECTOR &&
                       (s_count + look) < FLASH_MAX_RECORDS; look++) {
            if (ReadSlot(s_head + look, &slot, NULL) != 0) break;
            if (SlotValid(&slot) && slot.seq == s_recSeq) { matched = 1; break; }
        }
        if (!matched) break;

        s_head = (s_head + look + 1) % FLASH_MAX_RECORDS;
        s_count += look + 1;
        s_recSeq++;
        absorbed++;
    }

    if (absorbed > 0) {
        s_hdrDirty = 1;
        HAL_LOG("[Backup] Roll-forward %lu record(s) -> count=%lu head=%lu",
                (unsigned long)absorbed, (unsigned long)s_count, (unsigned long)s_head);
    }
}

/** Khong co header: dung lai hang doi tu chinh cac ban ghi tren flash */
static void Backup_RebuildFromScan(void)
{
    FlashSlot_t slot;
    uint32_t sec;
    uint32_t maxSec = 0, maxSeq = 0;
    uint32_t minSec = 0, minSeq = 0;
    uint32_t last = 0;
    uint32_t i;
    int found = 0;

    s_head = 0;
    s_tail = 0;
    s_count = 0;
    s_recSeq = 1;

    for (sec = 0; sec < FLASH_LOG_SECTORS; sec++) {
        if (ReadSlot(sec * FLASH_RECS_PER_SECTOR, &slot, NULL) != 0) continue;
        if (!SlotValid(&slot)) continue;
        if (!found || slot.seq > maxSeq) { maxSeq = slot.seq; maxSec = sec; }
        if (!found || slot.seq < minSeq) { minSeq = slot.seq; minSec = sec; }
        found = 1;
    }
    if (!found) return;

    for (i = 1; i < FLASH_RECS_PER_SECTOR; i++) {
        if (ReadSlot(maxSec * FLASH_RECS_PER_SECTOR + i, &slot, NULL) != 0) break;
        if (SlotValid(&slot) && slot.seq > maxSeq) {
            maxSeq = slot.seq;
            last = i;
        }
    }

    s_head = (maxSec * FLASH_RECS_PER_SECTOR + last + 1) % FLASH_MAX_RECORDS;
    s_tail = minSec * FLASH_RECS_PER_SECTOR;
    s_count = (s_head + FLASH_MAX_RECORDS - s_tail) % FLASH_MAX_RECORDS;
    s_recSeq = maxSeq + 1;
    if (s_count == 0) {
        /* Vong da day: sector tai head se bi xoa o lan ghi toi */
        s_tail = (s_tail + FLASH_RECS_PER_SECTOR) % FLASH_MAX_RECORDS;
        s_count = FLASH_MAX_RECORDS - FLASH_RECS_PER_SECTOR;
    }
    HAL_LOG("[Backup] Rebuilt from scan (count=%lu head=%lu tail=%lu)",
            (unsigned long)s_count, (unsigned long)s_head, (unsigned long)s_tail);
}

static void Backup_PushToFlash(const BackupRecord_t *rec)
{
    FlashSlot_t slot;
    FlashSlot_t chk;
    int badWrites = 0;
    uint32_t guard;

    for (guard = 0; guard < 2 * FLASH_RECS_PER_SECTOR; guard++) {
        uint32_t addr = FlashRecordAddr(s_head);
        int blank = 0;

        if ((s_head % FLASH_RECS_PER_SECTOR) == 0) {
            /* Sector sap xoa con chua ban ghi chua gui -> bo chung khoi hang doi */
            if (s_count > FLASH_MAX_RECORDS - FLASH_RECS_PER_SECTOR) {
                uint32_t drop = s_count - (FLASH_MAX_RECORDS - FLASH_RECS_PER_SECTOR);
                s_tail = (s_tail + drop) % FLASH_MAX_RECORDS;
                s_count -= drop;
                HAL_LOG("[Backup] Ring full, drop %lu oldest record(s)", (unsigned long)drop);
            }
            if (EN25_EraseSector(addr) != 0) {
                HAL_LOG("[Backup] EraseSector 0x%06X fail, skip push", (unsigned)addr);
                return;
            }
        } else {
            /* NOR khong ghi de duoc: o da co du lieu (ghi do truoc do) thi bo qua */
            if (ReadSlot(s_head, &chk, &blank) != 0) {
                HAL_LOG("[Backup] Read 0x%06X fail, skip push", (unsigned)addr);
                return;
            }
            if (!blank) {
                s_head = (s_head + 1) % FLASH_MAX_RECORDS;
                s_count++;
                s_hdrDirty = 1;
                continue;
            }
        }

        memset(&slot, 0, sizeof(slot));
        memcpy(&slot.rec, rec, sizeof(slot.rec));
        slot.magic = FLASH_SLOT_MAGIC;
        slot.seq = s_recSeq;
        slot.crc32 = Crc32((const uint8_t *)&slot, offsetof(FlashSlot_t, crc32));

        if (EN25_Write(addr, (const uint8_t *)&slot, sizeof(slot)) == 0 &&
            EN25_Read(addr, (uint8_t *)&chk, sizeof(chk)) == 0 &&
            memcmp(&slot, &chk, sizeof(slot)) == 0) {
            s_head = (s_head + 1) % FLASH_MAX_RECORDS;
            s_count++;
            s_recSeq++;
            s_hdrDirty = 1;
            HAL_LOG("[Backup/Push] offline log -> SPI Flash (addr=0x%06X, head=%lu, count=%lu/%lu, dt=%s)",
                    (unsigned)addr, (unsigned long)s_head, (unsigned long)s_count,
                    (unsigned long)FLASH_MAX_RECORDS,
                    (rec->used == BACKUP_KIND_TEXT) ? "text" : rec->datetime);
            return;
        }

        /* Ghi/doc lai sai: de lai o hong, thu o ke tiep */
        HAL_LOG("[Backup] Write 0x%06X verify fail", (unsigned)addr);
        s_writeErrCount++;
        s_head = (s_head + 1) % FLASH_MAX_RECORDS;
        s_count++;
        s_hdrDirty = 1;
        if (++badWrites >= BACKUP_WRITE_RETRIES) break;
    }
    HAL_LOG("[Backup] Push give up, record lost");
}

/* =============================== EFS ====================================== */

/** Mo file backup de doc/ghi ma KHONG xoa noi dung cu; chua co thi tao moi */
static HalFile_t EfsOpenRw(void)
{
    HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "rb+");
    if (!fp) fp = HAL_FS_Open(NASA_BACKUP_FILE, "wb+");
    return fp;
}

/** @return 1 neu file EFS co header hop le (da nap vao out) */
static int Backup_LoadHeaderFromEfs(EfsHdr_t *out, uint32_t *recBase)
{
    HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "rb");
    EfsHdr_t h;
    if (!fp) return 0;

    memset(&h, 0, sizeof(h));
    if (HAL_FS_Read(fp, &h, sizeof(h)) < 20) {
        HAL_FS_Close(fp);
        return 0;
    }
    HAL_FS_Close(fp);

    if (h.magic != EFS_HDR_MAGIC && h.magic != EFS_HDR_MAGIC_LEGACY) {
        HAL_LOG("[Backup] EFS magic invalid 0x%08lX -- ignore file", (unsigned long)h.magic);
        return 0;
    }

    if (h.count > NASA_BACKUP_MAX_RECORDS || h.head >= NASA_BACKUP_MAX_RECORDS ||
        h.tail >= NASA_BACKUP_MAX_RECORDS) {
        HAL_LOG("[Backup] EFS header out of range count=%lu -- ignore", (unsigned long)h.count);
        return 0;
    }

    *out = h;
    *recBase = (h.magic == EFS_HDR_MAGIC_LEGACY) ? 20u : EFS_HDR_SIZE;
    HAL_LOG("[Backup] EFS loaded count=%lu head=%lu tail=%lu base=%lu",
            (unsigned long)h.count, (unsigned long)h.head,
            (unsigned long)h.tail, (unsigned long)*recBase);
    return 1;
}

static void Backup_SaveHeaderToEfs(void)
{
    HalFile_t fp = EfsOpenRw();
    EfsHdr_t h;

    if (!fp) return;

    memset(&h, 0, sizeof(h));
    h.magic = (s_efsRecBase == 20u) ? EFS_HDR_MAGIC_LEGACY : EFS_HDR_MAGIC;
    h.head = s_head;
    h.tail = s_tail;
    h.count = s_count;
    h.crc32 = Buffer_GetChecksum((const uint8_t *)&h, 20);

    if (HAL_FS_Seek(fp, 0, HAL_FS_SEEK_SET) == 0) {
        (void)HAL_FS_Write(fp, &h, s_efsRecBase);
    }
    HAL_FS_Close(fp);
}

static void Backup_PushToEfs(const BackupRecord_t *rec)
{
    HalFile_t fp;
    long offset;

    if (s_count >= NASA_BACKUP_MAX_RECORDS) {
        s_tail = (s_tail + 1) % NASA_BACKUP_MAX_RECORDS;
        s_count = NASA_BACKUP_MAX_RECORDS - 1;
    }

    fp = EfsOpenRw();
    if (!fp) {
        HAL_LOG("[Backup] EFS open fail, skip push");
        return;
    }

    offset = (long)(s_efsRecBase + s_head * sizeof(BackupRecord_t));
    if (HAL_FS_Seek(fp, offset, HAL_FS_SEEK_SET) != 0) {
        HAL_FS_Close(fp);
        HAL_LOG("[Backup] EFS seek fail off=%ld head=%lu", offset, (unsigned long)s_head);
        return;
    }

    if (HAL_FS_Write(fp, rec, sizeof(BackupRecord_t)) != (int)sizeof(BackupRecord_t)) {
        HAL_FS_Close(fp);
        HAL_LOG("[Backup] EFS write record fail off=%ld", offset);
        return;
    }

    HAL_FS_Close(fp);
    s_head = (s_head + 1) % NASA_BACKUP_MAX_RECORDS;
    s_count++;
    s_hdrDirty = 1;
}

/* ============================= Chung ====================================== */

static void Backup_FlushHeader(void)
{
    if (!s_hdrDirty) return;
    if (s_useFlashStorage) {
        if (Backup_SaveHeaderToFlash() == 0) {
            s_hdrDirty = 0;
            s_pushSinceFlush = 0;
        }
    } else {
        Backup_SaveHeaderToEfs();
        s_hdrDirty = 0;
    }
}

static void Backup_SelectStorageEngine(void)
{
    EfsHdr_t efs;
    uint32_t efsBase = EFS_HDR_SIZE;
    int efsOk;

    s_head = 0;
    s_tail = 0;
    s_count = 0;
    s_hdrDirty = 0;
    s_pushSinceFlush = 0;
    s_efsRecBase = EFS_HDR_SIZE;

    if (EN25_IsReady()) {
        if (Backup_LoadHeaderFromFlash() == 0) {
            Backup_RollForward();
        } else {
            /* Chua co header (lan dau / dinh dang cu): ghi tu dau journal */
            s_hdrSlot = FLASH_HDR_SLOTS - 1;
            s_hdrSeq = 0;
            Backup_RebuildFromScan();
            s_hdrDirty = 1;
        }

        if (s_count > 0) {
            s_useFlashStorage = 1;
            Backup_FlushHeader();
            HAL_LOG("[Backup] Use SPI Flash (count=%lu)", (unsigned long)s_count);
            return;
        }

        /* Flash rong: phat not du lieu con ton tren EFS (neu co) truoc */
        efsOk = Backup_LoadHeaderFromEfs(&efs, &efsBase);
        if (efsOk && efs.count > 0) {
            s_useFlashStorage = 0;
            s_head = efs.head;
            s_tail = efs.tail;
            s_count = efs.count;
            s_efsRecBase = efsBase;
            s_hdrDirty = 0;
            HAL_LOG("[Backup] Use EFS file (count=%lu, recBase=%lu) -- flash empty",
                    (unsigned long)s_count, (unsigned long)s_efsRecBase);
            return;
        }

        s_useFlashStorage = 1;
        Backup_FlushHeader();
        HAL_LOG("[Backup] Use SPI Flash empty");
        return;
    }

    s_useFlashStorage = 0;
    if (Backup_LoadHeaderFromEfs(&efs, &efsBase)) {
        s_head = efs.head;
        s_tail = efs.tail;
        s_count = efs.count;
        s_efsRecBase = efsBase;
    }
    HAL_LOG("[Backup] Use EFS (no SPI flash, count=%lu)", (unsigned long)s_count);
}

void Backup_Init(void)
{
    if (s_inited) return;

    if (s_bakMutex == NULL) {
        HAL_OS_MutexCreate(&s_bakMutex);
    }

    Backup_SelectStorageEngine();
    s_inited = 1;
    HAL_LOG("[Backup] init count=%lu head=%lu tail=%lu recSeq=%lu (FlashStorage=%d)",
            (unsigned long)s_count, (unsigned long)s_head, (unsigned long)s_tail,
            (unsigned long)s_recSeq, s_useFlashStorage);
}

/** Ghi 1 ban ghi vao che do luu dang dung. Goi khi da giu khoa. */
static void Backup_PushLocked(const BackupRecord_t *rec)
{
    if (s_useFlashStorage) {
        Backup_PushToFlash(rec);
        if (++s_pushSinceFlush >= BACKUP_HDR_FLUSH_PUSHES) {
            Backup_FlushHeader();
        }
    } else {
        Backup_PushToEfs(rec);
        /* EFS: luu header ngay de mat dien khong lam mat ban ghi vua ghi */
        Backup_FlushHeader();
    }
}

void Backup_Push(const BackupRecord_t *rec)
{
    if (!rec) return;

    BackupLock();
    Backup_Init();
    Backup_PushLocked(rec);
    /* Vua luu 1 vi tri: Backup_OnTick khong luu trung ngay sau do */
    s_lastStoreTick = GetTickNow();
    if (s_lastStoreTick == 0) s_lastStoreTick = 1;
    BackupUnlock();
}

int Backup_PushText(const char *text)
{
    uint32_t len;
    uint32_t parts;
    uint32_t i;

    if (!text) return -1;
    len = (uint32_t)strlen(text);
    if (len == 0 || len > BACKUP_TEXT_MAX) return -1;
    parts = (len + TEXT_CHUNK_DATA - 1) / TEXT_CHUNK_DATA;

    BackupLock();
    Backup_Init();
    for (i = 0; i < parts; i++) {
        BackupRecord_t rec;
        TextChunk_t ck;
        uint32_t n = len - i * TEXT_CHUNK_DATA;
        if (n > TEXT_CHUNK_DATA) n = TEXT_CHUNK_DATA;

        memset(&ck, 0, sizeof(ck));
        ck.part = (uint8_t)i;
        ck.parts = (uint8_t)parts;
        ck.len = (uint8_t)n;
        memcpy(ck.text, text + i * TEXT_CHUNK_DATA, n);

        memset(&rec, 0, sizeof(rec));
        memcpy(&rec, &ck, sizeof(ck));
        rec.used = BACKUP_KIND_TEXT;
        Backup_PushLocked(&rec);
    }
    BackupUnlock();
    return 0;
}

/**
 * Doc ban ghi cach tail `offset` o. Goi khi da giu khoa.
 * @return 1: hop le; 0: o hong (lo trong); -1: loi doc
 */
static int Backup_ReadAt(uint32_t offset, BackupRecord_t *out)
{
    if (s_useFlashStorage) {
        FlashSlot_t slot;
        if (ReadSlot(s_tail + offset, &slot, NULL) != 0) return -1;
        if (!SlotValid(&slot)) return 0;
        memcpy(out, &slot.rec, sizeof(BackupRecord_t));
        return 1;
    } else {
        HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "rb");
        uint32_t idx = (s_tail + offset) % NASA_BACKUP_MAX_RECORDS;
        long off = (long)(s_efsRecBase + idx * sizeof(BackupRecord_t));
        int ok;

        if (!fp) return -1;
        ok = (HAL_FS_Seek(fp, off, HAL_FS_SEEK_SET) == 0 &&
              HAL_FS_Read(fp, out, sizeof(BackupRecord_t)) == (int)sizeof(BackupRecord_t));
        HAL_FS_Close(fp);
        return ok ? 1 : -1;
    }
}

int Backup_PeekText(char *out, uint32_t outSize, int *slots)
{
    uint32_t off = 0;
    uint32_t pos = 0;
    uint32_t got = 0;
    uint32_t parts = 0;
    int res = 0;

    if (!out || outSize == 0 || !slots) return -1;
    *slots = 0;

    BackupLock();
    Backup_Init();
    while (off < s_count && off < TEXT_PEEK_MAX_SLOTS) {
        BackupRecord_t rec;
        TextChunk_t ck;
        int r = Backup_ReadAt(off, &rec);

        if (r < 0) { res = -1; break; }
        if (r == 0) { off++; continue; }            /* o hong nam giua cac manh */
        if (rec.used != BACKUP_KIND_TEXT) break;    /* chuoi manh bi cat ngang */

        memcpy(&ck, &rec, sizeof(ck));
        if (got == 0) {
            if (ck.part != 0 || ck.parts == 0 || ck.parts > TEXT_MAX_PARTS) { off++; break; }
            parts = ck.parts;
        } else if (ck.part != got || ck.parts != parts) {
            break;                                  /* manh cua khung khac -> bo phan da gom */
        }
        if (ck.len > TEXT_CHUNK_DATA || pos + ck.len >= outSize) { off++; break; }

        memcpy(out + pos, ck.text, ck.len);
        pos += ck.len;
        got++;
        off++;
        if (got == parts) { res = 1; break; }
    }
    BackupUnlock();

    if (res < 0) return -1;
    out[pos] = '\0';
    *slots = (off > 0) ? (int)off : 1;
    return res;
}

int Backup_Peek(BackupRecord_t *out)
{
    int res = 0;
    if (!out) return 0;

    BackupLock();
    Backup_Init();

    if (s_count == 0) {
        BackupUnlock();
        return 0;
    }

    if (s_useFlashStorage) {
        int skip;
        /* O khong qua duoc CRC (ghi do, du lieu cu) bi bo qua, khong phat len Server */
        for (skip = 0; skip < BACKUP_PEEK_MAX_SKIP && s_count > 0; skip++) {
            FlashSlot_t slot;
            if (ReadSlot(s_tail, &slot, NULL) != 0) break;
            if (SlotValid(&slot)) {
                memcpy(out, &slot.rec, sizeof(BackupRecord_t));
                res = 1;
                break;
            }
            s_tail = (s_tail + 1) % FLASH_MAX_RECORDS;
            s_count--;
            s_hdrDirty = 1;
        }
    } else {
        HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "rb");
        long offset;
        BackupRecord_t rec;

        if (!fp) {
            BackupUnlock();
            return 0;
        }

        offset = (long)(s_efsRecBase + s_tail * sizeof(BackupRecord_t));
        if (HAL_FS_Seek(fp, offset, HAL_FS_SEEK_SET) != 0) {
            HAL_FS_Close(fp);
            BackupUnlock();
            return 0;
        }

        if (HAL_FS_Read(fp, &rec, sizeof(BackupRecord_t)) != (int)sizeof(BackupRecord_t)) {
            HAL_FS_Close(fp);
            BackupUnlock();
            return 0;
        }

        HAL_FS_Close(fp);
        memcpy(out, &rec, sizeof(BackupRecord_t));
        res = 1;
    }
    BackupUnlock();
    return res;
}

int Backup_CommitPopN(int n)
{
    BackupLock();
    while (n-- > 0 && s_count > 0) {
        uint32_t maxRecs = s_useFlashStorage ? FLASH_MAX_RECORDS : NASA_BACKUP_MAX_RECORDS;
        s_tail = (s_tail + 1) % maxRecs;
        s_count--;
        s_hdrDirty = 1;
    }
    BackupUnlock();
    return 0;
}

int Backup_CommitPop(void)
{
    return Backup_CommitPopN(1);
}

int Backup_Count(void)
{
    int c;
    BackupLock();
    Backup_Init();
    c = (int)s_count;
    BackupUnlock();
    return c;
}

void Backup_GetStatus(int *useFlash, int *count, uint32_t *writeErrors)
{
    BackupLock();
    Backup_Init();
    if (useFlash) *useFlash = s_useFlashStorage;
    if (count) *count = (int)s_count;
    if (writeErrors) *writeErrors = s_writeErrCount;
    BackupUnlock();
}

void Backup_Flush(void)
{
    BackupLock();
    Backup_FlushHeader();
    BackupUnlock();
}

void Backup_OnTick(const BackupRecord_t *rec, uint32_t nowTick)
{
    int isMoving = GPS_IsMoving();
    uint32_t periodSec = isMoving ? CFG_GetPeriodMoving() : CFG_GetPeriodStopped();
    uint32_t periodTicks = periodSec * HAL_TICKS_PER_SEC;

    if (rec == NULL) return;

    /* Lan goi dau sau khi khoi dong chi dat moc: khong luu ban ghi thua trong
     * luc ket noi binh thuong, ban ghi dau tien duoc luu sau 1 chu ky. */
    if (s_lastStoreTick == 0) {
        s_lastStoreTick = (nowTick != 0) ? nowTick : 1;
        return;
    }

    if ((nowTick - s_lastStoreTick) >= periodTicks) {
        Backup_Push(rec);
    }
}

void Backup_Clear(void)
{
    BackupLock();
    Backup_Init();
    if (s_useFlashStorage) {
        /* Giu nguyen head va recSeq: ban ghi cu phia sau khong bi nhan lai */
        s_tail = s_head;
        s_count = 0;
        s_hdrDirty = 1;
        Backup_FlushHeader();
    } else {
        s_head = 0;
        s_tail = 0;
        s_count = 0;
        s_hdrDirty = 0;
        HAL_FS_Delete(NASA_BACKUP_FILE);
    }
    BackupUnlock();
}
