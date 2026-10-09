/**
 * @file    hal_fota.h
 * @brief   HAL - FOTA Abstraction (cập nhật customer_app.bin từ xa)
 */
#ifndef HAL_FOTA_H
#define HAL_FOTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Tải gói ứng dụng (customer_app.bin) về phân vùng cập nhật. Hàm block
 *         cho tới khi tải xong hoặc lỗi.
 * @param  url URL đầy đủ: http:// | https:// | ftp:// | ftps://
 * @param  recvTimeoutMs Timeout chờ dữ liệu từ server (ms)
 * @return 0 nếu thành công, -1 nếu tham số sai, >0 là mã lỗi của SDK
 *         (SCAppDwonLoadReturnCode)
 */
int HAL_FOTA_AppDownload(const char *url, uint32_t recvTimeoutMs);

/**
 * @brief  Kiểm tra CRC gói ứng dụng vừa tải. Chỉ khi hàm này trả về 0 thì
 *         bootloader mới nạp gói mới ở lần reset kế tiếp.
 * @param  outSize Kích thước gói (byte), có thể NULL
 * @return 0 nếu gói hợp lệ, khác 0 nếu lỗi
 */
int HAL_FOTA_AppPackageVerify(uint32_t *outSize);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FOTA_H */
