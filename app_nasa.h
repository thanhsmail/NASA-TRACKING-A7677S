#ifndef APP_NASA_H
#define APP_NASA_H

void NASA_Init(void);
void NASA_RunStep(void);
void NASA_SetAcc(int accVal);
int  NASA_IsSessionActive(void);
float NASA_GetVoltage(void);

/** cmd 39: 2=location (frame2), 7=sensor/backup */
void NASA_RequestImmediate(int msgType);
/** cmd 12: phát lại backup (hiện gửi toàn bộ queue) */
void NASA_RequestReplay(const char *fromDt, const char *toDt);
void NASA_ClearBackup(void);
void NASA_ClearAllData(void);

/** Server gửi dòng bắt đầu bằng !NASA — xử lý ACK login */
void NASA_OnServerProtocolLine(const char *line);

/** Bản tin 5: đăng nhập / đăng xuất lái xe (cmd 14, 25) */
void NASA_DriverLogin(void);
void NASA_DriverLogout(void);

/**
 * SIM vừa cắm lại / đổi SIM (gọi từ task khác). Task NASA cập nhật ICCID và
 * đăng nhập lại để Server nhận ICCID mới.
 */
void NASA_OnSimChanged(const char *iccid);

uint32_t NASA_GetNextMessageId(void);
void NASA_FeedWatchdog(void);
int  NASA_IsWatchdogTimeout(void);
/** 1 = NASA_Init đã xong (đã quét flash backup), task chính đang chạy vòng lặp */
int  NASA_IsStarted(void);

/** 1 = đang ngủ tiết kiệm ắc quy (GNSS tắt, LED tắt, các task lặp thưa) */
int  NASA_IsSleeping(void);
/** 1 = xe đang bị dời khi dây ACC tắt (đẩy / kéo / chở): báo dày, không ngủ */
int  NASA_IsMovingNoAcc(void);
/**
 * Yêu cầu thức khỏi chế độ ngủ (SMS / lệnh / ngắt ACC). Gọi được từ task khác
 * và từ ngắt GPIO; reason phải là chuỗi hằng. Đang thức thì hoãn ngủ thêm
 * SLEEP_EVENT_HOLD_SEC.
 */
void NASA_WakeRequest(const char *reason);
void NASA_AccIrq(void);
/** @return 1 nếu có LIS3DSH + INT1; thrMg = ngưỡng lần nạp gần nhất (mg) */
int  NASA_MotionInfo(int *thrMg);
/** Lệnh na,43,off|on: áp dụng CFG_GetAccelEnabled() (tắt / bật LIS3DSH) ở vòng nasa_reporter kế tiếp */
void NASA_AccelPowerRequest(void);
/** Tên tình trạng LIS3DSH: "TAT", "OK", "khong co cam bien", "loi I2C"... */
const char *NASA_AccelStateName(void);
/** Ghi tình trạng LIS3DSH chi tiết (kiểm tra định kỳ + self-test gần nhất) vào buf; @return độ dài */
int  NASA_AccelStatus(char *buf, int len);

#endif /* APP_NASA_H */
