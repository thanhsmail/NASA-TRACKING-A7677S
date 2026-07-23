#include "simcom_api.h"
#include "app_config.h"
#include "app_backup.h"
#include "drv_en25qh64a.h"
#include "app_utils.h"
#include <stdio.h>
#include <string.h>

#define BACKUP_HDR_MAGIC        0x52323643u /* 'R26C' — v2: header journal + seq */
#define BACKUP_HDR_MAGIC_LEGACY 0x52323642u /* 'R26B' — v1: không có seq */
#define FLASH_HDR_SECTOR        2           /* Sector 2: Address 0x002000 */
#define FLASH_LOG_START_ADDR    (3 * EN25_SECTOR_SIZE) /* Address 0x003000 */
#define FLASH_LOG_END_ADDR      EN25_FLASH_SIZE        /* 8MB Address 0x800000 */
#define FLASH_RECORD_SIZE       128         /* Size per record padded */
#define FLASH_MAX_RECORDS       ((FLASH_LOG_END_ADDR - FLASH_LOG_START_ADDR) / FLASH_RECORD_SIZE)
#define FLASH_RECS_PER_SECTOR   (EN25_SECTOR_SIZE / FLASH_RECORD_SIZE) /* 32 */

/*
 * Header ghi kiểu journal trong sector 2: mỗi lần lưu ghi vào 1 slot 32 byte
 * kế tiếp (NOR flash ghi đè bit 1->0 không cần xoá), chỉ xoá sector khi hết
 * 128 slot. Giảm số chu kỳ xoá sector header đi 128 lần — nếu xoá mỗi lần
 * lưu như trước, push 10s/lần sẽ đốt hết 100K chu kỳ xoá của NOR chỉ sau
 * ~12 ngày. Khi load: quét toàn bộ slot, lấy header hợp lệ có seq lớn nhất.
 */
#define FLASH_HDR_SLOT_SIZE     32
#define FLASH_HDR_SLOTS         (EN25_SECTOR_SIZE / FLASH_HDR_SLOT_SIZE) /* 128 */

/* EFS dùng sizeof(BackupHdr_t) làm base — giữ tương thích file đã ghi */
#define EFS_HDR_SIZE            ((uint32_t)sizeof(BackupHdr_t))

typedef struct {
    uint32_t magic;
    uint32_t head;   /* write index */
    uint32_t tail;   /* read index */
    uint32_t count;
    uint32_t seq;    /* số thứ tự journal, tăng dần mỗi lần lưu */
    uint32_t crc32;  /* checksum của 5 field trên */
} BackupHdr_t;

/* Guard kích thước khi đổi struct: header phải lọt slot 32B, record lọt 128B */
typedef char BackupHdrFitsSlot[(sizeof(BackupHdr_t) <= FLASH_HDR_SLOT_SIZE) ? 1 : -1];
typedef char BackupRecFits128[(sizeof(BackupRecord_t) <= FLASH_RECORD_SIZE) ? 1 : -1];

static BackupHdr_t s_hdr;
static uint32_t    s_lastStoreTick = 0;
static int         s_inited = 0;
static int         s_hdrDirty = 0;
static int         s_useFlashStorage = 0;
static uint32_t    s_hdrSlot = 0; /* slot journal kế tiếp sẽ ghi trong sector 2 */
static uint32_t    s_efsRecBase = EFS_HDR_SIZE; /* offset bắt đầu vùng record trên EFS */

static sMutexRef   s_bakMutex = NULL;

static void BackupLock(void)
{
    if (s_bakMutex) sAPI_MutexLock(s_bakMutex, SC_SUSPEND);
}

static void BackupUnlock(void)
{
    if (s_bakMutex) sAPI_MutexUnLock(s_bakMutex);
}

