/**
 * @file    hal_fs.h
 * @brief   HAL - File System Abstraction
 *
 * app_cfg.c va app_backup.c chi goi HAL_FS_*. Implementation dung
 * sAPI_fopen / sAPI_fread / sAPI_fwrite / sAPI_fclose / sAPI_fseek
 * cua SIMCom OpenSDK. Khi doi sang chip co FATFS hoac SPIFFS thi
 * chi can viet lai ham trong hal_impl_<chip>.c.
 */
#ifndef HAL_FS_H
#define HAL_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Handle file (opaque, anh xa sang SC_F_OBJ* cua SIMCom) */
typedef void *HalFile_t;

/** Gia tri tra ve khi mo file that bai */
#define HAL_FS_INVALID_HANDLE  ((HalFile_t)0)

/* Hang so seek -- giong POSIX */
#define HAL_FS_SEEK_SET  0  /**< Tu dau file */
#define HAL_FS_SEEK_CUR  1  /**< Tu vi tri hien tai */
#define HAL_FS_SEEK_END  2  /**< Tu cuoi file */

/**
 * @brief  Mo file.
 * @param  path  Duong dan (VD: "C:/rv26.cfg" tren SIMCom FS).
 * @param  mode  "r", "w", "rb", "wb", "r+b" ...
 * @return Handle hop le, hoac HAL_FS_INVALID_HANDLE neu that bai.
 */
HalFile_t HAL_FS_Open(const char *path, const char *mode);

/**
 * @brief  Dong file.
 * @return 0 neu thanh cong.
 */
int HAL_FS_Close(HalFile_t f);

/**
 * @brief  Doc du lieu tu file.
 * @param  buf   Buffer nhan.
 * @param  size  So byte can doc.
 * @return So byte da doc thuc te, hoac -1 neu loi.
 */
int HAL_FS_Read(HalFile_t f, void *buf, uint32_t size);

/**
 * @brief  Ghi du lieu vao file.
 * @param  buf   Buffer ghi.
 * @param  size  So byte can ghi.
 * @return So byte da ghi thuc te, hoac -1 neu loi.
 */
int HAL_FS_Write(HalFile_t f, const void *buf, uint32_t size);

/**
 * @brief  Di chuyen con tro doc/ghi trong file.
 * @param  offset  Do lech (bytes).
 * @param  whence  HAL_FS_SEEK_SET / SEEK_CUR / SEEK_END.
 * @return Vi tri moi tinh tu dau file, hoac -1 neu loi.
 */
int HAL_FS_Seek(HalFile_t f, int32_t offset, int whence);

/**
 * @brief  Lay kich thuoc file (bytes).
 * @return Kich thuoc, hoac -1 neu loi.
 */
int HAL_FS_Size(HalFile_t f);

/**
 * @brief  Xoa file.
 * @return 0 neu thanh cong.
 */
int HAL_FS_Delete(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FS_H */
