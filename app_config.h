#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "hal/hal_os.h"

/* ---- Chu kỳ báo cáo mặc định (Config cmd 4) ---- */
#define TRACKING_PERIOD_MOVING_DEFAULT 10   // 10 giây khi xe di chuyển
#define TRACKING_PERIOD_STOPPED_DEFAULT 300 // 300 giây khi xe dừng đỗ
#define TRACKING_PERIOD_MIN_SEC 5    // Thời gian tối thiểu giữa các lần báo cáo
#define TRACKING_PERIOD_MAX_SEC 3600 // Thời gian tối đa giữa các lần báo cáo

/* Chu kỳ gửi bản tin 6 dừng đỗ (giây) — Config cmd 26 */
#define PARK_REPORT_PERIOD_SEC_DEFAULT 60
#define PARK_REPORT_PERIOD_SEC_MAX 900

/* Bản tin 5 — thời gian làm việc lái xe */
#define WORK_PERIODIC_SEC 60
#define WORK_LOGOUT_STOP_SEC 900   /* dừng >= 15 phút → đăng xuất (5-3) */
#define WORK_LXLT_LIMIT_MIN 240    /* LXLT quá 4 giờ */
#define NASA_DEFAULT_DRIVER_NAME    "HO VA TEN LAI XE"
#define NASA_DEFAULT_DRIVER_LICENSE "GIAYPHEPXE"

/* Ngưỡng vận tốc phân biệt chạy/dừng (km/h) — Config cmd 10 */
#define TRACKING_SPEED_THRESHOLD_DEFAULT 3

/* Hệ số đổi đơn vị tốc độ GPS (knots) sang km/h */
#define KNOTS_TO_KMH 1.852

#define ERROR_RETRY_DELAY_SEC 30 // Thời gian retry khi báo cáo thất bại
#define APP_STARTUP_DELAY_SEC 5  // Thời gian delay sau khi khởi động
#define TASK_BASE_SLEEP_TICKS (HAL_TICKS_PER_SEC) // Số tick cơ bản

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

/* Rút / cắm SIM không có URC (app_sim.c) */
#define SIM_POLL_SEC 5                 // Chu kỳ hỏi trạng thái SIM
#define SIM_ABSENT_CONFIRM 2           // Số lần liên tiếp không sẵn sàng mới coi là rút SIM
#define SIM_REDETECT_AFTER_SEC 30      // Mất SIM bao lâu thì bắt đầu tắt / bật sóng
#define SIM_REDETECT_INTERVAL_SEC 60   // Chu kỳ tắt / bật sóng khi vẫn chưa thấy SIM
#define SIM_RADIO_OFF_SEC 3            // Thời gian tắt sóng mỗi lần
#define SIM_ICCID_CHECK_SEC 60         // Chu kỳ so ICCID để phát hiện đổi SIM nhanh
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
#define ODOM_MAX_STATIONARY_KM 0.100 // Quãng đường tối đa khi dừng đỗ
#define ODOM_GPS_LOSS_TIMEOUT_SEC 30 // Thời gian GPS mất để reset Odometer
#define ODOM_JUMP_MIN_CALC_SPEED_KPH 30.0 // Ngưỡng vận tốc sàn tối thiểu để tính maxDist cho Jump Filter
#define ODOM_SAVE_INTERVAL_SEC 60     // Khoảng cách tối thiểu giữa 2 lần lưu km ngày khi đang chạy
#define ODOM_SAVE_MIN_DELTA_KM 0.05  // Chỉ lưu khi km ngày đổi ít nhất 50 m (khi xe dừng: lưu mọi thay đổi)
#define ODOM_RESTORE_MAX_KM 5000.0   // Km ngày đọc lại lớn hơn mức này coi là hỏng

#define MOVING_CONFIRM_SEC 3   // Thời gian xác nhận trạng thái di chuyển
#define STOPPED_CONFIRM_SEC 10 // Thời gian xác nhận trạng thái dừng đỗ (khớp Park)
#define PARKED_CONFIRM_SEC 600 // Thời gian dừng liên tục (10 phút = 600s) xác nhận Đỗ (accMode 0 hoặc fallback)
#define PARK_FRAME_GUARD_SEC 5 // Thời gian tối thiểu (5s) hoãn phát bản tin 6-3 sau khi phát 6-1

