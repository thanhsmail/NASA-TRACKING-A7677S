# NASA Tracking — A7677S

Firmware giám sát hành trình chạy trên module 4G SIMCom **A7677S** (ASR1606, OpenSDK V702).
Thiết bị đọc GNSS, gửi vị trí lên Server theo giao thức `!NASA`, nhận lệnh cấu hình qua SMS hoặc Server, lưu bù dữ liệu khi mất mạng, tự cập nhật firmware từ xa (FOTA), ngủ tiết kiệm ắc quy khi xe đỗ và dùng cảm biến gia tốc LIS3DSH để phát hiện xe bị rung / dời lúc đang ngủ.

- Tên thiết bị: `RV4G` (`NASA_DEVICE_NAME`, xuất hiện ở đầu câu trả lời lệnh `sa,29`)
- Phiên bản firmware: `v1.05.261014` (`NASA_FW_CODE` trong [app_config.h](app_config.h))
- Server mặc định: `103.57.209.16:2590`

Tài liệu chi tiết hơn: [NASA_Tracking_Analysis.md](NASA_Tracking_Analysis.md) (phân tích logic) và [hal/README.md](hal/README.md) (lớp HAL).

## 1. Phân lớp

Code ứng dụng không gọi SDK SIMCom trực tiếp. Mọi truy cập phần cứng đi qua lớp HAL, nên phần quy tắc nghiệp vụ chạy được unit test trên PC.

