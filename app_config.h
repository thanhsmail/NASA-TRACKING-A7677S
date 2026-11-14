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
#define SIM_RADIO_ON_RETRY 3           // Số lần thử CFUN=1 liền nhau (cách 1 s); vẫn lỗi thì thử lại mỗi SIM_POLL_SEC
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
/* Xác nhận vào cua bằng gia tốc ngang của LIS3DSH (app_nasa.c TurnPoll): rút
 * ngắn chu kỳ gửi ngay khi xe bắt đầu cua, không chờ hướng GPS lệch đủ 15° / 30°.
 * Gia tốc ngang = v x tốc độ quay: 40 km/h quay 6°/s ~ 120 mg, 12°/s ~ 250 mg. */
#define ACCEL_TURN_ENABLE 1
#define ACCEL_TURN_MG 120             // >= ngưỡng này: cua -> chu kỳ chạy x HEADING_PERIOD_TURN_FACTOR
#define ACCEL_TURN_SHARP_MG 250       // >= ngưỡng này: cua gấp -> HEADING_PERIOD_SHARP_SEC
#define ACCEL_TURN_CONFIRM_SEC 2      // mức cao hơn giữ ngần này giây (vòng 1 s) mới nhận
#define ACCEL_TURN_RELEASE_SEC 2      // hết cua giữ ngần này giây mới trả chu kỳ thường
#define ACCEL_TURN_GRAVITY_TAU_SEC 60 // hằng số lọc trọng lực lúc xe chạy (giây)

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

/* Vị trí khi mất GPS. Fix cũ hơn GPS_FIX_STALE_SEC (GNSS im lặng / đang tắt lúc
 * ngủ) không còn coi là hợp lệ: gửi vị trí giữ lại, tốc độ 0, số vệ tinh 0. */
#define GPS_FIX_STALE_SEC 5
/* Vị trí hợp lệ cuối lưu ra NASA_POS_FILE để khởi động lại không gửi 0,0.
 * Chỉ ghi khi đã dời >= POS_SAVE_MIN_KM, và khi xe dừng hoặc đã qua
 * POS_SAVE_INTERVAL_SEC lúc đang chạy (giảm ghi flash). */
#define POS_SAVE_MIN_KM 0.03
#define POS_SAVE_INTERVAL_SEC 300

/* LBS (vị trí theo trạm phát sóng, AT+CLBS / sAPI_LocGet) khi mất GPS. Chỉ hỏi
 * khi đang có phiên với Server, mất fix >= LBS_NO_FIX_WAIT_SEC và: chưa có vị
 * trí nào, hoặc xe đang chạy / ACC bật (có thể đã đi xa vị trí giữ lại). Độ
 * chính xác cỡ vài trăm mét: không cộng odometer, không đổi trạng thái chạy / dừng. */
#define LBS_ENABLE 1
#define LBS_NO_FIX_WAIT_SEC 60       // Mất fix ngần này giây mới hỏi LBS
#define LBS_INTERVAL_SEC 300         // Giữa 2 lần hỏi khi lần trước thành công
#define LBS_RETRY_SEC 120            // Giữa 2 lần hỏi khi lần trước lỗi
#define LBS_TIMEOUT_SEC 30           // Chờ kết quả tối đa (tài liệu: 9 s)
#define LBS_MAX_ACC_M 3000           // Bỏ kết quả kém chính xác hơn mức này
#define LBS_MAX_PER_DAY 120          // Tối đa số lần hỏi trong 24 giờ chạy máy

/* Watchdog phần cứng (simcom_wtd.h): task gpio_status nuôi mỗi 0,5 s; chỉ bật sau
 * khi NASA_Init xong (quét flash backup lúc khởi động có thể lâu) */
#define HW_WDT_ENABLE 1
#define HW_WDT_PERIOD_REG 0x04       // Chu kỳ watchdog phần cứng: 0x04 = 16 giây (theo demo SDK)
#define GNSS_NO_URC_RESTART_SEC 60   // Không có URC GNSS nào ngần này giây thì tắt / bật lại GNSS
#define GNSS_TASK_HANG_SEC 120       // gnss_task không chạy vòng nào ngần này giây = treo -> ngừng nuôi watchdog

/* Server im: không nhận được byte nào từ Server ngần này giây thì ngắt và kết
 * nối lại (Server chỉ trả ACK đăng nhập; kết nối TCP nửa mở vẫn gửi được vào
 * bộ đệm nên không báo lỗi). 0 = tắt */
#define SERVER_SILENT_RECONNECT_SEC 0

