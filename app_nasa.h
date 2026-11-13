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
/**
 * Yêu cầu thức khỏi chế độ ngủ (SMS / lệnh / ngắt ACC). Gọi được từ task khác
 * và từ ngắt GPIO; reason phải là chuỗi hằng. Đang thức thì hoãn ngủ thêm
 * SLEEP_EVENT_HOLD_SEC.
 */
void NASA_WakeRequest(const char *reason);
void NASA_AccIrq(void);
/** Lệnh na,43,t: thử độ nhạy cảm biến rung trong sec giây lúc đang thức (chạy ở nasa_reporter) */
void NASA_MotionTestStart(int sec);
/** @return 1 nếu có LIS3DSH + INT1; ngưỡng lần nạp gần nhất, trạng thái / kết quả lần thử */
int  NASA_MotionInfo(int *thrMg, int *testOn, int *testCount, int *testSec);
/** Lệnh na,44,t: chạy self-test LIS3DSH ở vòng nasa_reporter kế tiếp (lúc thức) */
void NASA_AccelSelfTestRequest(void);
/** Lệnh na,44,0|1: áp dụng CFG_GetAccelEnabled() (tắt / bật LIS3DSH) ở vòng nasa_reporter kế tiếp */
void NASA_AccelPowerRequest(void);
/** Ghi tình trạng LIS3DSH (kiểm tra định kỳ + self-test gần nhất) vào buf; @return độ dài */
int  NASA_AccelStatus(char *buf, int len);

#endif /* APP_NASA_H */
