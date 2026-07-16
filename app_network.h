#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include "simcom_api.h"

/**
 * Kích hoạt PDP context để chuẩn bị kết nối mạng.
 * @param pdp_id ID của PDP context (thông thường là 1).
 * @return 0 nếu thành công, -1 nếu thất bại.
 */
int Network_ActivatePdp(INT32 pdp_id);

/**
 * Thực hiện phân giải DNS và kết nối TCP đến server.
 * @param host IP hoặc tên miền của server.
 * @param port Cổng (port) của server.
 * @return 0 nếu kết nối thành công, -1 nếu thất bại.
 */
int Network_Connect(const char *host, int port);

/**
 * Đóng kết nối TCP hiện tại và giải phóng socket.
 */
void Network_Disconnect(void);

/**
 * Gửi chuỗi dữ liệu qua socket TCP đang kết nối.
 * @param data Chuỗi ký tự cần gửi.
 * @param len Độ dài chuỗi dữ liệu.
 * @return Số byte đã gửi hoặc -1 nếu gặp lỗi.
 */
int Network_Send(const char *data, uint32_t len);

/**
 * Kiểm tra (poll) dữ liệu đến từ server bằng select timeout=0.
 * Nhận dữ liệu, cắt dòng và thực thi lệnh tương ứng.
 * @return 0 nếu hoạt động bình thường, 1 nếu mất kết nối hoặc socket lỗi.
 */
int Network_RecvPoll(void);

/**
 * Kiểm tra trạng thái kết nối socket.
 * @return 1 nếu socket hợp lệ, 0 nếu chưa kết nối hoặc đã đóng.
 */
int Network_IsConnected(void);

#endif /* APP_NETWORK_H */
