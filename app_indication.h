/**
 * @file app_indication.h
 * @brief LED / ACC Status Indication Module -- NASA Tracking
 *
 * Quan ly status LEDs (GNSS, NET, PWR), doc chan ACC in voi debounce,
 * dong bo relay DOUT, va giam sat Watchdog timeout.
 */
#ifndef APP_INDICATION_H
#define APP_INDICATION_H

/**
 * Khởi động Task chỉ thị LED / ACC / Watchdog nền.
 * @return 0 nếu thành công, -1 nếu thất bại.
 */
int INDICATION_TaskStart(void);

#endif /* APP_INDICATION_H */