static int Backup_SaveHdrFlash(void)
{
    uint8_t slotBuf[FLASH_HDR_SLOT_SIZE];
    BackupHdr_t hdrCopy;

    if (s_hdrSlot >= FLASH_HDR_SLOTS) {
        if (EN25_EraseSector(FLASH_HDR_SECTOR * EN25_SECTOR_SIZE) != 0) return -1;
        s_hdrSlot = 0;
    }

    s_hdr.seq++;
    hdrCopy = s_hdr;
    hdrCopy.magic = BACKUP_HDR_MAGIC;
    hdrCopy.crc32 = Buffer_GetChecksum((const uint8_t *)&hdrCopy, sizeof(uint32_t) * 5);

    memset(slotBuf, 0xFF, sizeof(slotBuf));
    memcpy(slotBuf, &hdrCopy, sizeof(hdrCopy));

    if (EN25_Write(FLASH_HDR_SECTOR * EN25_SECTOR_SIZE + s_hdrSlot * FLASH_HDR_SLOT_SIZE,
                   slotBuf, FLASH_HDR_SLOT_SIZE) != 0) {
        return -1;
    }
    s_hdrSlot++;
    return 0;
}

static int SlotIsBlank(const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        if (buf[i] != 0xFF) return 0;
    }
    return 1;
}

/*
 * Đọc header từ sector journal trên SPI flash. Trả 0 nếu flash dùng được
 * (kể cả chip mới chưa format — sẽ tự format lần đầu), -1 nếu không có flash.
 */
static int Backup_LoadHdrFlash(void)
{
    BackupHdr_t best;
    int foundValid = 0;
    int lastUsedSlot = -1;
    uint32_t slot;

    for (slot = 0; slot < FLASH_HDR_SLOTS; slot++) {
        uint8_t slotBuf[FLASH_HDR_SLOT_SIZE];
        BackupHdr_t h;

        if (EN25_Read(FLASH_HDR_SECTOR * EN25_SECTOR_SIZE + slot * FLASH_HDR_SLOT_SIZE,
                      slotBuf, sizeof(slotBuf)) != 0) {
            return -1; /* không đọc được SPI flash (chip rời/không init) */
        }
        if (SlotIsBlank(slotBuf, sizeof(slotBuf))) continue;
        lastUsedSlot = (int)slot;

        memcpy(&h, slotBuf, sizeof(h));
        if (h.magic != BACKUP_HDR_MAGIC) continue;
        if (Buffer_GetChecksum((const uint8_t *)&h, sizeof(uint32_t) * 5) != h.crc32) continue;
        if (h.count > FLASH_MAX_RECORDS ||
            h.head >= FLASH_MAX_RECORDS || h.tail >= FLASH_MAX_RECORDS) continue;

        if (!foundValid || (int32_t)(h.seq - best.seq) > 0) {
            best = h;
            foundValid = 1;
        }
    }

    if (foundValid) {
        s_hdr = best;
        s_hdrSlot = (uint32_t)(lastUsedSlot + 1); /* ghi tiếp sau slot dùng cuối */
        s_useFlashStorage = 1;
        s_hdrDirty = 0;
        sAPI_Debug("[Backup] Loaded Header from SPI Flash OK (count=%lu, seq=%lu, slot=%lu)",
                   (unsigned long)s_hdr.count, (unsigned long)s_hdr.seq, (unsigned long)s_hdrSlot);
        return 0;
    }

    /* Chip đọc được nhưng chưa có header hợp lệ: format lần đầu (chip mới
     * toàn 0xFF, hoặc dữ liệu rác/format cũ) rồi dùng flash làm kho chính */
    if (lastUsedSlot >= 0) {
        if (EN25_EraseSector(FLASH_HDR_SECTOR * EN25_SECTOR_SIZE) != 0) return -1;
    }
    s_hdr.magic = BACKUP_HDR_MAGIC;
    s_hdr.head = s_hdr.tail = s_hdr.count = 0;
    s_hdr.seq = 0;
    s_hdrSlot = 0;
    s_useFlashStorage = 1;
    if (Backup_SaveHdrFlash() != 0) {
        s_useFlashStorage = 0;
        return -1;
    }
    s_hdrDirty = 0;
    sAPI_Debug("[Backup] SPI Flash formatted for backup storage");
    return 0;
}

/*
 * Đọc header EFS vào out. Trả 1 = file hợp lệ, 0 = không có/không dùng được.
 */
