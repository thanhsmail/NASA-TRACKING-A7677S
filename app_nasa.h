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

uint32_t NASA_GetNextMessageId(void);
void NASA_FeedWatchdog(void);
int  NASA_IsWatchdogTimeout(void);

#endif /* APP_NASA_H */