/* Ngủ sâu tiết kiệm ắc quy xe (app_nasa.c, SleepPoll / SleepDeepStep): ACC tắt và xe
 * đứng yên liên tục quá thời gian chờ thì gửi bản tin cuối, ngắt Server, tắt sóng
 * (CFUN=4), tắt GNSS, tắt LED, dừng mọi tác vụ và cho module vào sleep
 * (sAPI_SystemSleepSet). Lúc ngủ chỉ còn nasa_reporter thức mỗi SLEEP_POLL_SEC giây
 * để đọc chân ACC, ngắt cảm biến gia tốc và nuôi watchdog; các task khác chờ
 * SLEEP_IDLE_TASK_SEC giây / lần hoặc tới khi được đánh thức. Chỉ ACC bật hoặc
 * rung (LIS3DSH) đánh thức; SMS / lệnh Server không tới được vì sóng tắt. Chỉ ngủ
 * khi tin được dây ACC (accMode 1, hoặc 2 và dây đã từng báo ON).
 * Bật / tắt và thời gian chờ chỉnh được bằng lệnh 42. */
#define SLEEP_ENABLE_DEFAULT 1
#define SLEEP_DELAY_SEC_DEFAULT 1800       // ACC tắt + đứng yên 30 phút thì ngủ
#define SLEEP_DELAY_SEC_MIN 60
#define SLEEP_DELAY_SEC_MAX 86400
/* Chu kỳ thức định kỳ: không còn dùng (ngủ sâu không thức định kỳ), chỉ giữ để
 * trường sleepWakeSec / khóa slpw trong cấu hình đã lưu vẫn đọc được */
#define SLEEP_WAKE_PERIOD_SEC_DEFAULT 3600
#define SLEEP_WAKE_PERIOD_SEC_MIN 300
#define SLEEP_WAKE_PERIOD_SEC_MAX 86400
#define SLEEP_EVENT_HOLD_SEC 120           // Thức vì rung / ngắt ACC giả: thức ít nhất ngần này giây rồi mới ngủ lại
#define SLEEP_POLL_SEC 8                   // Đang ngủ: nasa_reporter thức mỗi 8 s đọc ACC / cảm biến, nuôi watchdog phần cứng (16 s)
#define SLEEP_IDLE_TASK_SEC 60             // Đang ngủ: gpio_status, gnss_task, SmsRecvTask, agps, ntp chờ ngần này giây / lần (thức sớm khi được đánh thức)
#define SLEEP_ACC_CONFIRM_TICKS 20         // Chân ACC mức bật: đọc lại sau 100 ms cho chắc rồi mới thức
#define SLEEP_STATUS_LOG_SEC 600           // Đang ngủ: log cờ sleep / USB / điện áp mỗi ngần này giây (đối chiếu khi đo dòng). 0 = tắt
/* Ắc quy yếu (đo khi ACC tắt, lúc thức): dưới ngưỡng SLEEP_LOW_BATT_CONFIRM lần
 * liên tiếp thì ngủ sau SLEEP_LOW_BATT_DELAY_SEC thay vì chờ đủ thời gian cấu
 * hình. Hết yếu khi lên trên ngưỡng + 0,3 V. 0 = tắt. Ngưỡng cho xe 12 V; xe
 * 24 V không bao giờ chạm. */
#define SLEEP_LOW_BATT_VOLT 11.0f
#define SLEEP_LOW_BATT_HYST_VOLT 0.3f
#define SLEEP_LOW_BATT_CONFIRM 3
#define SLEEP_LOW_BATT_DELAY_SEC 60

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
#define NASA_POS_FILE "C:/rv26_pos.dat"  /* vị trí hợp lệ cuối, đọc lại sau khi khởi động */
#define NASA_BACKUP_INTERVAL_SEC 10
#define NASA_BACKUP_MAX_RECORDS 2000
#define NASA_BACKUP_REPLAY_BURST 5 

/*
 * GPIO board RV26 trên A7677S — pad từ A7670C_MANS_LANS_GPIO.h (cùng họ
 * ASR1606). SC_MODULE_GPIO_00 chỉ có khi FEATURE_SIMCOM_NORI; dùng pad số cố
 * định. Đổi khi schematic PCB khác.
 *
 * Theo schematic_TRACKING (KiCad, 15/06/2026):
 *  - LED_GPS  pin4  (GPIO_00, pad 80) / LED_MEM pin5 (GPIO_01, pad 32) / LED_NET pin7 (GPIO_03, pad 74)
 *  - ACC      pin52 (GPIO_09, pad 28): Q5 C1815 đảo, kéo lên 10K về 1V8 -> mức thấp = ACC bật
 *  - GPIO_OUT pin48 (GPIO_08, pad 77): Q4 C1815 hở collector ra J2 chân 4 (mức cao = đóng)
 *  - GSENSOR_INT pin36 (GPIO_14, pad 12) = INT1 LIS3DSH
 * CS flash ở pin12 (pad 17) — không được dùng chung với LED/DOUT.
 */
