#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include <stdint.h>

/**
 * Kich hoat PDP context de chuan bi ket noi mang.
 * @param pdp_id ID cua PDP context (thong thuong la 1).
 * @return 0 neu thanh cong, -1 neu that bai.
 */
int Network_ActivatePdp(int pdp_id);

/**
 * Thuc hien phan giai DNS va ket noi TCP den server.
 * @param host IP hoac ten mien cua server.
 * @param port Cong (port) cua server.
 * @return 0 neu thanh cong, -1 neu that bai.
 */
int Network_Connect(const char *host, int port);

/**
 * Dong ket noi TCP hien tai va giai phong socket.
 */
void Network_Disconnect(void);

/**
 * Gui chuoi du lieu qua socket TCP dang ket noi.
 * @param data Chuoi ky tu can gui.
 * @param len Do dai chuoi du lieu.
 * @return So byte da gui hoac -1 neu gap loi.
 */
int Network_Send(const char *data, uint32_t len);

/**
 * Kiem tra (poll) du lieu den tu server bang select timeout=0.
 * Nhan du lieu, cat dong va thuc thi lenh tuong ung.
 * @return 0 neu hoat dong binh thuong, 1 neu mat ket noi hoac socket loi.
 */
int Network_RecvPoll(void);

/**
 * Kiem tra trang thai ket noi socket.
 * @return 1 neu socket hap le, 0 neu chua ket noi hoac da dong.
 */
int Network_IsConnected(void);

#endif /* APP_NETWORK_H */
