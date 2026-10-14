# NASA Tracking — A7677S

Firmware giám sát hành trình chạy trên module 4G SIMCom **A7677S** (ASR1606, OpenSDK V702).
Thiết bị đọc GNSS, gửi vị trí lên Server theo giao thức `!NASA`, nhận lệnh cấu hình qua SMS hoặc Server, lưu bù dữ liệu khi mất mạng và tự cập nhật firmware từ xa (FOTA).

- Tên thiết bị: `RV4G` (`NASA_DEVICE_NAME`, xuất hiện ở đầu câu trả lời lệnh `sa,29`)
- Phiên bản firmware: `v1.04.261013` (`NASA_FW_CODE` trong [app_config.h](app_config.h))
- Server mặc định: `103.57.209.16:2590`

Tài liệu chi tiết hơn: [NASA_Tracking_Analysis.md](NASA_Tracking_Analysis.md) (phân tích logic) và [hal/README.md](hal/README.md) (lớp HAL).

## 1. Phân lớp

Code ứng dụng không gọi SDK SIMCom trực tiếp. Mọi truy cập phần cứng đi qua lớp HAL, nên phần quy tắc nghiệp vụ chạy được unit test trên PC.

```text
┌──────────────────────────────────────────────────────────────┐
│ ỨNG DỤNG   sc_application.c, app_*.c, drv_en25qh64a.c        │
├──────────────────────────────────────────────────────────────┤
│ HAL        hal/hal_*.h  (os, net, gnss, gpio, adc, spi, fs,  │
│            sms, fota, log)                                   │
├──────────────────────────────────────────────────────────────┤
│ CÀI ĐẶT    hal/impl/hal_impl_simcom.c  → gọi sAPI_* của SDK  │
├──────────────────────────────────────────────────────────────┤
│ KERNEL     SIMCom OpenSDK (kernel/, sc_lib/)                 │
└──────────────────────────────────────────────────────────────┘
```

## 2. Các file nguồn

| File | Vai trò |
|---|---|
| [sc_application.c](sc_application.c) | Điểm vào `userSpace_Main()`: khởi tạo và tạo các task |
| [app_nasa.c](app_nasa.c) | Máy trạng thái kết nối và báo cáo, đóng gói bản tin `!NASA`, watchdog |
| [app_network.c](app_network.c) | PDP, socket tới Server, nhận dòng lệnh từ Server |
| [app_gps.c](app_gps.c) | Phân tích dữ liệu GNSS, snapshot vị trí, odometer |
| [app_park_rules.c](app_park_rules.c) | Quy tắc dừng đỗ (bản tin 6), C thuần |
| [app_lxlt_rules.c](app_lxlt_rules.c) | Quy tắc thời gian lái xe liên tục / trong ngày (bản tin 5), C thuần |
| [app_cmd.c](app_cmd.c) | Phân tích và thực thi lệnh `na,<mã>,...` / `sa,<mã>` |
| [app_sms.c](app_sms.c) | Cấu hình modem SMS, tách nội dung SMS rồi chuyển cho `app_cmd` |
| [app_cfg.c](app_cfg.c) | Cấu hình thiết bị, lưu vào file `C:/rv26.cfg` |
| [app_backup.c](app_backup.c) | Bộ đệm điểm mù: hàng đợi vị trí chưa gửi được, lưu trên flash rời (xem mục 9) |
| [app_fota.c](app_fota.c) | Cập nhật firmware từ URL (lệnh 33) |
| [app_sim.c](app_sim.c) | Phát hiện rút / cắm / đổi SIM bằng cách hỏi định kỳ (module không báo URC) |
| [app_indication.c](app_indication.c) | LED, lọc nhiễu ACC, ngõ ra DOUT, giám sát watchdog |
| [app_utils.c](app_utils.c) | Tiện ích thời gian, đồng bộ NTP |
| [drv_en25qh64a.c](drv_en25qh64a.c) | Driver flash SPI rời 8 MB (W25Q64JV / EN25QH64A), tự dò kênh SPI lúc khởi động |
| [app_config.h](app_config.h) | Toàn bộ hằng số: chu kỳ, ngưỡng, chân GPIO, phiên bản |

## 3. Khởi động

`userSpace_Main()` chạy một lần rồi kết thúc, theo thứ tự:

