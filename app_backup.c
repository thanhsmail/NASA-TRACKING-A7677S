/**
 * @file app_backup.c
 * @brief Offline Backup Storage module -- NASA Tracking
 *
 * Luu y: File nay KHONG #include simcom_api.h truc tiep.
 * Moi tuong tac xuyet suot QOS / File / Mutex qua HAL layer.
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
#include <stdio.h>
#include <string.h>

#define BACKUP_HDR_MAGIC        0x52323643u /* 'R26C' -- v2: header journal + seq */
#define BACKUP_HDR_MAGIC_LEGACY 0x52323642u /* 'R26B' -- v1: khong co seq */
#define FLASH_HDR_SECTOR        2           /* Sector 2: Address 0x002000 */
#define FLASH_LOG_START_ADDR    (3 * EN25_SECTOR_SIZE) /* Address 0x003000 */
#define FLASH_LOG_END_ADDR      EN25_FLASH_SIZE        /* 8MB Address 0x800000 */
#define FLASH_RECORD_SIZE       128         /* Size per record padded */
#define FLASH_MAX_RECORDS       ((FLASH_LOG_END_ADDR - FLASH_LOG_START_ADDR) / FLASH_RECORD_SIZE)
#define FLASH_RECS_PER_SECTOR   (EN25_SECTOR_SIZE / FLASH_RECORD_SIZE) /* 32 */

#define FLASH_HDR_SLOT_SIZE     32
#define FLASH_HDR_SLOTS         (EN25_SECTOR_SIZE / FLASH_HDR_SLOT_SIZE) /* 128 */

#define EFS_HDR_SIZE            ((uint32_t)sizeof(BackupHdr_t))

typedef struct {
    uint32_t magic;
    uint32_t head;   /* write index */
    uint32_t tail;   /* read index */
    uint32_t count;
    uint32_t seq;    /* so thu tu journal */
    uint32_t crc32;  /* checksum */
} BackupHdr_t;

typedef char BackupHdrFitsSlot[(sizeof(BackupHdr_t) <= FLASH_HDR_SLOT_SIZE) ? 1 : -1];
typedef char BackupRecFits128[(sizeof(BackupRecord_t) <= FLASH_RECORD_SIZE) ? 1 : -1];

static BackupHdr_t s_hdr;
static uint32_t    s_lastStoreTick = 0;
static int         s_inited = 0;
static int         s_hdrDirty = 0;
static int         s_useFlashStorage = 0;
static uint32_t    s_hdrSlot = 0;
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

static uint32_t FlashRecordAddr(uint32_t index)
{
    return FLASH_LOG_START_ADDR + (index % FLASH_MAX_RECORDS) * FLASH_RECORD_SIZE;
}

static int Backup_LoadHeaderFromFlash(void)
{
    uint32_t slot;
    uint32_t bestSeq = 0;
    int foundBest = 0;
    BackupHdr_t h;

    for (slot = 0; slot < FLASH_HDR_SLOTS; slot++) {
        uint32_t addr = FLASH_HDR_SECTOR * EN25_SECTOR_SIZE + slot * FLASH_HDR_SLOT_SIZE;
        if (EN25_Read(addr, (uint8_t *)&h, sizeof(h)) != 0) continue;

        if (h.magic == BACKUP_HDR_MAGIC) {
            uint32_t crcCalc = Buffer_GetChecksum((const uint8_t *)&h, 20);
            if (crcCalc == h.crc32 && h.count <= FLASH_MAX_RECORDS) {
                if (!foundBest || h.seq > bestSeq || (bestSeq > 0xFFFFFF00u && h.seq < 0x000000FFu)) {
                    s_hdr = h;
                    bestSeq = h.seq;
                    s_hdrSlot = slot;
                    foundBest = 1;
                }
            }
        }
    }

    if (foundBest) {
        HAL_LOG("[Backup] Loaded Header from SPI Flash OK (count=%lu, seq=%lu, slot=%lu)",
                (unsigned long)s_hdr.count, (unsigned long)s_hdr.seq, (unsigned long)s_hdrSlot);
        return 0;
    }
    return -1;
}

static int Backup_SaveHeaderToFlash(void)
{
    BackupHdr_t h = s_hdr;
    uint32_t nextSlot = (s_hdrSlot + 1) % FLASH_HDR_SLOTS;
    uint32_t addr;

    h.magic = BACKUP_HDR_MAGIC;
    h.seq++;
    h.crc32 = Buffer_GetChecksum((const uint8_t *)&h, 20);

    if (nextSlot == 0) {
        if (EN25_EraseSector(FLASH_HDR_SECTOR * EN25_SECTOR_SIZE) != 0) return -1;
    }

    addr = FLASH_HDR_SECTOR * EN25_SECTOR_SIZE + nextSlot * FLASH_HDR_SLOT_SIZE;
    if (EN25_Write(addr, (const uint8_t *)&h, sizeof(h)) != 0) return -1;

    s_hdrSlot = nextSlot;
    s_hdr = h;
    return 0;
}