```text
┌──────────────────────────────────────────────────────────────┐
│ ỨNG DỤNG   sc_application.c, app_*.c, drv_en25qh64a.c,       │
│            drv_lis3dsh.c                                     │
├──────────────────────────────────────────────────────────────┤
│ HAL        hal/hal_*.h  (os, net, gnss, gpio, adc, spi, i2c, │
│            fs, sms, fota, log)                               │
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
| [app_nasa.c](app_nasa.c) | Máy trạng thái kết nối và báo cáo, đóng gói bản tin `!NASA`, watchdog, chế độ ngủ (`SleepPoll`, mục 11) |
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
| [drv_lis3dsh.c](drv_lis3dsh.c) | Driver cảm biến gia tốc LIS3DSH qua I2C: đọc X/Y/Z, self-test, nạp state machine phát hiện chuyển động (mục 12) |
| [app_config.h](app_config.h) | Toàn bộ hằng số: chu kỳ, ngưỡng, chân GPIO, phiên bản |

## 3. Khởi động

`userSpace_Main()` chạy một lần rồi kết thúc, theo thứ tự:

1. `ApiMapInit()`, chờ `APP_STARTUP_DELAY_SEC` (5 giây).
2. `EN25_Init()` → `LIS3DSH_Init()` + self-test (không có cảm biến thì bỏ qua) → `CFG_Init()` → `GPS_Init()` → `SMS_Init()`.
3. `HAL_NET_Init()`, bật NITZ, khởi động task nghe GNSS.
4. Tạo các task `gpio_status`, `nasa_reporter`, `SmsRecvTask`.

## 4. Các task đang chạy

| Task | Ưu tiên | Stack | Tạo ở | Công việc |
|---|---|---|---|---|
| `nasa_reporter` | 15 | 10 KB | [sc_application.c](sc_application.c) | Lặp `NASA_RunStep()`: kết nối, đăng nhập, gửi vị trí, nhận lệnh Server |
| `SmsRecvTask` | 18 | 8 KB | [sc_application.c](sc_application.c) | Chờ SMS mới, đọc, thực thi lệnh, xóa SMS |
| `gpio_status` | 20 | 4 KB | [app_indication.c](app_indication.c) | Chu kỳ 500 ms (6 giây khi ngủ): lọc ACC, LED, DOUT, nuôi watchdog phần cứng |
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
| `!NASA,6` | Dừng đỗ (6-1 bắt đầu, 6-2 đang đỗ, 6-3 kết thúc). Khi thiết bị ngủ, 6-2 chỉ gửi ở mỗi lần thức định kỳ |
| `!NASA,7` | Dữ liệu phát bù từ backup |

Trả lời lệnh từ Server là một dòng văn bản thường kết thúc bằng CR LF (không phải bản tin `!NASA`).

## 7. Lệnh điều khiển

Lệnh đến từ SMS hoặc Server đều vào `CMD_Execute()` trong [app_cmd.c](app_cmd.c). Cú pháp: `na,<mã>,<tham số>` để đặt, `sa,<mã>` để đọc.

| Mã | Chức năng | Mã | Chức năng |
|---|---|---|---|
| 1 | IP và cổng Server | 25 | Tên lái xe, số GPLX |
| 2 | Ngõ ra DOUT (rơ-le) | 26 | Chu kỳ báo dừng đỗ |
| 3 | Hệ vệ tinh: 1 GPS+BeiDou+QZSS (mặc định), 2 BeiDou, 3 GPS+QZSS | | |
| 4 | Chu kỳ gửi khi chạy / dừng | 27 | Vị trí hiện tại |
| 8 | Chế độ ACC | 29 | Thông tin thiết bị, kèm trạng thái flash và backup |
| 9 | Khởi động lại | 32 | Lịch tự reset |
| 10 | Ngưỡng tốc độ | 33 | Cập nhật firmware / đọc phiên bản |
| 11 | Chế độ im lặng (tắt LED) | 34, 35 | Xóa backup / xóa toàn bộ dữ liệu |
| 12 | Phát lại dữ liệu cũ | 37 | Chuyển tiếp SMS |
| 14 | Đăng nhập / đăng xuất lái xe | 40 | Khóa / mở khóa cấu hình |
| 17, 18 | Số điện thoại chủ xe 1, 2 | 41 | Khôi phục cài đặt gốc |
| | | 42 | Chế độ ngủ: bật / tắt, thời gian chờ, chu kỳ thức (mục 11) |

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
| `gpio_status` (hoặc cả ứng dụng) treo, hoặc `gnss_task` không chạy vòng nào 120 giây | Watchdog phần cứng 16 giây reset module (bật sau khi khởi tạo xong) |
| Không nhận được dòng GNSS nào 60 giây | Tắt / bật lại GNSS |
| Không nhận được byte nào từ Server 10 phút | Ngắt và kết nối, đăng nhập lại |
| Gọi `abort()` | Reset thiết bị |
| Đến giờ trong lịch reset (lệnh 32) | Reset thiết bị |
| SIM không sẵn sàng 2 lần hỏi liên tiếp (mỗi 5 giây), kéo dài quá 30 giây | Tắt / bật sóng (CFUN 0/1) mỗi 60 giây để module đọc lại khe SIM |
| SIM sẵn sàng trở lại, hoặc ICCID đổi | Cấu hình lại SMS, ngắt kết nối và đăng nhập lại Server với ICCID mới |

## 11. Chế độ ngủ tiết kiệm ắc quy

Code nằm ở `SleepPoll()` trong [app_nasa.c](app_nasa.c), hằng số trong [app_config.h](app_config.h) (nhóm `SLEEP_*`, `MOTION_WAKE_*`).

### Điều kiện vào ngủ

Phải thỏa tất cả, liên tục trong suốt thời gian chờ:

| Điều kiện | Ghi chú |
|---|---|
| Chế độ ngủ đang bật, thiết bị đang bật | Lệnh 42, lệnh `en` |
| Tin được dây ACC | `accMode` 1, hoặc 2 và dây ACC đã từng báo bật từ lúc khởi động. Xe không đấu ACC (`accMode` 0) không bao giờ ngủ |
| ACC tắt, GPS không báo xe chạy | Mode 2 tính cả vận tốc GPS |
| Không có FOTA | |
| Đủ thời gian chờ | Mặc định 30 phút (`SLEEP_DELAY_SEC_DEFAULT`). Ắc quy dưới 11,0 V ba lần đo liên tiếp thì chỉ chờ 60 giây |
| Đã qua 120 giây từ lần cuối có SMS, lệnh hoặc lần thức gần nhất | `SLEEP_EVENT_HOLD_SEC` |
| Đang có phiên Server thì phải phát bù hết điểm mù | |

### Khi ngủ

| Thành phần | Trạng thái |
|---|---|
| GNSS | Tắt; dữ liệu GPS giữ giá trị cuối |
| Module | Cho phép sleep hệ thống (`sAPI_SystemSleepSet`). **Module không ngủ khi đang cắm USB** |
| LED | Tắt cả 3 |
| LIS3DSH | Nạp chương trình phát hiện chuyển động, ngắt ra INT1 (mục 12) |
| Kết nối Server | Giữ để nhận lệnh. Mất kết nối thì 10 phút mới thử lại; bản tin không gửi được thì lưu điểm mù |
| Bản tin | `!NASA,2` lúc vào ngủ, ở mỗi lần thức định kỳ và lúc thức hẳn. `!NASA,6` loại 2 không theo chu kỳ 60 giây mà chỉ gửi ở mỗi lần thức định kỳ; số giây đỗ vẫn đếm liên tục |
| Vòng lặp task | `nasa_reporter`, `SmsRecvTask` 5 giây; `gpio_status` 6 giây (vẫn nuôi watchdog 16 giây); `gnss_task` 10 giây |

Mỗi 60 phút (`SLEEP_WAKE_PERIOD_SEC_DEFAULT`; ắc quy yếu thì 6 giờ), thiết bị thức định kỳ: bật GNSS chờ fix tối đa 90 giây, gửi `!NASA,2` và `!NASA,6` loại 2, rồi ngủ lại. Nếu vị trí mới cách chỗ bắt đầu ngủ quá 150 m thì thức hẳn.

### Điều kiện thức hẳn

| Nguồn | Cách phát hiện |
|---|---|
| ACC bật | Ngắt cạnh xuống chân ACC. `SleepPoll` đọc lại chân; nếu chân vẫn báo tắt thì coi là ngắt giả, ghi log và cho ngủ lại. Dự phòng: `gpio_status` đọc chân mỗi 6 giây |
| SMS / lệnh Server | Mọi lệnh `na` / `sa` |
| Rung / bị dời | LIS3DSH báo đủ 3 ngắt trong 60 giây (`MOTION_WAKE_EVENTS`, `MOTION_WAKE_WINDOW_SEC`). Một lần rung đơn (đóng cửa, xe khác chạy qua) không đủ |
| Lệch vị trí | Lần thức định kỳ thấy xe cách chỗ ngủ quá 150 m |
| Điều kiện ngủ không còn | Tắt chế độ ngủ, tắt thiết bị, FOTA, dây ACC báo bật |

Lúc đang ngủ, vận tốc GPS không được xét nữa, vì GNSS tắt hoặc vừa bật lại còn trôi. Việc phát hiện xe bị dời do cảm biến rung và kiểm tra lệch 150 m đảm nhận.

### Lệnh 42

```text
na,42,<0|1>[,<chờ phút>[,<thức mỗi phút>]]     ví dụ: na,42,1,30,60
sa,42
```

Trả lời: `sa,42 ngu tiet kiem: bat, cho 30 phut, thuc moi 60 phut, dang thuc, acquy 12.7V`.

### Log và đo dòng

| Log | Ý nghĩa |
|---|---|
| `[SLEEP] ACC off & still: sleep after 1800s` | Bắt đầu đếm thời gian chờ |
| `[SLEEP] enter: ...` | Vào ngủ |
| `[SLEEP] USB connected: module stays in full power ...` | Đang cắm USB, module sẽ không ngủ thật |
| `[SLEEP] system sleep enable ret=0 (now=1, usb=0)` | Cờ sleep hệ thống và trạng thái USB |
| `[SLEEP] asleep 600s: sleep flag=1 usb=0 V=12.6` | Mỗi 10 phút khi đang ngủ (`SLEEP_STATUS_LOG_SEC`) |
| `[SLEEP] motion #1/3 in 60s window (OUTS1=0x..)` | Một ngắt rung |
| `[SLEEP] ACC interrupt but pin 37 high (ACC off) -> ignore` | Ngắt ACC giả, ngủ lại |
| `[SLEEP] exit: <lý do> after ...s asleep` | Thức hẳn: `ACC interrupt`, `motion`, `moved`, `server command`... |