1. `ApiMapInit()`, chờ `APP_STARTUP_DELAY_SEC` (5 giây).
2. `EN25_Init()` → `CFG_Init()` → `GPS_Init()` → `SMS_Init()`.
3. `HAL_NET_Init()`, bật NITZ, khởi động task nghe GNSS.
4. Tạo các task `gpio_status`, `nasa_reporter`, `SmsRecvTask`.

## 4. Các task đang chạy

| Task | Ưu tiên | Stack | Tạo ở | Công việc |
|---|---|---|---|---|
| `nasa_reporter` | 15 | 10 KB | [sc_application.c](sc_application.c) | Lặp `NASA_RunStep()`: kết nối, đăng nhập, gửi vị trí, nhận lệnh Server |
| `SmsRecvTask` | 18 | 8 KB | [sc_application.c](sc_application.c) | Chờ SMS mới, đọc, thực thi lệnh, xóa SMS |
| `gpio_status` | 20 | 4 KB | [app_indication.c](app_indication.c) | Chu kỳ 100 ms: lọc ACC, LED, DOUT, kiểm tra watchdog |
| `gnss_task` | 10 | — | [hal_impl_simcom.c](hal/impl/hal_impl_simcom.c) | Nhận dữ liệu GNSS từ SDK, đẩy vào `GPS_OnUrcString()` |
| `fota_task` | 20 | 10 KB | [app_fota.c](app_fota.c) | Chỉ tạo khi có lệnh 33 lần đầu; tải và nạp firmware |
| `ntp_sync` | 130 | — | [app_utils.c](app_utils.c) | Chỉ tạo khi đồng hồ RTC chưa hợp lệ |

Chỉ task `nasa_reporter` được ghi vào socket Server. Task khác muốn gửi lên Server (ví dụ kết quả FOTA) phải để lại thông điệp cho task này lấy.

## 5. Máy trạng thái `NASA_RunStep()`

```text
            ┌────────────┐  PDP OK   ┌───────────────┐ socket OK ┌─────────────┐
   ───────► │ STATE_INIT ├──────────►│ STATE_CONNECT ├──────────►│ STATE_LOGIN │
            └─────▲──────┘           └───────┬───────┘           └──────┬──────┘
                  │                          │ lỗi                      │ nhận ACK
                  │ sau 30 giây              ▼                          ▼
            ┌─────┴─────────────┐   lỗi   ┌──────────────────────────────────┐
            │ STATE_ERROR_RETRY │◄────────┤ STATE_TRACKING (lặp mỗi 1 giây)  │
            └───────────────────┘         └──────────────────────────────────┘
```

Mỗi vòng `STATE_TRACKING` làm lần lượt:

1. `Network_RecvPoll()`: gửi kết quả FOTA đang chờ, đọc và thực thi lệnh từ Server.
2. Phát bù hàng đợi backup thành bản tin 7 (mỗi lượt tối đa 5 mục).
3. Kiểm tra đổi ngày (xóa bộ đếm ngày) và lịch tự reset (lệnh 32).
4. Lấy snapshot GPS, chạy quy tắc dừng đỗ và quy tắc lái xe.
5. Tính chu kỳ gửi: ACC tắt dùng chu kỳ dừng; ACC bật dùng chu kỳ chạy, rút ngắn khi xe đổi hướng (≥15° còn một nửa, ≥30° còn 2 giây, áp dụng từ 8 km/h).
6. Đến hạn hoặc có yêu cầu gửi ngay thì gửi bản tin; gửi lỗi thì ghi vào backup.

Khi chưa có phiên với Server (sau kích hoạt PDP, sau kết nối, trong lúc chờ ACK đăng nhập, trong `STATE_ERROR_RETRY`), thiết bị vẫn chạy quy tắc dừng đỗ / lái xe và ghi vị trí vào backup theo đúng chu kỳ ở bước 5. Bản tin 5 và 6 phát sinh lúc này không được lưu (bỏ), nhưng trạng thái phiên lái và dừng đỗ vẫn được cập nhật nên bản tin kế tiếp sau khi có mạng vẫn đúng.

## 6. Bản tin giao thức

| Mã | Ý nghĩa |
|---|---|
| `!NASA,1` | Đăng nhập |
| `!NASA,2` | Vị trí định kỳ |
| `!NASA,5` | Thời gian làm việc của lái xe (5-1 bắt đầu, 5-2 định kỳ, 5-3 kết thúc) |
| `!NASA,6` | Dừng đỗ (6-1 bắt đầu, 6-2 đang đỗ, 6-3 kết thúc) |
| `!NASA,7` | Dữ liệu phát bù từ backup |
| `!NASA,13` | Phản hồi lệnh |