/* Dịch chuyển thực khỏi điểm neo tối thiểu để xác nhận xe chạy (chống nhiễu
 * GPS khi đỗ — nhiễu đô thị có thể báo tốc độ tới 26 km/h nhưng vị trí chỉ
 * quẩn quanh điểm đỗ) */
#define MOVING_MIN_DISPLACEMENT_KM 0.015 // 15 m
/* Chế độ vệ tinh (lệnh 3) — chip Unicorecomm bản trong nước, SDK chỉ có 3 chế độ */
#define GNSS_MODE_MIN 1
#define GNSS_MODE_MAX 3
#define GNSS_MODE_DEFAULT 1 // 1 = GPS + BeiDou + QZSS (nhiều vệ tinh nhất)

/* Chế độ giữ tĩnh của chip GNSS ($CFGDYN): tốc độ dưới ngưỡng thì chip giữ vị
 * trí, chống trôi khi dừng. 100 cm/s = 3,6 km/h, khớp ngưỡng chạy/dừng 3 km/h. 0 = tắt */
#define GNSS_STATIC_HOLD_CMS 100

/* AGPS (tăng tốc bắt vệ tinh). Nhà sản xuất khuyến cáo <= 12 lần/ngày, gọi dày hơn có thể treo */
#define AGPS_ENABLE 1
#define AGPS_NO_FIX_WAIT_SEC 60      // Có mạng mà chưa có fix sau ngần này giây (từ lúc bật) thì tải AGPS
#define AGPS_MIN_INTERVAL_SEC 7200   // Tối thiểu 2 giờ giữa 2 lần tải

/* Log debug GNSS / AGPS: thời gian có fix, mất / có lại fix, lý do chưa tải AGPS,
 * trạng thái định kỳ ([GNSS/Stat]). 0 = tắt khi phát hành nếu log quá nhiều */
#define GNSS_DEBUG 1
#define GNSS_DEBUG_STATUS_SEC 30     // Chu kỳ in [GNSS/Stat] và lý do bỏ qua AGPS

/* Điểm neo khi đứng yên = trung bình các mẫu GPS tốt (app_gps.c ApplyStationaryAnchor) */
#define ANCHOR_MAX_HDOP 2.5                // Mẫu HDOP lớn hơn không gộp vào điểm neo
#define ANCHOR_OUTLIER_KM 0.025            // Mẫu cách trung bình hơn 25 m bị bỏ (nhiễu đa đường)
#define ANCHOR_MIN_SAMPLES_FOR_REJECT 10   // Chưa đủ số mẫu này thì chưa loại mẫu xa
#define ANCHOR_RESTART_REJECTS 30          // 30 mẫu tốt liên tiếp bị loại thì làm lại điểm neo
#define ANCHOR_MAX_SAMPLES 900            // Đủ 15 phút mẫu (1 mẫu/giây) thì giữ cố định điểm neo

/* ACC tắt mà xe bị dời xa điểm neo hơn ngưỡng này (kéo / chở xe) mới thoát đỗ */
#define PARKED_ACC_OFF_MIN_DISPLACEMENT_KM 0.150 // 150 m

/*
 * Quy tắc accMode (Config cmd 8):
 *  - accMode == 0 (GPS Speed only): Đỗ khi dừng liên tục >= PARKED_CONFIRM_SEC (600s).
 *  - accMode == 1 (Wire ACC only + Fallback): ACC OFF -> COASTING_TO_PARK -> PARKED khi speed <= thresh. Fallback 600s.
 *  - accMode == 2 (Hybrid ACC): như accMode 1 nếu dây ACC đã từng báo ON từ lúc khởi động;
 *    nếu chưa (xe không đấu dây) thì chạy theo vận tốc như accMode 0.
 *  - Chưa đọc được chân ACC lần nào: không coi là ACC OFF, chạy theo vận tốc.
 */

/* Phiên bản thiết bị RV26 / A7677S */
#define NASA_DEVICE_NAME "RV4G"
#define NASA_HW_CODE "v1.0"
#define NASA_FW_CODE "v1.05.261014"
#define NASA_SERVER_HOST_DEFAULT "103.57.209.16"
#define NASA_SERVER_PORT_DEFAULT 2590
#define NASA_PDP_ID 1

/* FOTA — cập nhật firmware từ URL (Config cmd 33): tên file customer_app.bin
 * → cập nhật ứng dụng; tên khác (vd system_patch.bin) → cập nhật hệ thống */