Khi đo dòng lúc ngủ phải **rút USB**, vì log qua USB giữ module thức. Kết quả đo trên board ngày 03/11/2026 ở 12 V: 0,09 A khi thức và cắm USB, 0,06 A khi ngủ. Mức 0,06 A vẫn cao hơn nhiều so với lúc module ngủ thật (vài mA). Chưa tách được phần nào thuộc module, phần nào thuộc các mạch khác trên board (nguồn, RS232, anten GPS); cần đo riêng dòng vào chân VBAT.

## 12. Cảm biến gia tốc LIS3DSH

Driver: [drv_lis3dsh.c](drv_lis3dsh.c), bus qua [hal/hal_i2c.h](hal/hal_i2c.h). Nếu không có cảm biến hoặc khởi tạo lỗi, firmware chạy như cũ, chỉ thiếu tính năng thức khi rung.

### Kết nối

| Tín hiệu | Nối | Ghi chú |
|---|---|---|
| SCL / SDA | I2C kênh 0 (CI2C, chân 38 / 37 của module) | 400 kHz, kéo lên Vdd_IO |
| CS | Vdd_IO | Chọn giao tiếp I2C |
| SEL / SDO | GND | Địa chỉ 7 bit `0x1E` (driver vẫn thử `0x1D` cho board khác) |
| INT1 | pad 120 (GPIO_05, chân 19) | Ngắt chuyển động, tích cực mức thấp, chốt |
| Vdd_IO | Cùng mức I/O của module (1,8 V) | |

