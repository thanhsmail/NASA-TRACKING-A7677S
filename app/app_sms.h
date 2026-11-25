#ifndef APP_SMS_H
#define APP_SMS_H

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

#endif /* APP_SMS_H */