static int Backup_TryLoadEfs(BackupHdr_t *out, uint32_t *recBaseOut)
{
    SCFILE *fp;
    BackupHdr_t h;
    uint32_t recBase = EFS_HDR_SIZE;

    if (!out) return 0;

    fp = sAPI_fopen(NASA_BACKUP_FILE, "rb");
    if (!fp) return 0;

    memset(&h, 0, sizeof(h));
    if (sAPI_fread(&h, 1, sizeof(h), fp) < 20) {
        sAPI_fclose(fp);
        return 0;
    }
    sAPI_fclose(fp);

    if (h.magic == BACKUP_HDR_MAGIC_LEGACY) {
        /* v1: magic,head,tail,count,crc — không có seq; record base = 20 */
        h.seq = 0;
        recBase = 20;
    } else if (h.magic == BACKUP_HDR_MAGIC) {
        recBase = EFS_HDR_SIZE; /* sizeof(BackupHdr_t) == 24 */
    } else {
        sAPI_Debug("[Backup] EFS magic invalid 0x%08lX — ignore file",
                   (unsigned long)h.magic);
        return 0;
    }

    if (h.count > NASA_BACKUP_MAX_RECORDS ||
        h.head >= NASA_BACKUP_MAX_RECORDS ||
        h.tail >= NASA_BACKUP_MAX_RECORDS) {
        sAPI_Debug("[Backup] EFS header out of range count=%lu — ignore",
                   (unsigned long)h.count);
        return 0;
    }

    *out = h;
    out->magic = BACKUP_HDR_MAGIC;
    if (recBaseOut) *recBaseOut = recBase;
    sAPI_Debug("[Backup] EFS loaded count=%lu head=%lu tail=%lu base=%lu",
               (unsigned long)h.count, (unsigned long)h.head,
               (unsigned long)h.tail, (unsigned long)recBase);
    return 1;
}

static int Backup_SaveHdrEfs(void)
{
    SCFILE *fp = sAPI_fopen(NASA_BACKUP_FILE, "rb+");
    if (!fp) {
        fp = sAPI_fopen(NASA_BACKUP_FILE, "wb");
        if (!fp) return -1;
        s_efsRecBase = EFS_HDR_SIZE;
    }

    sAPI_fseek(fp, 0, FS_SEEK_BEGIN);

    if (s_efsRecBase == 20) {
        /* Giữ layout v1 để không đè 4 byte đầu bản ghi đầu tiên */
        uint32_t v1[5];
        v1[0] = BACKUP_HDR_MAGIC_LEGACY;
        v1[1] = s_hdr.head;
        v1[2] = s_hdr.tail;
        v1[3] = s_hdr.count;
        v1[4] = Buffer_GetChecksum((const uint8_t *)v1, sizeof(uint32_t) * 4);
        /* sAPI_fwrite trả về số BYTE đã ghi, không phải num như fwrite chuẩn */
        if (sAPI_fwrite(v1, 1, sizeof(v1), fp) != (int)sizeof(v1)) {
            sAPI_fclose(fp);
            return -1;
        }
    } else {
        BackupHdr_t hdrCopy = s_hdr;
        hdrCopy.magic = BACKUP_HDR_MAGIC;
        hdrCopy.crc32 = Buffer_GetChecksum((const uint8_t *)&hdrCopy, sizeof(uint32_t) * 5);
        if (sAPI_fwrite(&hdrCopy, 1, sizeof(hdrCopy), fp) != (int)sizeof(hdrCopy)) {
            sAPI_fclose(fp);
            return -1;
        }
    }
    sAPI_fsync(fp);
    sAPI_fclose(fp);
    return 0;
}