static void Backup_LoadHeaderFromEfs(void)
{
    HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "rb");
    BackupHdr_t h;
    if (!fp) return;

    if (HAL_FS_Read(fp, &h, sizeof(h)) < 20) {
        HAL_FS_Close(fp);
        return;
    }
    HAL_FS_Close(fp);

    if (h.magic != BACKUP_HDR_MAGIC && h.magic != BACKUP_HDR_MAGIC_LEGACY) {
        HAL_LOG("[Backup] EFS magic invalid 0x%08lX -- ignore file", (unsigned long)h.magic);
        return;
    }

    if (h.count > NASA_BACKUP_MAX_RECORDS) {
        HAL_LOG("[Backup] EFS header out of range count=%lu -- ignore", (unsigned long)h.count);
        return;
    }

    s_hdr = h;
    s_hdr.magic = BACKUP_HDR_MAGIC;
    s_efsRecBase = (h.magic == BACKUP_HDR_MAGIC_LEGACY) ? 20u : EFS_HDR_SIZE;
    HAL_LOG("[Backup] EFS loaded count=%lu head=%lu tail=%lu base=%lu",
            (unsigned long)s_hdr.count, (unsigned long)s_hdr.head,
            (unsigned long)s_hdr.tail, (unsigned long)s_efsRecBase);
}

static void Backup_SaveHeaderToEfs(void)
{
    HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "wb");
    BackupHdr_t hdrCopy;
    uint8_t v1[20];

    if (!fp) return;

    hdrCopy = s_hdr;
    hdrCopy.crc32 = Buffer_GetChecksum((const uint8_t *)&hdrCopy, 20);

    if (s_efsRecBase == 20u) {
        memcpy(v1, &hdrCopy, 20);
        if (HAL_FS_Write(fp, v1, sizeof(v1)) != (int)sizeof(v1)) {
            HAL_FS_Close(fp);
            return;
        }
    } else {
        if (HAL_FS_Write(fp, &hdrCopy, sizeof(hdrCopy)) != (int)sizeof(hdrCopy)) {
            HAL_FS_Close(fp);
            return;
        }
    }
    HAL_FS_Close(fp);
}

static void Backup_FlushHeader(void)
{
    if (!s_hdrDirty) return;
    if (s_useFlashStorage) {
        if (Backup_SaveHeaderToFlash() == 0) s_hdrDirty = 0;
    } else {
        Backup_SaveHeaderToEfs();
        s_hdrDirty = 0;
    }
}

static void Backup_SelectStorageEngine(void)
{
    memset(&s_hdr, 0, sizeof(s_hdr));

    if (EN25_IsReady()) {
        int attempt;
        for (attempt = 0; attempt < 3; attempt++) {
            if (Backup_LoadHeaderFromFlash() == 0) {
                int flashCount = (int)s_hdr.count;
                if (flashCount > 0) {
                    s_useFlashStorage = 1;
                    HAL_LOG("[Backup] Use SPI Flash (count=%d)", flashCount);
                    return;
                }
                break;
            }
            HAL_OS_TaskSleep(HAL_TICKS_PER_SEC / 5);
        }

        Backup_LoadHeaderFromEfs();
        if (s_hdr.count > 0) {
            s_useFlashStorage = 0;
            HAL_LOG("[Backup] Use EFS file (count=%d, recBase=%lu) -- flash empty",
                    (int)s_hdr.count, (unsigned long)s_efsRecBase);
            return;
        }

        s_useFlashStorage = 1;
        s_hdr.magic = BACKUP_HDR_MAGIC;
        s_hdr.head = 0;
        s_hdr.tail = 0;
        s_hdr.count = 0;
        s_hdr.seq = 0;
        HAL_LOG("[Backup] Use SPI Flash empty");
        return;
    }

    s_useFlashStorage = 0;
    Backup_LoadHeaderFromEfs();
    HAL_LOG("[Backup] Use EFS empty (no SPI flash)");
}

void Backup_Init(void)
{
    if (s_inited) return;

    if (s_bakMutex == NULL) {
        HAL_OS_MutexCreate(&s_bakMutex);
    }

    Backup_SelectStorageEngine();
    s_inited = 1;
    HAL_LOG("[Backup] init count=%lu (FlashStorage=%d)", (unsigned long)s_hdr.count, s_useFlashStorage);
}

