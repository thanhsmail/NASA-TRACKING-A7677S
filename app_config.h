#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Số lượng tick hệ thống mỗi giây (SIMCom OpenCPU) */
#define SC_TICKS_PER_SECOND 200

/* ---- Chu kỳ báo cáo mặc định (Config cmd 4) ---- */
#define TRACKING_PERIOD_MOVING_DEFAULT 10   // 10 giây khi xe di chuyển
#define TRACKING_PERIOD_STOPPED_DEFAULT 300 // 300 giây khi xe dừng đỗ
#define TRACKING_PERIOD_MIN_SEC 5    // Thời gian tối thiểu giữa các lần báo cáo
#define TRACKING_PERIOD_MAX_SEC 3600 // Thời gian tối đa giữa các lần báo cáo

/* Chu kỳ gửi bản tin 6 dừng đỗ (giây) — Config cmd 26 */
#define PARK_REPORT_PERIOD_SEC_DEFAULT 60
#define PARK_REPORT_PERIOD_SEC_MAX 900

/* Ngưỡng vận tốc phân biệt chạy/dừng (km/h) — Config cmd 10 */
#define TRACKING_SPEED_THRESHOLD_DEFAULT 3

/* Hệ số đổi đơn vị tốc độ GPS (knots) sang km/h */
#define KNOTS_TO_KMH 1.852

#define ERROR_RETRY_DELAY_SEC 30 // Thời gian retry khi báo cáo thất bại
#define APP_STARTUP_DELAY_SEC 5  // Thời gian delay sau khi khởi động
#define TASK_BASE_SLEEP_TICKS (SC_TICKS_PER_SECOND) // Số tick cơ bản

/* Login ACK timeout */
#define NASA_LOGIN_ACK_TIMEOUT_SEC 30 // Thời gian chờ ACK khi đăng nhập

/* NTP / GNSS */
#define NASA_NTP_SERVER "pool.ntp.org"
#define NASA_NTP_TIMEZONE_PARAM 28
#define NASA_GNSS_UTC_OFFSET_HOURS 7
#define NASA_GNSS_TIME_MAX_AGE_SEC 120
#define NASA_NTP_MSGQ_WAIT_MS 120000u
#define NASA_NTP_RETRY_INTERVAL (200 * 30)
#define NASA_NTP_MAX_ATTEMPTS 3
#define NASA_NITZ_ENABLE_VALUE 1
#define NASA_SERVER_CMD_BUF_SIZE 1024
#define NASA_SERVER_RECV_TIMEOUT_US 0

#define PARK_STOP_CONFIRM_SEC 3 // Thời gian xác nhận dừng đỗ
#define ACC_DEBOUNCE_CYCLES 6   // Số chu kỳ debounce cho ACC

/* Ngưỡng góc lái — gửi dày hơn khi cua */
#define HEADING_THRESHOLD_SHARP_DEG 30.0 // Góc cua gấp
#define HEADING_THRESHOLD_TURN_DEG 15.0  // Góc cua
#define HEADING_PERIOD_SHARP_SEC 2       // Giảm chu kỳ khi cua gấp
#define HEADING_PERIOD_TURN_FACTOR 0.5   // Giảm chu kỳ khi cua
#define HEADING_MIN_SPEED_KPH 8.0 // Vận tốc tối thiểu để phát hiện góc lái

/* Odometer */
#define ODOM_MIN_DIST_KM 0.001       // Quãng đường tối thiểu để tính Odometer
#define ODOM_MAX_JUMP_FACTOR 3.0     // Hệ số nhảy Odometer tối đa
#define ODOM_MAX_STATIONARY_KM 0.010 // Quãng đường tối đa khi dừng đỗ
#define ODOM_GPS_LOSS_TIMEOUT_SEC 30 // Thời gian GPS mất để reset Odometer

#define MOVING_CONFIRM_SEC 3   // Thời gian xác nhận trạng thái di chuyển
#define STOPPED_CONFIRM_SEC 10 // Thời gian xác nhận trạng thái dừng đỗ
/* Dịch chuyển thực khỏi điểm neo tối thiểu để xác nhận xe chạy (chống nhiễu
 * GPS khi đỗ — nhiễu đô thị có thể báo tốc độ tới 26 km/h nhưng vị trí chỉ
 * quẩn quanh điểm đỗ) */
#define MOVING_MIN_DISPLACEMENT_KM 0.05 // 50 m

/* Phiên bản thiết bị RV26 / A7677S */
#define NASA_DEVICE_NAME "NASA4G"
#define NASA_HW_CODE "v1.0"
#define NASA_FW_CODE "v1.0.260718a"
#define NASA_SERVER_HOST_DEFAULT "103.57.209.16"
#define NASA_SERVER_PORT_DEFAULT 2590
#define NASA_PDP_ID 1

/* Điện áp acquy: ngưỡng bình yếu (Volt) */
#define NASA_BATTERY_LOW_VOLT 11.5f
#define NASA_SPEED_LIMIT_KPH 80

/* Khóa cấu hình / SĐT trung tâm — 0 = tắt (chưa dùng) */
#define NASA_LOCK_FEATURE_ENABLE 0
#define NASA_LOCK_AFTER_DAYS 10

/* File lưu cấu hình / backup trên FS nội bộ */
#define NASA_CFG_FILE "C:/rv26.cfg"
#define NASA_BACKUP_FILE "C:/rv26_bak.dat"
#define NASA_BACKUP_INTERVAL_SEC 10
#define NASA_BACKUP_MAX_RECORDS 2000
#define NASA_BACKUP_REPLAY_BURST 5

/*
 * GPIO board RV26 trên A7677S — pad từ A7670C_MANS_LANS_GPIO.h (cùng họ
 * ASR1606). SC_MODULE_GPIO_00 chỉ có khi FEATURE_SIMCOM_NORI; dùng pad số cố
 * định. Đổi khi schematic PCB khác.
 */
#ifndef GPIO_LED_NET
#define GPIO_LED_NET 80 /* SC_MODULE_GPIO_00 */
#endif
#ifndef GPIO_LED_PWR
#define GPIO_LED_PWR 32 /* SC_MODULE_GPIO_01 */
#endif
#ifndef GPIO_LED_GNSS
#define GPIO_LED_GNSS 74 /* SC_MODULE_GPIO_03 */
#endif
#ifndef GPIO_DOUT
#define GPIO_DOUT 73 /* SC_MODULE_GPIO_04 */
#endif
#ifndef GPIO_ACC_IN
#define GPIO_ACC_IN 37 /* SC_MODULE_GPIO_10 */
#endif

/* Bitmask trạng thái thiết bị (Protocol RV26 — bản tin 2/7) */
#define ST_GPS (1u << 0)
#define ST_FLASH_OK (1u << 1)
#define ST_ACC (1u << 2)
#define ST_DOOR (1u << 3)
#define ST_AC (1u << 4)
#define ST_DRIVER (1u << 5)
#define ST_SPEED_VIOLATION (1u << 6)
#define ST_LXLT_VIOLATION (1u << 7)
#define ST_BATTERY_OK (1u << 8)
#define ST_FUEL_RS232 (1u << 9)
#define ST_FUEL_PULSE (1u << 10)
#define ST_TEMP_SENSOR (1u << 11)
#define ST_SOS (1u << 12)
#define ST_BEN (1u << 13)

#endif /* APP_CONFIG_H */
