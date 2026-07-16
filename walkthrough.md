# Báo cáo Phân tích & Hướng dẫn Di trú sang SDK 7677S

Tài liệu này cung cấp cái nhìn tổng quan về cấu trúc mã nguồn hiện tại của dự án giám sát hành trình **NASA Tracking (RV26)** và các lưu ý kỹ thuật cần thiết khi chuyển đổi (migrate) sang dòng module mới **SIMCom 7677S SDK**.

---

## 1. Cấu trúc Chương trình Hiện tại
Mã nguồn được cấu trúc thành các file modular hóa cao, phân chia rõ ràng giữa tầng phần cứng (GPIO/GPS/SMS/Network) và tầng nghiệp vụ (NASA Protocol/Commands/Backup):

*   [simcom_application.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/simcom_application.c) - Điểm khởi chạy (Entry point) và điều phối 3 tác vụ chính (Tasks).
*   [app_config.h](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_config.h) - Định nghĩa macro cấu hình hệ thống, chu kỳ, chân GPIO và Bitmask trạng thái.
*   [app_cfg.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_cfg.c) - Quản lý cấu hình persistent (Lưu trữ và đọc cấu hình từ Flash).
*   [app_cmd.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_cmd.c) - Bộ phân tích cú pháp lệnh GET/SET (na/sa) từ SMS và TCP Server.
*   [app_network.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_network.c) - Wrapper TCP Socket hỗ trợ non-blocking và quét dữ liệu bằng select.
*   [app_gps.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_gps.c) - Giao tiếp chip GNSS, xử lý tọa độ, tính quãng đường xe chạy (Odometer).
*   [app_sms.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_sms.c) - Phân tích cú pháp tin nhắn SMS chỉ định thực thi lệnh điều khiển.
*   [app_backup.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_backup.c) - Quản lý hàng đợi (FIFO Queue) lưu trữ bản ghi hành trình offline trong Flash.
*   [app_utils.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_utils.c) - Các hàm tiện ích hỗ trợ tính checksum, RTC, định dạng chuỗi.
*   [Makefile](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/Makefile) - Quản lý build và khai báo các file nguồn C biên dịch.

---

## 2. Hướng dẫn & Lưu ý khi Di trú sang SDK 7677S
Khi chuyển đổi nền tảng sang SDK 7677S, cần thực hiện kiểm tra và chỉnh sửa ở 5 khu vực trọng yếu sau:

### 2.1. Cấu hình GPIO (Hardware Pinout Mapping)
Chân GPIO của dòng module mới thường khác biệt hoàn toàn với dòng module cũ (`A7672S`).
*   **Vị trí sửa đổi**: [app_config.h](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_config.h#L88-L102)
*   **Chi tiết cần đối chiếu**: Tra cứu tài liệu *A7677S Hardware Design* và sơ đồ nguyên lý (Schematic) mạch thiết kế thực tế để cập nhật lại các định nghĩa chân GPIO:
    ```c
    #define RV26_GPIO_LED_GNSS    /* Chân điều khiển LED GPS */
    #define RV26_GPIO_LED_NET     /* Chân điều khiển LED mạng */
    #define RV26_GPIO_LED_PWR     /* Chân điều khiển LED nguồn */
    #define RV26_GPIO_ACC_IN      /* Chân đọc tín hiệu ACC khóa điện xe */
    #define RV26_GPIO_DOUT        /* Chân điều khiển đầu ra rơ-le ngắt nguồn */
    ```

### 2.2. Kiểm tra Đường dẫn & Kích thước Phân vùng Flash (Filesystem Path)
*   **Vị trí sửa đổi**: [app_config.h](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_config.h#L78-L79)
*   **Lưu ý**: 
    *   Hệ thống file trên module A7677S có thể sử dụng phân vùng đĩa khác như `E:/` hoặc `D:/` thay vì `C:/` mặc định của dòng A7672S. Cần kiểm tra SDK mới hỗ trợ phân vùng nào để sửa đường dẫn lưu trữ cấu hình (`NASA_CFG_FILE`) và backup (`NASA_BACKUP_FILE`).
    *   Kiểm tra kích thước còn lại của Flash để điều chỉnh số lượng bản tin lưu trữ tối đa (`NASA_BACKUP_MAX_RECORDS`), tránh tràn bộ nhớ Flash nội bộ của module.

### 2.3. Kiểm tra tính tương thích của SIMCom API (`sAPI_*`)
Mặc dù SIMCom cung cấp các hàm API thống nhất bắt đầu bằng `sAPI_`, một số hàm cụ thể có sự thay đổi giữa các dòng chipset (ví dụ: ASR platform vs. Unisoc vs. Qualcomm):
*   **GNSS/GPS API**: Kiểm tra cơ chế lấy dữ liệu GPS qua URC hoặc trực tiếp qua [app_gps.c](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_gps.c). Một số SDK yêu cầu khởi động cổng ảo GPS riêng biệt.
*   **API đo điện áp nguồn**: Kiểm tra tính chính xác của hàm đọc vbat trong [NASA_GetVoltage](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/app_nasa.c#L52). Đối chiếu xem hàm `sAPI_ReadVbat()` có bị đổi tên hoặc trả về định dạng đơn vị khác (millivolt vs microvolt) trong SDK 7677S hay không.

### 2.4. Cấu hình Toolchain và Build Script
*   **Vị trí sửa đổi**: [Makefile](file:///e:/test/NASATRACKING_Latest%20(1)/NASATRACKING/sc_app/simcom_application/Makefile) và file kịch bản build ngoài thư mục gốc (`build.bat` hoặc `build_16M.bat`).
*   **Lưu ý**:
    *   Cập nhật macro biên dịch sang dòng tương ứng với 7677S (ví dụ: đổi `SIMCOM_A7678_V1_02` thành macro thích hợp cho 7677S).
    *   Cấu hình lại thư viện liên kết tĩnh `simcom_lib` của A7677S trong `Makefile`.

---

## 3. Kế hoạch xác minh sau di trú (Verification Plan)
Sau khi port code và build thành công firmware cho module 7677S, cần tiến hành các bước kiểm thử sau:

1.  **Kiểm tra Khởi động & Nhận diện phần cứng**:
    *   Xem log qua cổng Debug xem thiết bị có đọc được đúng IMEI, ICCID và nạp thành công cấu hình persistent không.
    *   Bật tắt ACC vật lý và đo phản hồi LED trạng thái.
2.  **Kiểm tra Kết nối & Đăng nhập Server**:
    *   Xác nhận PDP context được kích hoạt thành công.
    *   Theo dõi log trao đổi gói tin: Thiết bị gửi Login Frame (Type 1), nhận đúng ACK `!NASA,1,...` và chuyển trạng thái sang `TRACKING`.
3.  **Kiểm tra tính năng lưu/phát lại dữ liệu (Backup Ring-buffer)**:
    *   Rút anten 4G hoặc cấu hình sai Server để thiết bị mất mạng.
    *   Sau 1-2 phút, cắm lại anten/sửa cấu hình và xác nhận thiết bị gửi loạt tin Type 7 (Replay Burst) lên server thành công.