## 7. Lệnh điều khiển

Lệnh đến từ SMS hoặc Server đều vào `CMD_Execute()` trong [app_cmd.c](app_cmd.c). Cú pháp: `na,<mã>,<tham số>` để đặt, `sa,<mã>` để đọc.

| Mã | Chức năng | Mã | Chức năng |
|---|---|---|---|
| 1 | IP và cổng Server | 25 | Tên lái xe, số GPLX |
| 2 | Ngõ ra DOUT (rơ-le) | 26 | Chu kỳ báo dừng đỗ |
| 4 | Chu kỳ gửi khi chạy / dừng | 27 | Vị trí hiện tại |
| 8 | Chế độ ACC | 29 | Thông tin thiết bị, kèm trạng thái flash và backup |
| 9 | Khởi động lại | 32 | Lịch tự reset |
| 10 | Ngưỡng tốc độ | 33 | Cập nhật firmware / đọc phiên bản |
| 11 | Chế độ im lặng (tắt LED) | 34, 35 | Xóa backup / xóa toàn bộ dữ liệu |
| 12 | Phát lại dữ liệu cũ | 37 | Chuyển tiếp SMS |
| 14 | Đăng nhập / đăng xuất lái xe | 40 | Khóa / mở khóa cấu hình |
| 17, 18 | Số điện thoại chủ xe 1, 2 | 41 | Khôi phục cài đặt gốc |

## 8. Cập nhật firmware (lệnh 33)

`na,33,<URL>` trả lời ngay `dang update firmware`, sau đó `fota_task` chạy nền và báo kết quả về nguồn gửi lệnh (SMS hoặc Server).

| Tên file cuối URL | Loại | Cách nạp |
|---|---|---|
| `customer_app.bin` | Ứng dụng | App tự tải (HTTP/HTTPS/FTP), kiểm CRC, reset để bootloader nạp |
| Tên khác, ví dụ `system_patch.bin` | Hệ thống | SDK tải gói vi sai tạo bằng `adiff.exe`, đạt 100% thì reset để nạp |

Mỗi lần lỗi sẽ thử lại, tối đa 3 lần, cách nhau 10 giây. Lỗi thì firmware đang chạy được giữ nguyên.

## 9. Bộ đệm điểm mù (backup)

Dữ liệu chưa gửi được nằm trên flash rời và không mất khi reset hay mất nguồn.

Bố cục flash (sector 4 KB):

| Sector | Nội dung |
|---|---|
| 0, 1 | Cấu hình thiết bị (bản chính và bản sao) |
| 2, 3 | Nhật ký header của hàng đợi, 256 ô, hai sector dùng luân phiên |
| 4 trở đi | Vòng bản ghi: 65.408 ô, mỗi ô 128 byte |

Cách hoạt động:

- Mỗi ô chứa dữ liệu, số thứ tự và CRC32. Ô không qua được CRC bị bỏ qua khi phát bù, không gửi lên Server.
- Header chỉ là điểm mốc, lưu sau mỗi 16 bản ghi và sau mỗi lượt phát bù. Khi khởi động, thiết bị dò tiếp từ điểm mốc theo số thứ tự để nhận các bản ghi ghi sau đó.
- Trước khi ghi, ô được kiểm tra còn trống; sau khi ghi, đọc lại để so. Ô hỏng bị bỏ, bản ghi ghi sang ô kế.
- Chỉ lưu bản tin vị trí. Bản tin 5 và 6 hiện không lưu. Code vẫn còn khả năng lưu chúng thành các mảnh 92 byte trong cùng vòng (`Backup_PushText`), và vẫn phát bù được các mảnh còn sót trên flash từ bản firmware trước.
- Khi vòng đầy, sector cũ nhất (32 ô) bị xóa để ghi tiếp.
- Mất header ở cả hai sector thì hàng đợi được dựng lại bằng cách quét các ô.

### Dung lượng lưu

Mỗi bản tin vị trí chiếm 1 ô. Chu kỳ lưu khi mất mạng giống chu kỳ gửi trực tiếp: 10 giây khi ACC bật (2 giây khi vào cua), 300 giây khi ACC tắt.