### Cấu hình

| Thanh ghi | Giá trị | Ý nghĩa |
|---|---|---|
| CTRL_REG4 `0x20` | `0x4F` | ODR 25 Hz, BDU, bật X/Y/Z |
| CTRL_REG5 `0x24` | `0xC0` | ±2 g (0,06 mg/LSB), lọc chống răng cưa 50 Hz |
| CTRL_REG6 `0x25` | `0x10` | Tự tăng địa chỉ khi đọc nhiều byte |

Khởi động lần lượt:
1. Đọc `WHO_AM_I` (`0x0F`), phải ra `0x3F`.
2. Soft reset, ghi cấu hình, đọc lại CTRL_REG4 để kiểm tra.
3. Chạy self-test: tạm chuyển 100 Hz, so trung bình 10 mẫu khi bật và khi tắt self-test.

Datasheet chỉ cho giá trị điển hình (X/Y khoảng 140 mg, Z khoảng 590 mg), nên self-test chỉ ghi log `PASS` / `OUT OF RANGE`, không chặn hoạt động.

### Phát hiện chuyển động lúc ngủ

Khi vào ngủ, `LIS3DSH_MotionArm()` nạp state machine SM1:

- Chương trình: `ST1_1 = 0x05` (một trục bất kỳ có |a| > THRS1_1), `ST1_2 = 0x11` (báo ngắt rồi chạy lại). MASK = ±X ±Y ±Z, ngắt ra INT1.
- Ngưỡng tính lại mỗi lần vào ngủ để bù trọng lực theo tư thế lắp: ngưỡng = trục lớn nhất lúc đứng yên + 150 mg (`MOTION_WAKE_DELTA_MG`), đơn vị 15,6 mg/LSB.
- `SleepPoll` đọc `OUTS1` (`0x5F`) để xóa ngắt và đếm. Nếu chân INT1 không hỗ trợ đánh thức, firmware vẫn đọc mức chân mỗi vòng ngủ.
- Khi thức hẳn thì tắt SM1 (`LIS3DSH_MotionDisarm()`).

