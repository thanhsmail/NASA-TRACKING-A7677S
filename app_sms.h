#ifndef APP_SMS_H
#define APP_SMS_H

#include "simcom_api.h"

/**
 * Khởi tạo dữ liệu và cấu hình ban đầu cho module SMS.
 */
void SMS_Init(void);
/* Retry CNMI/text mode khi SIM sẵn sàng (gọi từ SmsReceiverTask) */
int SMS_EnsureReady(void);

/**
 * Thực thi lệnh nhận được từ SMS hoặc Server.
 * @param body Nội dung lệnh đã lọc bỏ khoảng trắng thừa.
 * @param src Nguồn gửi lệnh ("SMS", "SMS_FLASH", "SERVER").
 */
void SMS_Command_Execute(const char *body, const char *src);

/**
 * Phân tích phần thân tin nhắn SMS và gọi hàm thực thi lệnh tương ứng.
 * @param payload Toàn bộ nội dung tin nhắn nhận được từ URC.
 * @param source Nguồn gốc tin nhắn để log debug.
 */
void SMS_ExtractAndExecuteSmsBody(const char *payload, const char *source);

/**
 * Kiểm tra trạng thái tải bản cập nhật FOTA đã sẵn sàng để nâng cấp chưa.
 * @return 1 nếu đã sẵn sàng, 0 nếu chưa, 2 nếu đang xử lý reset.
 */
int SMS_IsFotaDownloadReady(void);

/**
 * Đánh dấu trạng thái FOTA đang được xử lý nâng cấp (chuẩn bị reset).
 */
void SMS_SetFotaDownloadHandled(void);

/* --- Các hàm Getter/Setter cho cấu hình hệ thống thay đổi qua SMS --- */

/** Lấy chu kỳ gửi tin khi đang di chuyển (giây) */
int SMS_GetPeriodMoving(void);
/** Lấy chu kỳ gửi tin khi dừng/đỗ (giây) */
int SMS_GetPeriodStopped(void);
/** Lấy biển số xe */
const char *SMS_GetLicensePlate(void);
/** Lấy tên lái xe */
const char *SMS_GetDriverName(void);
/** Lấy số giấy phép lái xe */
const char *SMS_GetDriverLicense(void);

/** Cập nhật biển số xe */
void SMS_SetLicensePlate(const char *val);
/** Cập nhật tên lái xe */
void SMS_SetDriverName(const char *val);
/** Cập nhật số giấy phép lái xe */
void SMS_SetDriverLicense(const char *val);

#endif /* APP_SMS_H */
