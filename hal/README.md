# HAL (Hardware Abstraction Layer) Architecture & Portability Guide

Hệ thống **HAL Abstraction Layer** giúp ứng dụng **NASA Tracking** tách biệt 100% mã nguồn ứng dụng (`app_*.c`, `sc_application.c`, `drv_en25qh64a.c`) khỏi SDK của nhà sản xuất phần cứng (SIMCom OpenSDK).

---

## 🏛️ Kiến Trúc Hệ Thống (Architecture)

```
+-------------------------------------------------------------------+
|                     APPLICATION LAYER (Logic)                     |
|                                                                   |
|   sc_application.c | app_nasa.c | app_gps.c | app_cfg.c | ...      |
|   drv_en25qh64a.c  | app_network.c | app_sms.c | app_backup.c     |
+-------------------------------------------------------------------+
                                  |
                                  | #include "hal/hal_*.h"
                                  v
+-------------------------------------------------------------------+
|                        HAL CONTRACT LAYER                         |
|                                                                   |
|  hal_log.h | hal_os.h  | hal_gpio.h | hal_net.h                   |
|  hal_gnss.h| hal_spi.h | hal_fs.h   | hal_sms.h                   |
|  hal_adc.h                                                        |
+-------------------------------------------------------------------+
                                  |
               +------------------+------------------+
               | (Giao diện chuẩn / Non-SDK)         |
               v                                     v
+-----------------------------+     +-------------------------------+
|  hal/impl/hal_impl_simcom.c |     |  hal/impl/hal_impl_quectel.c  |
|  (SIMCom A7677S SDK Calls)  |     |  (Quectel OpenCPU Calls)      |
+-----------------------------+     +-------------------------------+
```

---

## 📋 Danh Sách Module API HAL

| Interface Header | Chức Năng Bọc | Trách Nhiệm |
|---|---|---|
| `hal/hal_log.h` | Log Debug | Bọc hàm in log qua UART debug (`HAL_LOG`) |
| `hal/hal_os.h` | RTOS Core | Tạo Task, Delay, Mutex, Reset hệ thống, Ticks |
| `hal/hal_gpio.h` | Digital I/O | Cấu hình Input/Output, Đọc/Ghi trạng thái Pin, Pull-up/down |
| `hal/hal_net.h` | TCP & Cell Network | PDP Activation, TCP Connect/Send/Recv non-blocking, CSQ, IMEI/ICCID |
| `hal/hal_gnss.h` | GNSS/GPS | Đọc RTC, Bật/Tắt GNSS, Lắng nghe URC định vị qua Callback |
| `hal/hal_spi.h` | SPI Bus | Cấu hình SPI, Truyền/Nhận buffer cho Flash NOR |
| `hal/hal_fs.h` | File System | Đọc/Ghi/Xóa file trên bộ nhớ Flash nội (EFS) |
| `hal/hal_sms.h` | Short Message | Cấu hình CNMI, Đọc/Xóa/Gửi SMS, Nhận URC SMS |
| `hal/hal_adc.h` | ADC | Đọc điện áp thực tế tại chân ADC (mV), ví dụ ADC1/ADC.PWR |

---

## 🔄 Hướng Dẫn Chi Tiết Khi Chuyển Sang Chip Mới (Porting Guide)

Khi chuyển từ **SIMCom A7677S** sang bất kỳ dòng Chip / Modem khác (ví dụ: Quectel EC600S, Fibocom L610, ESP32, STM32 Cellular...):

### ⚠️ QUY TẮC VÀNG:
1. **KHÔNG** chỉnh sửa bất kỳ file `hal/*.h` nào (trừ khi bổ sung API mới cho toàn bộ dự án).
2. **KHÔNG** chỉnh sửa bất kỳ file ứng dụng `app_*.c` hay `sc_application.c` nào.

### 📝 BƯỚC 1: Tạo File Implement Cho Chip Mới
Tạo file mới trong folder `hal/impl/`, ví dụ:
```bash
hal/impl/hal_impl_quectel.c
```

### 📝 BƯỚC 2: Triển Khai Các Hàm Trong Contract
Trong file `hal_impl_<chip_moi>.c`, `#include` các header của Chip mới (ví dụ: `ql_api_*.h`) và implement lại toàn bộ các hàm thuộc contract `hal/*.h`:

```c
#include "ql_api_osi.h"   /* SDK Quectel */
#include "ql_api_nw.h"
#include "hal_log.h"
#include "hal_os.h"
/* ... include cac hal_*.h ... */

void HAL_LOG_Print(const char *fmt, ...) {
    /* Implement qua va_list va QL_LOG */
}

int HAL_OS_TaskCreate(HalTaskRef_t *ref, void *stack, uint32_t stackSize,
                      int priority, const char *name,
                      void (*fn)(void *), void *arg) {
    /* Implement qua ql_rtos_task_create */
}
```

### 📝 BƯỚC 3: Cập Nhật Build System
Trong `CMakeLists.txt` hoặc `Makefile`, thay thế file `hal_impl_simcom.c` bằng file triển khai mới:

```cmake
# CMakeLists.txt
SET(app_src
    sc_application.c
    drv_en25qh64a.c
    app_nasa.c
    app_network.c
    app_gps.c
    app_sms.c
    app_cfg.c
    app_backup.c
    app_utils.c
    # Doi sang file impl cua chip moi tai day:
    hal/impl/hal_impl_quectel.c
)
```

---

## 👥 Quy Trình Làm Việc Nhóm (Parallel Teamwork Workflow)

- **Lập trình viên Ứng dụng (App Developer):**
  - Chỉ quan tâm đến logic nghiệp vụ (`app_nasa.c`, `app_gps.c`...) và gọi hàm `HAL_*`.
  - Có thể lập trình logic ngay cả khi chưa có bo mạch thật bằng cách stub/mock các hàm `HAL_*`.
- **Lập trình viên Phần cứng (Driver / Hardware Developer):**
  - Chỉ làm việc trong thư mục `hal/impl/`.
  - Cấu hình chân GPIO, tinh chỉnh SPI clock, tối ưu PDP/Socket theo khuyến cáo của hãng sản xuất chip mà không làm ảnh hưởng đến mã nguồn ứng dụng.