| Tình huống mất mạng | Ô / phút | Thời gian tới khi đầy |
|---|---|---|
| Xe chạy liên tục, ACC bật | khoảng 6 | khoảng 7,5 ngày |
| Xe đỗ, ACC tắt | 0,2 | hơn 200 ngày |
| Một ngày 10 giờ chạy + 14 giờ đỗ | khoảng 3.770 ô/ngày | khoảng 17 ngày |

Vào cua nhiều thì số ô khi chạy tăng. Khi đầy, dữ liệu mới nhất luôn được giữ, phần cũ nhất bị ghi đè theo từng sector 32 ô.

Nếu flash rời không khởi tạo được, thiết bị lưu tạm vào file `C:/rv26_bak.dat` trong module, tối đa 2.000 ô (khoảng 5,5 giờ xe chạy hoặc gần 7 ngày xe đỗ); header lưu sau mỗi bản ghi.

Kiểm tra nhanh bằng lệnh `sa,29`; cuối câu trả lời có `FL:1 BK:F12 ERR:0`:

| Trường | Ý nghĩa |
|---|---|
| `FL` | Flash rời có nhận không (1 / 0) |
| `BK` | Nơi lưu (`F` = flash, `E` = file) và số ô đang chờ phát bù |
| `ERR` | Số lần ghi flash lỗi từ lúc khởi động |

Dữ liệu chưa gửi ở định dạng của firmware cũ (trước bản có CRC) bị bỏ khi nâng cấp.

## 10. Cơ chế tự phục hồi

| Điều kiện | Hành động |
|---|---|
| `nasa_reporter` không chạy vòng nào trong 180 giây | `gpio_status` reset thiết bị |
| Mất kết nối Server liên tục 15 phút | Reset thiết bị |
| Gọi `abort()` | Reset thiết bị |
| Đến giờ trong lịch reset (lệnh 32) | Reset thiết bị |
| SIM không sẵn sàng 2 lần hỏi liên tiếp (mỗi 5 giây), kéo dài quá 30 giây | Tắt / bật sóng (CFUN 0/1) mỗi 60 giây để module đọc lại khe SIM |
| SIM sẵn sàng trở lại, hoặc ICCID đổi | Cấu hình lại SMS, ngắt kết nối và đăng nhập lại Server với ICCID mới |

## 11. Phần cứng

| Tín hiệu | Pad | Ghi chú |
|---|---|---|
| LED mạng | 80 | Nháy khi đã đăng nhập Server, sáng khi có sóng, tắt khi mất sóng |
| LED nguồn | 32 | |
| LED GNSS | 74 | Nháy khi có từ 5 vệ tinh |
| DOUT | 73 | Rơ-le |
| ACC_IN | 37 | Mức thấp = ACC bật |
| CS flash | 120 (chân 19) | Qua transistor đảo: GPIO mức cao = chọn flash; không dùng chung với chân khác |
| ADC1 | — | Điện áp ắc-quy qua cầu chia 1/23 |

Flash rời 8 MB (trên board là W25Q64JV, mã `EF 40 17`) nối bus SPI ở chân 11, 13, 14 của module, qua IC chuyển mức 1,8 V ↔ 3,3 V (TC7MPB9307FT).

- Phía phần mềm là **kênh SPI 0**, mode 0, 812 kHz (`FLASH_SPI_INDEX` trong [app_config.h](app_config.h)). Kênh 1 không đọc được flash.
- Nếu kênh mặc định không trả mã chip hợp lệ, driver thử các kênh còn lại và in kết quả ra log (`[EN25/Flash] Flash Init OK! ... spi=N`).

## 12. Build và nạp

Build cho biến thể MANV từ thư mục gốc, chạy trong `cmd` hoặc PowerShell (Makefile dùng lệnh của Windows, không chạy được trong Git Bash):

```bash
.\GNUmake.exe A7677S_MANV_1606_V702_OPENSDK
```

Kết quả nằm trong `out/A7677S_MANV_1606_V702_OPENSDK/`:

- `customer_app.bin`: file dùng cho FOTA ứng dụng.
- `burn.zip`: gói nạp qua cáp bằng công cụ aboot; nên lưu lại cho từng bản phát hành vì `system.img` bên trong cần để tạo gói FOTA hệ thống.

Unit test cho hai bộ quy tắc (dừng đỗ, lái xe) nằm trong [tests/](tests/), chạy trên PC với HAL giả lập ở `tests/mocks/`. Bộ đệm điểm mù chưa có unit test; được kiểm tra trên board qua log.
