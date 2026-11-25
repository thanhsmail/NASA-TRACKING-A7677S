/**
 * @file    app_main.h
 * @brief   Điểm khởi động của ứng dụng, gọi từ port/<chip>/port_entry.c
 */
#ifndef APP_MAIN_H
#define APP_MAIN_H

/**
 * Khởi tạo ứng dụng và tạo các task. Gọi một lần, sau khi SDK đã sẵn sàng
 * (trên SIMCom: sau ApiMapInit). Hàm trả về sau khi tạo xong các task.
 */
void APP_Main(void);

#endif /* APP_MAIN_H */