#define FOTA_APP_FILE_NAME "customer_app.bin"
/* FOTA hệ thống: hết giờ nếu không có tiến triển. Phải đủ dài: sau khi nhận
 * ~64 KB đầu, SDK tạm ngừng nhận để kiểm tra firmware cũ (nhiều phút, không
 * gọi callback) rồi mới tải tiếp. */
#define FOTA_SYS_STALL_SEC 1200
#define FOTA_URL_MAX_LEN 200       // Độ dài URL tối đa (kể cả scheme)
#define FOTA_START_DELAY_SEC 3     // Chờ gửi xong phản hồi lệnh rồi mới bắt đầu tải
#define FOTA_MAX_ATTEMPTS 3        // Số lần thử tải lại khi lỗi
#define FOTA_RETRY_DELAY_SEC 10    // Thời gian chờ giữa các lần thử
#define FOTA_RECV_TIMEOUT_MS 5000  // Timeout chờ dữ liệu từ server
#define FOTA_ERR_CRC (-100)        // Tải xong nhưng gói sai CRC
#define FOTA_ERR_SYS_FAIL (-101)   // FOTA hệ thống: SDK báo tải/kiểm tra lỗi
#define FOTA_ERR_SYS_TIMEOUT (-102) // FOTA hệ thống: không có tiến triển

/* Điện áp acquy: ngưỡng bình yếu (Volt) */
#define NASA_BATTERY_LOW_VOLT 10.5f
#define NASA_SPEED_LIMIT_KPH    120 

/* Khóa cấu hình / SĐT trung tâm — 0 = tắt (chưa dùng) */
#define NASA_LOCK_FEATURE_ENABLE 0
#define NASA_LOCK_AFTER_DAYS 10

/* File lưu cấu hình / backup trên FS nội bộ */
#define NASA_CFG_FILE "C:/rv26.cfg"
#define NASA_BACKUP_FILE "C:/rv26_bak.dat"
#define NASA_ODOM_FILE "C:/rv26_odo.dat" /* km trong ngày, đọc lại sau khi khởi động */
#define NASA_BACKUP_INTERVAL_SEC 10
#define NASA_BACKUP_MAX_RECORDS 2000
#define NASA_BACKUP_REPLAY_BURST 5 

/*
 * GPIO board RV26 trên A7677S — pad từ A7670C_MANS_LANS_GPIO.h (cùng họ
 * ASR1606). SC_MODULE_GPIO_00 chỉ có khi FEATURE_SIMCOM_NORI; dùng pad số cố
 * định. Đổi khi schematic PCB khác.
 *
 * Quan trọng: GPIO_05 (pad 120, pin19) dành riêng cho CS flash qua NPN C1815 —
 * không được dùng chung với LED/DOUT.
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
#define GPIO_DOUT 28 /* SC_MODULE_GPIO_09 / pin52 */
#endif
#ifndef GPIO_ACC_IN
#define GPIO_ACC_IN 37 /* SC_MODULE_GPIO_10 */
#endif

/*
 * SPI flash rời EN25QH64A trên A7677S MANS (SC_SPI_1606_PIN):
 *  - SPI channel 0 = SSP0 data (da xac nhan tren board: JEDEC ID EF 40 17): pin11 CLK (pad16), pin13 MOSI (pad19), pin14 MISO (pad18)
 *  - CS# Flash: GPIO_05 (pad 120, pin19) → NPN C1815 đảo logic
 *      MCU HIGH → transistor dẫn → CS# = 0V (Active)
 *      MCU LOW  → transistor ngắt → CS# kéo lên 3.3V (Inactive)
 *  - Không dùng SSP0_CS (pin12/pad17) cho CS — schematic đấu GPIO riêng
 */
#ifndef FLASH_SPI_INDEX
#define FLASH_SPI_INDEX 0
#endif
#ifndef FLASH_SPI_CS_PIN
#define FLASH_SPI_CS_PIN 120 /* SC_MODULE_GPIO_05 / pin19 — lái CS# qua C1815 */
#endif

#if (GPIO_LED_PWR == FLASH_SPI_CS_PIN) || (GPIO_LED_NET == FLASH_SPI_CS_PIN) || \
    (GPIO_LED_GNSS == FLASH_SPI_CS_PIN) || (GPIO_DOUT == FLASH_SPI_CS_PIN) || \
    (GPIO_ACC_IN == FLASH_SPI_CS_PIN)
#error "FLASH_SPI_CS_PIN xung đột với GPIO LED/DOUT/ACC — Flash CS phải chân riêng"
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
