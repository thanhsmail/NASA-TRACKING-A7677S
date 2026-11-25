#ifndef APP_FOTA_H
#define APP_FOTA_H

/* Kết quả FOTA_Request */
#define FOTA_REQ_OK       0  /* Đã nhận yêu cầu, bắt đầu tải nền */
#define FOTA_REQ_BAD_URL -1  /* URL rỗng / quá dài / sai định dạng */
#define FOTA_REQ_BUSY    -2  /* Đang có phiên cập nhật khác */
#define FOTA_REQ_NO_TASK -3  /* Không tạo được task FOTA */

/**
 * Yêu cầu cập nhật firmware từ URL. Tên file customer_app.bin → cập nhật ứng
 * dụng; tên khác (vd system_patch.bin) → cập nhật hệ thống (gói vi sai adiff).
 * Hàm trả về ngay; việc tải + kiểm tra + reset chạy trong task riêng.
 * @param url http:// | https:// | ftp:// | ftps:// (thiếu scheme → mặc định http://)
 * @param notifyPhone SĐT nhận SMS báo kết quả (NULL/"" nếu không cần)
 * @param notifyServer 1 nếu lệnh đến từ Server: kết quả được báo lên Server
 *                     (lấy bằng FOTA_TakeServerResult)
 * @return FOTA_REQ_*
 */
int FOTA_Request(const char *url, const char *notifyPhone, int notifyServer);

/**
 * Lấy kết quả FOTA đang chờ báo lên Server (mỗi kết quả chỉ lấy được 1 lần).
 * Gọi từ task NASA (task duy nhất ghi socket Server).
 * @return 1 nếu có kết quả (đã chép vào out), 0 nếu không có
 */
int FOTA_TakeServerResult(char *out, int outsz);

/** @return 1 nếu đang có phiên cập nhật (đang chờ / đang tải), 0 nếu rảnh */
int FOTA_IsBusy(void);

#endif /* APP_FOTA_H */