static int Backup_LoadHdr(void)
{
    BackupHdr_t flashHdr;
    BackupHdr_t efsHdr;
    uint32_t efsRecBase = EFS_HDR_SIZE;
    int flashOk = 0;
    int efsStat = 0;
    int flashCount = 0;
    int efsCount = 0;
    int try;

    /* Flash: thử load (0 = dùng được, kể cả count=0 sau format) */
    if (Backup_LoadHdrFlash() == 0) {
        flashOk = 1;
        flashHdr = s_hdr;
        flashCount = (int)s_hdr.count;
    }

    /* EFS: retry vài lần — lúc boot FS đôi khi chưa mount xong */
    efsStat = 0;
    for (try = 0; try < 5; try++) {
        efsStat = Backup_TryLoadEfs(&efsHdr, &efsRecBase);
        if (efsStat == 1) break;
        sAPI_TaskSleep(SC_TICKS_PER_SECOND / 5); /* 200ms */
    }
    if (efsStat == 1) efsCount = (int)efsHdr.count;

    /*
     * Chọn kho có dữ liệu thật. Lỗi cũ: flash trống (mới format) luôn thắng
     * → bỏ quên toàn bộ backup trên C:/rv26_bak.dat sau mất nguồn/reboot.
     */
    if (flashOk && flashCount > 0) {
        s_hdr = flashHdr;
        s_useFlashStorage = 1;
        s_hdrDirty = 0;
        sAPI_Debug("[Backup] Use SPI Flash (count=%d)", flashCount);
        return 0;
    }

    if (efsStat == 1 && efsCount > 0) {
        s_hdr = efsHdr;
        s_efsRecBase = efsRecBase;
        s_useFlashStorage = 0;
        s_hdrDirty = 0;
        sAPI_Debug("[Backup] Use EFS file (count=%d, recBase=%lu) — flash empty or absent",
                   efsCount, (unsigned long)s_efsRecBase);
        /* Nếu flash đang trống có sẵn: lần save sau vẫn EFS cho hết hàng đợi;
         * không xoá EFS ở đây. */
        return 0;
    }

    if (flashOk) {
        /* Flash trống, EFS trống/không có → dùng flash */
        s_hdr = flashHdr;
        s_useFlashStorage = 1;
        s_hdrDirty = 0;
        sAPI_Debug("[Backup] Use SPI Flash empty");
        return 0;
    }

    /* Không có flash: EFS trống hoặc chưa có file */
    s_hdr.magic = BACKUP_HDR_MAGIC;
    s_hdr.head = s_hdr.tail = s_hdr.count = 0;
    s_hdr.seq = 0;
    s_efsRecBase = EFS_HDR_SIZE;
    s_useFlashStorage = 0;
    s_hdrDirty = 0;
    sAPI_Debug("[Backup] Use EFS empty (no SPI flash)");
    return 0;
}

static int Backup_SaveHdr(int force)
{
    if (!s_hdrDirty && !force) return 0;

    if (s_useFlashStorage) {
        int r = Backup_SaveHdrFlash();
        if (r == 0) s_hdrDirty = 0;
        return r;
    }

    if (Backup_SaveHdrEfs() != 0) return -1;
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
    sAPI_Debug("[Backup] init count=%lu (FlashStorage=%d)", (unsigned long)s_hdr.count, s_useFlashStorage);
}

