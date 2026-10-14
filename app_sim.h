#ifndef APP_SIM_H
#define APP_SIM_H

/**
 * Theo dõi rút / cắm SIM khi module không báo URC (hot swap chưa bật hoặc
 * board không nối chân SIM_DET): hỏi trạng thái SIM định kỳ.
 *
 * - SIM mất: báo NASA, sau một lúc thì tắt / bật sóng (CFUN 0/1) định kỳ để
 *   module đọc lại khe SIM, vì SIM cắm lại sau khi khởi động không tự được nhận.
 * - SIM có lại hoặc đổi SIM (ICCID khác): cấu hình lại SMS và yêu cầu NASA
 *   đăng nhập lại với ICCID mới.
 *
 * Gọi SIM_Poll() thường xuyên (mỗi ~1 giây) từ task SMS; hàm tự giới hạn tần
 * suất và có thể chặn vài giây khi tắt / bật sóng.
 */
void SIM_Poll(void);

/** @return 1 nếu SIM đang sẵn sàng, 0 nếu không / chưa biết */
int SIM_IsReady(void);

#endif /* APP_SIM_H */