Mã lệnh state machine và đơn vị ngưỡng lấy theo ví dụ wake-up của ST (AN3393), chưa đối chiếu được với tài liệu gốc. Cần kiểm trên board: gõ nhẹ vào board phải thấy `[SLEEP] motion #1/3`.

### Log

| Log | Ý nghĩa |
|---|---|
| `[LIS3DSH] OK addr=0x1E ch=0 CTRL4=0x4F` | Khởi tạo xong |
| `[LIS3DSH] self-test PASS: dX=-174 dY=-181 dZ=464 mg` | Kết quả self-test |
| `[ACCEL] x=-700 y=323 z=619 mg` | Khi thức, mỗi 10 giây (`ACCEL_LOG_SEC`, 0 = tắt), để kiểm tra lắp đặt |
| `[LIS3DSH] motion armed: g=(-700,323,619) thr=54 (843 mg)` | Đã nạp chương trình lúc vào ngủ |

## 13. Phần cứng

| Tín hiệu | Pad | Ghi chú |
|---|---|---|
| LED mạng | 80 | Nháy khi đã đăng nhập Server, sáng khi có sóng, tắt khi mất sóng |
| LED nguồn | 32 | Trạng thái bộ nhớ backup: sáng = bộ nhớ trong (file EFS), nháy = bộ nhớ ngoài (flash SPI), tắt = cả hai lỗi |
| LED GNSS | 74 | Nháy khi có từ 5 vệ tinh |
| DOUT | 28 (GPIO_09, pin 52) | Rơ-le |
| ACC_IN | 37 (GPIO_10, chân 66) | Mức thấp = ACC bật; ngắt cạnh xuống đánh thức khi ngủ |
| CS flash | 17 (GPIO_35/SSP0_CS, chân 12) | Nối thẳng CS#: GPIO mức thấp = chọn flash (`FLASH_SPI_CS_ACTIVE_LEVEL`); driver tự thử mức ngược lại nếu không đọc được mã chip |
| INT1 LIS3DSH | 120 (GPIO_05, chân 19) | Ngắt chuyển động, đánh thức khi ngủ |
| I2C LIS3DSH | CI2C kênh 0 (chân 37 SDA, 38 SCL) | Mục 12 |
| ADC1 | — | Điện áp ắc-quy qua cầu chia 1/23 |

Flash rời 8 MB (trên board là W25Q64JV, mã `EF 40 17`) nối bus SPI ở chân 11, 13, 14 của module, qua IC chuyển mức 1,8 V ↔ 3,3 V (TC7MPB9307FT).

- Phía phần mềm là **kênh SPI 0**, mode 0, 812 kHz (`FLASH_SPI_INDEX` trong [app_config.h](app_config.h)). Kênh 1 không đọc được flash.
- Nếu kênh mặc định không trả mã chip hợp lệ, driver thử các kênh còn lại và in kết quả ra log (`[EN25/Flash] Flash Init OK! ... spi=N`).

## 14. Build và nạp

Build cho biến thể MANV từ thư mục gốc, chạy trong `cmd` hoặc PowerShell (Makefile dùng lệnh của Windows, không chạy được trong Git Bash):

```bash
.\GNUmake.exe A7677S_MANV_1606_V702_OPENSDK
```

Kết quả nằm trong `out/A7677S_MANV_1606_V702_OPENSDK/`:

- `customer_app.bin`: file dùng cho FOTA ứng dụng.
- `burn.zip`: gói nạp qua cáp bằng công cụ aboot; nên lưu lại cho từng bản phát hành vì `system.img` bên trong cần để tạo gói FOTA hệ thống.

Unit test cho hai bộ quy tắc (dừng đỗ, lái xe) nằm trong [tests/](tests/), chạy trên PC với HAL giả lập ở `tests/mocks/` (cần gcc cho PC). Test dừng đỗ có cả trường hợp đang ngủ: 6-2 chỉ ra khi được yêu cầu. Bộ đệm điểm mù, chế độ ngủ và LIS3DSH chưa có unit test; được kiểm tra trên board qua log.