#ifndef GPIO_LED_NET
#define GPIO_LED_NET 80 /* SC_MODULE_GPIO_00 / pin7 (LED_NET) */
#endif
#ifndef GPIO_LED_PWR
#define GPIO_LED_PWR 32 /* SC_MODULE_GPIO_01 / pin5 (LED_MEM) */
#endif
#ifndef GPIO_LED_GNSS
#define GPIO_LED_GNSS 74/* SC_MODULE_GPIO_03 / pin4 (LED_GPS) */
#endif
#ifndef GPIO_DOUT
#define GPIO_DOUT 28 /* SC_MODULE_GPIO_09 / pin48 (GPIO_OUT) */
#endif
#ifndef GPIO_ACC_IN
#define GPIO_ACC_IN 37 /* SC_MODULE_GPIO_10 / pin52 (ACC) */
#endif

/*
 * SPI flash rời EN25QH64A trên A7677S MANS (SC_SPI_1606_PIN):
 *  - SPI channel 0 = SSP0 data (da xac nhan tren board: JEDEC ID EF 40 17): pin11 CLK (pad16), pin13 MOSI (pad19), pin14 MISO (pad18)
 *  - CS# Flash: pin12 (pad 17, GPIO_35/SSP0_CS), driver tự lái bằng GPIO.
 *    Mức chọn chip FLASH_SPI_CS_ACTIVE_LEVEL được thử trước; EN25_Init vẫn thử
 *    mức ngược lại để chẩn đoán (board cũ: pin19 qua NPN C1815 đảo logic, mức 1)
 */
#ifndef FLASH_SPI_INDEX
#define FLASH_SPI_INDEX 0
#endif
#ifndef FLASH_SPI_CS_PIN
#define FLASH_SPI_CS_PIN 17 /* SC_MODULE_GPIO_35 / pin12 */
#endif
#ifndef FLASH_SPI_CS_ACTIVE_LEVEL
#define FLASH_SPI_CS_ACTIVE_LEVEL 0 /* nối thẳng CS# (active low) */
#endif

/* INT1 của LIS3DSH: GSENSOR_INT pin36 (pad 12). Đặt -1 nếu board không nối INT1 */
#ifndef LIS3DSH_INT1_PIN
#define LIS3DSH_INT1_PIN 12 /* SC_MODULE_GPIO_14 / pin36 */
#endif

#if (GPIO_LED_PWR == FLASH_SPI_CS_PIN) || (GPIO_LED_NET == FLASH_SPI_CS_PIN) || \
    (GPIO_LED_GNSS == FLASH_SPI_CS_PIN) || (GPIO_DOUT == FLASH_SPI_CS_PIN) || \
    (GPIO_ACC_IN == FLASH_SPI_CS_PIN) || (LIS3DSH_INT1_PIN == FLASH_SPI_CS_PIN)
#error "FLASH_SPI_CS_PIN xung đột với GPIO LED/DOUT/ACC/INT1 — Flash CS phải chân riêng"
#endif
#if (LIS3DSH_INT1_PIN >= 0) && ((GPIO_LED_PWR == LIS3DSH_INT1_PIN) || (GPIO_LED_NET == LIS3DSH_INT1_PIN) || \
    (GPIO_LED_GNSS == LIS3DSH_INT1_PIN) || (GPIO_DOUT == LIS3DSH_INT1_PIN) || (GPIO_ACC_IN == LIS3DSH_INT1_PIN))
#error "LIS3DSH_INT1_PIN xung đột với GPIO LED/DOUT/ACC"
#endif

/*
 * Cảm biến gia tốc LIS3DSH qua I2C (drv_lis3dsh.c). CS nối Vdd_IO (chọn I2C);
 * driver tự dò địa chỉ 0x1E (SEL = GND, board RV26) rồi 0x1D (SEL = Vdd). Vdd_IO phải cùng
 * mức I/O của module (1.8 V), điện trở kéo lên SCL/SDA về Vdd_IO.
 *  - Kênh 0 = CI2C (pin37 SDA / pin38 SCL trên họ A767x) — đã xác nhận trên board
 *  - Không có cảm biến / lỗi: firmware chạy như cũ
 */
#ifndef LIS3DSH_I2C_CHANNEL
#define LIS3DSH_I2C_CHANNEL 0
#endif
#ifndef LIS3DSH_ODR
#define LIS3DSH_ODR LIS3DSH_ODR_25HZ // ~ vài chục µA; đủ cho phát hiện rung / di chuyển
#endif
#define ACCEL_LOG_SEC 10 // Đang thức: log X/Y/Z (mg) mỗi ngần này giây để kiểm tra lắp đặt. 0 = tắt
/* Kiểm tra sức khỏe LIS3DSH lúc thức (app_nasa.c AccelHealthPoll, lệnh na,44):
 * WHO_AM_I, CTRL_REG4 còn đúng (mất = chip bị reset do sụt áp), dữ liệu không
 * đứng yên, |g| hợp lý. Lỗi -> khởi tạo lại; mất cảm biến -> thử lại định kỳ. */
