#ifndef APP_COM_H
#define APP_COM_H

/**
 * Cấu hình qua cổng COM ảo USB (USB AT port của module).
 * Mỗi dòng gõ vào (kết thúc bằng Enter) được chạy như một lệnh SMS
 * (na,... / sa,...), nguồn lệnh "COM"; câu trả lời in lại ra cổng.
 */
void COM_TaskStart(void);

#endif /* APP_COM_H */