static void Backup_PushLocked(const BackupRecord_t *rec)
{
    BackupRecord_t tmp;
    uint32_t maxLimit = s_useFlashStorage ? FLASH_MAX_RECORDS : NASA_BACKUP_MAX_RECORDS;

    /*
     * Không chuyển Flash↔EFS giữa chừng: head/tail/count của 2 kho khác nhau.
     * Đổi kho giữa Push sẽ làm lệch chỉ số → mất/ghi đè bản ghi.
     * Flash hỏng thì bỏ qua lần push này (EFS chỉ dùng khi Init không có flash).
     */

    if (s_hdr.count >= maxLimit) {
        s_hdr.tail = (s_hdr.tail + 1) % maxLimit;
        s_hdr.count--;
    }

    tmp = *rec;
    tmp.used = 1;

    if (s_useFlashStorage) {
        uint32_t addr = FLASH_LOG_START_ADDR + (s_hdr.head * FLASH_RECORD_SIZE);
        uint32_t sectorAddr = addr & ~(EN25_SECTOR_SIZE - 1);

        /* Nếu ghi ở đầu Sector 4KB, tiến hành Xóa Sector trước khi ghi đè */
        if ((addr % EN25_SECTOR_SIZE) == 0) {
            /* Ring đầy quay vòng: nếu tail còn nằm trong sector sắp xoá thì
             * loại các bản ghi đó khỏi hàng đợi trước, tránh về sau replay
             * dữ liệu 0xFF đã bị xoá (xoá 1 sector = 32 bản ghi, không phải 1) */
            uint32_t firstRec = (sectorAddr - FLASH_LOG_START_ADDR) / FLASH_RECORD_SIZE;
            uint32_t lastRec  = firstRec + FLASH_RECS_PER_SECTOR - 1;
            while (s_hdr.count > 0 && s_hdr.tail >= firstRec && s_hdr.tail <= lastRec) {
                s_hdr.tail = (s_hdr.tail + 1) % maxLimit;
                s_hdr.count--;
            }
            if (EN25_EraseSector(sectorAddr) != 0) {
                sAPI_Debug("[Backup] EraseSector 0x%06X fail, skip push", sectorAddr);
                s_hdrDirty = 1; /* tail có thể đã dời — vẫn cần lưu header */
                Backup_SaveHdr(1);
                return;
            }
        }

        uint8_t pageBuf[FLASH_RECORD_SIZE];
        memset(pageBuf, 0, sizeof(pageBuf));
        memcpy(pageBuf, &tmp, sizeof(tmp));

        if (EN25_Write(addr, pageBuf, FLASH_RECORD_SIZE) != 0) {
            sAPI_Debug("[Backup] Write 0x%06X fail, skip push", addr);
            s_hdrDirty = 1;
            Backup_SaveHdr(1);
            return;
        }

        s_hdr.head = (s_hdr.head + 1) % maxLimit;
        s_hdr.count++;
        s_hdrDirty = 1;
        Backup_SaveHdr(1);
        s_lastStoreTick = GetTickNow();
        sAPI_Debug("[Backup/Push] offline log -> SPI Flash (addr=0x%06X, head=%lu, count=%lu/%lu, dt=%s)",
                   addr, (unsigned long)s_hdr.head, (unsigned long)s_hdr.count, (unsigned long)maxLimit,
                   tmp.datetime);
        return;
    }

    /* Fallback EFS file */
    {
        SCFILE *fp = sAPI_fopen(NASA_BACKUP_FILE, "rb+");
        long offset;

        if (!fp) {
            SCFILE *probe = sAPI_fopen(NASA_BACKUP_FILE, "rb");
            if (probe) {
                /* File tồn tại nhưng rb+ fail — KHÔNG wb truncate (mất hết backup) */
                sAPI_fclose(probe);
                sAPI_Debug("[Backup] EFS rb+ fail, skip push to avoid truncate");
                return;
            }
            /* Chưa có file: tạo mới + ghi header */
            fp = sAPI_fopen(NASA_BACKUP_FILE, "wb");
            if (!fp) return;
            s_efsRecBase = EFS_HDR_SIZE;
            s_hdr.magic = BACKUP_HDR_MAGIC;
            {
                BackupHdr_t hdrCopy = s_hdr;
                hdrCopy.crc32 = Buffer_GetChecksum((const uint8_t *)&hdrCopy, sizeof(uint32_t) * 5);
                if (sAPI_fwrite(&hdrCopy, 1, sizeof(hdrCopy), fp) != (int)sizeof(hdrCopy)) {
                    sAPI_fclose(fp);
                    sAPI_Debug("[Backup] EFS fwrite header fail");
                    return;
                }
            }
        }

        offset = (long)(s_efsRecBase + s_hdr.head * sizeof(BackupRecord_t));
        if (sAPI_fseek(fp, offset, FS_SEEK_BEGIN) != 0) {
            sAPI_fclose(fp);
            sAPI_Debug("[Backup] EFS fseek fail off=%ld head=%lu", offset, (unsigned long)s_hdr.head);
            return;
        }
        {
            /* sAPI_fwrite trả về số BYTE đã ghi (size*num), không phải num */
            int wr = sAPI_fwrite(&tmp, 1, sizeof(tmp), fp);
            if (wr != (int)sizeof(tmp)) {
                sAPI_fclose(fp);
                sAPI_Debug("[Backup] EFS fwrite record fail wr=%d need=%d off=%ld",
                           wr, (int)sizeof(tmp), offset);
                return;
            }
        }
        sAPI_fsync(fp);
        sAPI_fclose(fp);

        s_hdr.head = (s_hdr.head + 1) % maxLimit;
        s_hdr.count++;
        s_hdrDirty = 1;
        Backup_SaveHdr(1);
        s_lastStoreTick = GetTickNow();
        sAPI_Debug("[Backup/Push] offline log -> EFS File (head=%lu, count=%lu/%lu, dt=%s)",
                   (unsigned long)s_hdr.head, (unsigned long)s_hdr.count, (unsigned long)maxLimit,
                   tmp.datetime);
    }
}