void Backup_Push(const BackupRecord_t *rec)
{
    BackupRecord_t tmp;
    if (!rec) return;

    BackupLock();
    Backup_Init();

    if (s_useFlashStorage) {
        uint32_t addr = FlashRecordAddr(s_hdr.head);
        uint32_t sectorAddr = addr & ~(EN25_SECTOR_SIZE - 1);

        if (s_hdr.count >= FLASH_MAX_RECORDS) {
            s_hdr.tail = (s_hdr.tail + 1) % FLASH_MAX_RECORDS;
            s_hdr.count = FLASH_MAX_RECORDS - 1;
        }

        if ((addr % EN25_SECTOR_SIZE) == 0) {
            if (EN25_EraseSector(sectorAddr) != 0) {
                HAL_LOG("[Backup] EraseSector 0x%06X fail, skip push", sectorAddr);
                BackupUnlock();
                return;
            }
        }

        memset(&tmp, 0, sizeof(tmp));
        tmp = *rec;
        if (EN25_Write(addr, (const uint8_t *)&tmp, sizeof(tmp)) != 0) {
            HAL_LOG("[Backup] Write 0x%06X fail, skip push", addr);
            BackupUnlock();
            return;
        }

        s_hdr.head = (s_hdr.head + 1) % FLASH_MAX_RECORDS;
        s_hdr.count++;
        s_hdrDirty = 1;
        HAL_LOG("[Backup/Push] offline log -> SPI Flash (addr=0x%06X, head=%lu, count=%lu/%lu, dt=%s)",
                addr, (unsigned long)s_hdr.head, (unsigned long)s_hdr.count,
                (unsigned long)FLASH_MAX_RECORDS, rec->datetime);
    } else {
        HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "wb");
        long offset;

        if (s_hdr.count >= NASA_BACKUP_MAX_RECORDS) {
            s_hdr.tail = (s_hdr.tail + 1) % NASA_BACKUP_MAX_RECORDS;
            s_hdr.count = NASA_BACKUP_MAX_RECORDS - 1;
        }

        if (!fp) {
            HAL_LOG("[Backup] EFS open fail, skip push");
            BackupUnlock();
            return;
        }

        offset = (long)(s_efsRecBase + s_hdr.head * sizeof(BackupRecord_t));
        if (HAL_FS_Seek(fp, offset, HAL_FS_SEEK_SET) != 0) {
            HAL_FS_Close(fp);
            HAL_LOG("[Backup] EFS seek fail off=%ld head=%lu", offset, (unsigned long)s_hdr.head);
            BackupUnlock();
            return;
        }

        if (HAL_FS_Write(fp, rec, sizeof(BackupRecord_t)) != (int)sizeof(BackupRecord_t)) {
            HAL_FS_Close(fp);
            HAL_LOG("[Backup] EFS write record fail off=%ld", offset);
            BackupUnlock();
            return;
        }

        HAL_FS_Close(fp);
        s_hdr.head = (s_hdr.head + 1) % NASA_BACKUP_MAX_RECORDS;
        s_hdr.count++;
        s_hdrDirty = 1;
    }
    BackupUnlock();
}

int Backup_Peek(BackupRecord_t *out)
{
    int res = 0;
    if (!out) return 0;

    BackupLock();
    Backup_Init();

    if (s_hdr.count == 0) {
        BackupUnlock();
        return 0;
    }

    if (s_useFlashStorage) {
        uint32_t addr = FlashRecordAddr(s_hdr.tail);
        if (EN25_Read(addr, (uint8_t *)out, sizeof(BackupRecord_t)) == 0) {
            res = 1;
        }
    } else {
        HalFile_t fp = HAL_FS_Open(NASA_BACKUP_FILE, "rb");
        long offset;
        BackupRecord_t rec;

        if (!fp) {
            BackupUnlock();
            return 0;
        }

        offset = (long)(s_efsRecBase + s_hdr.tail * sizeof(BackupRecord_t));
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

int Backup_CommitPop(void)
{
    BackupLock();
    if (s_hdr.count > 0) {
        uint32_t maxRecs = s_useFlashStorage ? FLASH_MAX_RECORDS : NASA_BACKUP_MAX_RECORDS;
        s_hdr.tail = (s_hdr.tail + 1) % maxRecs;
        s_hdr.count--;
        s_hdrDirty = 1;
    }
    BackupUnlock();
    return 0;
}

int Backup_Count(void)
{
    int c;
    BackupLock();
    Backup_Init();
    c = (int)s_hdr.count;
    BackupUnlock();
    return c;
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

    if (s_lastStoreTick == 0 || (nowTick - s_lastStoreTick) >= periodTicks) {
        s_lastStoreTick = nowTick;
        Backup_Push(rec);
    }
}

void Backup_Clear(void)
{
    BackupLock();
    s_hdr.head = 0;
    s_hdr.tail = 0;
    s_hdr.count = 0;
    s_hdrDirty = 1;
    Backup_FlushHeader();

    if (!s_useFlashStorage) {
        HAL_FS_Delete(NASA_BACKUP_FILE);
    }
    BackupUnlock();
}