#define ACCEL_HEALTH_SEC 30   // chu kỳ kiểm tra (giây). 0 = tắt
#define ACCEL_REINIT_SEC 300  // không có cảm biến: thử khởi tạo lại mỗi ngần này giây
#define ACCEL_FAIL_MAX 3      // số lần liên tiếp lỗi I2C / sai WHO_AM_I thì coi là hỏng
#define ACCEL_STUCK_MAX 10    // số lần liên tiếp X/Y/Z thô y hệt nhau thì coi là treo dữ liệu
#define ACCEL_G_MIN_MG 400    // |g| ngoài [MIN, MAX] liên tiếp ACCEL_BAD_G_MAX lần -> lỗi
#define ACCEL_G_MAX_MG 1800
#define ACCEL_BAD_G_MAX 5
/* Đang ngủ: thức khi rung / bị dời. Lúc vào ngủ nạp SM1 với ngưỡng = trục lớn
 * nhất lúc đứng yên + MOTION_WAKE_DELTA_MG; INT1 chốt đến khi SleepPoll đọc
 * OUTS1 (mỗi vòng ngủ 5 s nên một lần rung ngắn chỉ tính 1 ngắt). Đủ
 * MOTION_WAKE_EVENTS ngắt trong MOTION_WAKE_WINDOW_SEC giây thì thức hẳn —
 * đóng cửa / xe khác chạy qua chỉ 1 ngắt, bị kéo / chở thì rung liên tục. */
/* Ba giá trị dưới là mặc định, chỉnh được bằng lệnh 43 (lưu cấu hình) */
#define MOTION_WAKE_DELTA_MG 150
#define MOTION_WAKE_DELTA_MG_MIN 30        // dưới mức này nhiễu cảm biến + rung nền dễ báo giả
#define MOTION_WAKE_DELTA_MG_MAX 900       // ngưỡng tối đa 127 LSB x 15,6 mg ~ 1980 mg tính cả trọng lực
#define MOTION_WAKE_EVENTS 3
#define MOTION_WAKE_EVENTS_MIN 1
#define MOTION_WAKE_EVENTS_MAX 20
#define MOTION_WAKE_WINDOW_SEC 60
#define MOTION_WAKE_WINDOW_SEC_MIN 10
#define MOTION_WAKE_WINDOW_SEC_MAX 600
/* Nghiêng lúc ngủ: mỗi SLEEP_POLL_SEC đọc véc-tơ trọng lực, lệch so với lúc vào
 * ngủ quá ngần này độ (đọc lại sau 1 s vẫn lệch) thì thức — bắt xe bị cẩu / kéo
 * êm không đủ rung. 0 = tắt. Đi theo bật / tắt thức khi rung (lệnh 43) */
#define MOTION_TILT_DEG 8
#define MOTION_TILT_SAMPLES 4              // số mẫu trung bình mỗi lần đọc (25 Hz: 160 ms)
/* Xe di chuyển khi tắt ACC (bị đẩy / kéo / chở): dây ACC tắt (accMode 1, hoặc 2
 * đã từng thấy dây bật) mà vận tốc GPS (fix thật) > ngưỡng lệnh 10 liên tục
 * NOACC_MOVE_CONFIRM_SEC giây và đã rời chỗ đỗ >= NOACC_MOVE_MIN_DIST_M mét
 * (chưa có chỗ đỗ thì chờ gấp 3 lần). Khi đó: gửi !NASA,2 ngay, chu kỳ gửi như
 * lúc chạy, không ngủ; SMS cảnh báo tới số chủ xe 1, 2 (lệnh 17, 18). Hết khi
 * đứng yên NOACC_MOVE_END_SEC giây hoặc ACC bật */
#define NOACC_MOVE_ENABLE 1
#define NOACC_MOVE_CONFIRM_SEC 10
#define NOACC_MOVE_MIN_DIST_M 50
#define NOACC_MOVE_END_SEC 300
#define NOACC_MOVE_SMS_ENABLE 1
#define NOACC_MOVE_SMS_MIN_SEC 1800        // tối thiểu giữa 2 lần SMS cảnh báo (kéo xe dừng / đi nhiều lần)
#define MOTION_TEST_SEC_DEFAULT 120       // Lệnh na,43,t: thời gian thử độ nhạy lúc đang thức
#define MOTION_TEST_SEC_MAX 600

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