void Backup_Push(const BackupRecord_t *rec)
{
    if (!rec) return;
    Backup_Init();
    BackupLock();
    Backup_PushLocked(rec);
    BackupUnlock();
}

/*
 * Đọc bản ghi ở đầu hàng đợi (tail) mà không xoá.
 * Bỏ qua bản ghi hỏng (used != 1). Trả 1 nếu có bản hợp lệ.
 */
static int Backup_PeekLocked(BackupRecord_t *out)
{
    BackupRecord_t tmp;
    uint32_t maxLimit = s_useFlashStorage ? FLASH_MAX_RECORDS : NASA_BACKUP_MAX_RECORDS;

    if (!out || s_hdr.count == 0) return 0;

    if (s_useFlashStorage) {
        while (s_hdr.count > 0) {
            uint32_t addr = FLASH_LOG_START_ADDR + (s_hdr.tail * FLASH_RECORD_SIZE);
            uint8_t pageBuf[FLASH_RECORD_SIZE];

            if (EN25_Read(addr, pageBuf, FLASH_RECORD_SIZE) != 0) return 0;

            memcpy(&tmp, pageBuf, sizeof(tmp));
            if (tmp.used == 1) {
                *out = tmp;
                return 1;
            }
            /* Bản ghi rác — bỏ khỏi hàng đợi ngay */
            s_hdr.tail = (s_hdr.tail + 1) % maxLimit;
            s_hdr.count--;
            s_hdrDirty = 1;
            sAPI_Debug("[Backup/Peek] Skip invalid record (addr=0x%06X, used=%u)",
                       addr, (unsigned)tmp.used);
        }
        return 0;
    }

    {
        SCFILE *fp = sAPI_fopen(NASA_BACKUP_FILE, "rb");
        if (!fp) return 0;

        while (s_hdr.count > 0) {
            long offset = (long)(s_efsRecBase + s_hdr.tail * sizeof(BackupRecord_t));
            sAPI_fseek(fp, offset, FS_SEEK_BEGIN);
            if (sAPI_fread(&tmp, 1, sizeof(tmp), fp) != (int)sizeof(tmp)) {
                sAPI_fclose(fp);
                return 0;
            }
            if (tmp.used == 1) {
                sAPI_fclose(fp);
                *out = tmp;
                return 1;
            }
            s_hdr.tail = (s_hdr.tail + 1) % maxLimit;
            s_hdr.count--;
            s_hdrDirty = 1;
        }
        sAPI_fclose(fp);
        return 0;
    }
}

static int Backup_CommitPopLocked(void)
{
    uint32_t maxLimit = s_useFlashStorage ? FLASH_MAX_RECORDS : NASA_BACKUP_MAX_RECORDS;

    if (s_hdr.count == 0) return 0;
    s_hdr.tail = (s_hdr.tail + 1) % maxLimit;
    s_hdr.count--;
    s_hdrDirty = 1;
    /* Lưu header ngay sau mỗi lần gửi thành công — giảm trùng/mất khi mất điện */
    Backup_SaveHdr(1);
    return 1;
}

int Backup_Peek(BackupRecord_t *out)
{
    int r;
    if (!out) return 0;
    Backup_Init();
    BackupLock();
    r = Backup_PeekLocked(out);
    if (s_hdrDirty) Backup_SaveHdr(1); /* lưu nếu đã skip bản rác */
    BackupUnlock();
    return r;
}

int Backup_CommitPop(void)
{
    int r;
    Backup_Init();
    BackupLock();
    r = Backup_CommitPopLocked();
    BackupUnlock();
    return r;
}

int Backup_Pop(BackupRecord_t *out)
{
    /* Legacy: peek + commit ngay — chỉ dùng khi caller chắc chắn không cần giữ lại */
    if (!out) return 0;
    Backup_Init();
    BackupLock();
    if (!Backup_PeekLocked(out)) {
        if (s_hdrDirty) Backup_SaveHdr(1);
        BackupUnlock();
        return 0;
    }
    Backup_CommitPopLocked();
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
    if (!s_useFlashStorage) {
        sAPI_remove(NASA_BACKUP_FILE);
    }
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
