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
| `gpio_status` | 20 | 4 KB | [app_indication.c](app_indication.c) | Chu kỳ 500 ms: lọc ACC, LED, DOUT, nuôi watchdog phần cứng. Khi ngủ chỉ chờ (tối đa 60 giây / lần) |
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
5. Tính chu kỳ gửi: ACC tắt dùng chu kỳ dừng; ACC bật dùng chu kỳ chạy, rút ngắn khi xe đổi hướng (≥15° còn một nửa, ≥30° còn 2 giây, áp dụng từ 8 km/h). Khi có LIS3DSH, gia tốc ngang xác nhận xe đang vào cua sẽ rút ngắn chu kỳ ngay từ đầu khúc cua (mục 12).
6. Đến hạn hoặc có yêu cầu gửi ngay thì gửi bản tin; gửi lỗi thì ghi vào backup.

Khi chưa có phiên với Server (sau kích hoạt PDP, sau kết nối, trong lúc chờ ACK đăng nhập, trong `STATE_ERROR_RETRY`), thiết bị vẫn chạy quy tắc dừng đỗ / lái xe và ghi vị trí vào backup theo đúng chu kỳ ở bước 5. Bản tin 5 và 6 phát sinh lúc này không được lưu (bỏ), nhưng trạng thái phiên lái và dừng đỗ vẫn được cập nhật nên bản tin kế tiếp sau khi có mạng vẫn đúng.

## 6. Bản tin giao thức

| Mã | Ý nghĩa |
|---|---|
| `!NASA,1` | Đăng nhập |
| `!NASA,2` | Vị trí định kỳ |
| `!NASA,5` | Thời gian làm việc của lái xe (5-1 bắt đầu, 5-2 định kỳ, 5-3 kết thúc) |
| `!NASA,6` | Dừng đỗ (6-1 bắt đầu, 6-2 đang đỗ, 6-3 kết thúc). Khi thiết bị ngủ sâu thì không gửi |
| `!NASA,7` | Dữ liệu phát bù từ backup |
| `!NASA,13` | Trả lời lệnh từ Server (mục 7) |

Trả lời lệnh từ Server là bản tin `!NASA,13,<msgId>,<ngày giờ>,<nội dung>,*<checksum>` kết thúc bằng CR LF (mục 7).

## 7. Lệnh điều khiển

Lệnh đến từ SMS hoặc Server đều vào `CMD_Execute()` trong [app_cmd.c](app_cmd.c). Gửi lệnh nào cũng làm thiết bị thức khỏi chế độ ngủ để xử lý và trả lời.

### Cú pháp

```text
na,<mã>[,<tham số 1>[,<tham số 2>...]]   đặt / thực hiện (SET)
sa,<mã>                                   đọc (GET)
```

- Các tham số cách nhau bằng dấu phẩy, không có khoảng trắng.
- Với lệnh cấu hình, `na` và `sa` đều trả lời giá trị hiện tại sau khi xử lý. Giá trị được lưu ngay vào flash SPI (2 sector) và EFS (`NASA_CFG_FILE`), nên giữ được qua khởi động lại.
- Lệnh chỉ có dạng `na` (9, 12, 34, 35, 37, 41): gửi `sa,<mã>` thì không làm gì và không trả lời.
- Thiếu tham số bắt buộc thì trả lời `ERROR`. Mã không có trong bảng thì trả lời `ERROR ma lenh khong ho tro`.
- Giá trị ngoài khoảng cho phép: các lệnh 4, 10 và 8 giữ giá trị cũ; các lệnh 26, 32, 42 và 43 kẹp về giới hạn gần nhất.

### Trả lời

| Nguồn | Dạng trả lời |
|---|---|
| SMS | Gửi nguyên văn về số đã nhắn, ví dụ `sa,10 nguong van toc: 3 km/h` |
| Server | `!NASA,13,<msgId>,<ngày giờ>,<nội dung>,*<checksum>` + CR LF; tiền tố `sa,<mã> ` được bỏ khỏi nội dung |

### Quyền (lệnh 40)

Mặc định `NASA_LOCK_FEATURE_ENABLE 0` trong [app_config.h](app_config.h): mọi nguồn đều được đổi mọi cấu hình. Khi đặt bằng 1 và cấu hình đang khóa (`na,40,close`), các lệnh 1, 17, 18, 33 (và lệnh cũ `update`) chỉ nhận từ Server hoặc từ số chủ xe 1 / 2. Nguồn khác nhận `Ban khong co quyen thuc hien chuc nang nay`. Riêng lệnh 41 thì luôn kiểm tra, kể cả khi tính năng khóa bị tắt: cấu hình đang khóa thì chỉ nhận từ Server hoặc số chủ xe 1 / 2.

### Bảng lệnh

| Mã | Chức năng | Đặt / thực hiện (`na`) | Mặc định / khoảng | Trả lời (ví dụ) |
|---|---|---|---|---|
| 1 | IP / tên miền và cổng Server | `na,1,<host>,<port>` | `103.57.209.16`, 2590; cổng 1–65535 | `sa,1 ip/port: 103.57.209.16 / 2590` |
| 2 | Ngõ ra DOUT (rơ-le) | `na,2,<1\|on\|0\|off>` | tắt | `sa,2 dieu khien ra: bat` |
| 3 | Hệ vệ tinh | `na,3,<1\|2\|3>`: 1 GPS+BeiDou+QZSS, 2 BeiDou, 3 GPS+QZSS | 1 | `sa,3 che do ve tinh: 1 GPS+BDS+QZSS (dang chay: 1)` |
| 4 | Chu kỳ gửi vị trí khi chạy / khi dừng (giây) | `na,4,<chạy>,<dừng>` | 10 / 300; 5–3600 | `sa,4 tan suat truyen: 10/300` |
| 8 | Cách xác định ACC | `na,8,<0\|1\|2>`: 0 theo vận tốc (không đấu dây), 1 theo dây ACC, 2 dây + vận tốc | 1 | `sa,8 co dau day ACC` |
| 9 | Khởi động lại | `na,9` | | Trả lời `reset ok`, 5 giây sau reset |
| 10 | Ngưỡng vận tốc coi là xe chạy (km/h) | `na,10,<km/h>` | 3; 0–200 | `sa,10 nguong van toc: 3 km/h` |
| 11 | Chế độ im lặng (tắt LED) | `na,11,<0\|1>` | 0 | `sa,11 trang thai im lang: tat` |
| 12 | Phát lại toàn bộ dữ liệu trong backup | `na,12,<từ>,<đến>` (hiện bỏ qua 2 mốc thời gian, phát lại tất cả) | | `bat dau truyen lai du lieu cu` |
| 14 | Đăng nhập / đăng xuất lái xe (đảo trạng thái) | `na,14` | | `na`: `sa,14 dang nhap lai xe: <tên> / <GPLX>`; `sa`: `sa,14 trang thai lai xe: <tên> / <GPLX> / login` |
| 17, 18 | Số điện thoại chủ xe 1, 2 | `na,17,<số>` | trống; tối đa 19 ký tự | `sa,17 so dien thoai 1: 0912345678` |
| 25 | Tên lái xe và số GPLX | `na,25,<tên>,<GPLX>` (đổi người thì tự đăng xuất người cũ, đăng nhập người mới) | | `sa,25 lai xe: <tên> / <GPLX> / login` |
| 26 | Thời gian xác nhận dừng đỗ (giây) | `na,26,<giây>` | 60; 0–900 | `sa,26 thoi gian ghi nhan dung/do: 60 giay` |
| 27 | Vị trí hiện tại (chỉ đọc) | | | Xem bên dưới |
| 29 | Thông tin thiết bị (chỉ đọc) | | | Xem bên dưới |
| 32 | Lịch tự reset | `na,32,<bật>,<giờ>`: bật > 0 thì reset mỗi ngày lúc `<giờ>`:00 theo đồng hồ thiết bị; 0 = tắt | 0, 0; giờ 0–23 | `sa,32 lich reset lai thiet bi: 1,3` |
| 33 | Cập nhật firmware / đọc phiên bản | `na,33,<URL>` (mục 8) | | `sa,33 firmware: <mã FW>` (thêm `(dang update)` khi đang chạy) |
| 34 | Xóa backup | `na,34` | | `dang xoa du lieu hanh trinh` |
| 35 | Xóa backup và đặt lại odometer | `na,35` | | `dang xoa tat ca du lieu` |
| 37 | Gửi SMS từ SIM của thiết bị (kiểm tra số / nạp tiền) | `na,37,<số nhận>,<nội dung>` | | Trả lời lại nội dung đã gửi |
| 40 | Khóa / mở khóa cấu hình | `na,40,<close\|open>` | mở | `sa,40 thiet bi da mo khoa cau hinh` |
| 41 | Khôi phục cài đặt gốc (xóa cấu hình trên flash và EFS) | `na,41` | | `khoi phuc cai dat mac dinh ban dau` |
| 42 | Ngủ sâu tiết kiệm ắc quy (mục 11) | `na,42,<0\|1>[,<chờ phút>]` | bật, 30 phút; 1–1440 phút | `sa,42 ngu sau: bat, cho 30 phut, thuc khi ACC bat / rung, dang thuc, acquy 12.6V` |
| 43 | Cảm biến gia tốc: thức khi rung / nghiêng, độ nhạy, bật / tắt cảm biến; `sa,43` trả cấu hình và sức khỏe cảm biến (mục 12) | `na,43,<0\|1>[,<+mg>[,<số ngắt>[,<cửa sổ giây>]]]`, `na,43,<off\|on>` | cảm biến bật; rung bật, 150 mg, 3 ngắt / 60 s | `sa,43 rung: bat, nguong +150mg ...; cam bien: OK, ...` |

Lệnh 44 cũ (`na,44`, `sa,44`) đã gộp vào lệnh 43 và không còn nhận (trả lời `ERROR ma lenh khong ho tro`).

Lệnh cũ vẫn nhận: `reset` hoặc `reset#` (trả lời `OK` rồi reset ngay), `update,<URL>` (như `na,33`).

### Lệnh 27: vị trí hiện tại

```text
sa,27 https://maps.google.com/maps?q=N%2010%2048.757780%20E%20106%2040.054320#OK
```

Tọa độ dạng độ + phút. Đuôi sau `#` cho biết nguồn vị trí:

| Đuôi | Nguồn |
|---|---|
| `#OK` | GNSS đang có fix (mẫu mới trong `GPS_FIX_STALE_SEC` = 5 giây) |
| `#LBS<n>m` | Mất GPS, vị trí theo trạm phát sóng (LBS) với độ chính xác `n` mét |
| `#LAST` | Mất GPS, vị trí tốt cuối cùng (lưu trong EFS, còn sau khởi động lại) |
| `sa,27 NO FIX` | Chưa từng có vị trí |

### Lệnh 29: thông tin thiết bị

```text
<tên> <IMEI> <ICCID> GSM: <CSQ> GPS: <số vệ tinh> <host> <port> <da ket noi|chua ket noi> <ACC> FL:<0|1> BK:<F|E><số ô> ERR:<n> SIM:<0|1>
```

| Trường | Ý nghĩa |
|---|---|
| `GSM` | Cường độ sóng (CSQ 0–31) |
| `GPS` | Số vệ tinh, 0 khi đang mất fix |
| ACC | 1 bật, 0 tắt, -1 đọc chân lỗi |
| `FL` | Flash SPI rời nhận được hay không |
| `BK` | Nơi lưu backup (`F` flash, `E` EFS) và số ô đang chờ phát bù |
| `ERR` | Số lần ghi flash lỗi từ lúc khởi động |
| `SIM` | SIM sẵn sàng |

### Lệnh 43: cảm biến gia tốc

```text
sa,43                                              cấu hình thức khi rung / nghiêng + sức khỏe cảm biến
na,43,<0|1>[,<+mg>[,<số ngắt>[,<cửa sổ giây>]]]   tắt / bật thức khi rung / nghiêng, độ nhạy (mục 12)
na,43,off                                          tắt hẳn cảm biến (lưu cấu hình, khóa aoff)
na,43,on                                           bật lại (khởi tạo lại + self-test)
```

`off` / `on` không phân biệt hoa thường. Mọi dạng `na,43` trả lời giống `sa,43`:

```text
sa,43 rung: bat, nguong +150mg (nap 156mg 3truc), 3 ngat/60s, nghieng 8do; cam bien: OK, addr 0x1E, x=-455 y=661 z=-555 |g|=975mg, self-test PASS (-177,-183,470), loi 0 lan (cuoi: OK), khoi tao lai 0
```

| Trường | Ý nghĩa |
|---|---|
| `rung` | Thức khi rung / nghiêng lúc ngủ: `bat` / `tat`; `(khong INT1)` nếu chưa có cảm biến hoặc chân ngắt |
| `nguong` / `nap` | Độ lệch đã đặt, giá trị thực nạp lần gần nhất và kiểu (`3truc`, `abs`, `-` = chưa nạp) |
| `ngat/s` | Số ngắt cần trong cửa sổ để thức |
| `nghieng` | Ngưỡng nghiêng `MOTION_TILT_DEG` |
| `cam bien` | Tình trạng (bảng dưới; `dang bat lai` ngay sau `na,43,on`), địa chỉ I2C, X/Y/Z và \|g\| lần kiểm tra gần nhất, self-test gần nhất (lúc khởi động hoặc sau khởi tạo lại), số lần hỏng, lỗi cuối, số lần khởi tạo lại. Đang ngủ thì thêm `(dang ngu, khong kiem tra)` |

Câu trả lời dài hơn 160 ký tự, nên qua SMS có thể bị tách hoặc cắt tùy nhà mạng.

Tình trạng là một trong: `OK`, `TAT` (đã tắt bằng `na,43,off`), `khong co cam bien`, `loi I2C`, `sai WHO_AM_I`, `mat cau hinh (reset)`, `du lieu dung yen`, `|g| bat thuong`. Self-test: `PASS`, `LECH` (ngoài khoảng), `LOI` (đọc lỗi), `chua chay`. Cách kiểm tra và tác dụng khi tắt xem mục 12.

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
| Mất kết nối Server liên tục 15 phút (không tính lúc ngủ) | Reset thiết bị |
| `gpio_status` (hoặc cả ứng dụng) treo, hoặc `gnss_task` không chạy vòng nào 120 giây | Watchdog phần cứng 16 giây reset module (bật sau khi khởi tạo xong) |
| Không nhận được dòng GNSS nào 60 giây | Tắt / bật lại GNSS |
| Không nhận được byte nào từ Server 10 phút | Ngắt và kết nối, đăng nhập lại |
| Gọi `abort()` | Reset thiết bị |
| Đến giờ trong lịch reset (lệnh 32) | Reset thiết bị |
| SIM không sẵn sàng 2 lần hỏi liên tiếp (mỗi 5 giây), kéo dài quá 30 giây | Tắt / bật sóng (CFUN 0/1) mỗi 60 giây để module đọc lại khe SIM |
| SIM sẵn sàng trở lại, hoặc ICCID đổi | Cấu hình lại SMS, ngắt kết nối và đăng nhập lại Server với ICCID mới |

## 11. Chế độ ngủ sâu tiết kiệm ắc quy

Code nằm ở `SleepPoll()` / `SleepDeepStep()` trong [app_nasa.c](app_nasa.c), hằng số trong [app_config.h](app_config.h) (nhóm `SLEEP_*`, `MOTION_WAKE_*`).

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

### Khi ngủ (ngủ sâu)

Lúc vào ngủ, thiết bị gửi `!NASA,2` vị trí cuối (không có Server thì lưu điểm mù), ngắt Server, tắt sóng (CFUN=4, SIM vẫn cấp nguồn) và tắt GNSS. Sau đó mọi tác vụ dừng, chỉ còn đọc chân ACC và cảm biến gia tốc.

| Thành phần | Trạng thái |
|---|---|
| Sóng / Server | Tắt (CFUN=4), socket đã đóng. Không nhận được SMS hay lệnh Server |
| GNSS | Tắt; dữ liệu GPS giữ giá trị cuối |
| Module | Cho phép sleep hệ thống (`sAPI_SystemSleepSet`). **Module không ngủ khi đang cắm USB** |
| LED | Tắt cả 3 |
| LIS3DSH | Nạp chương trình phát hiện rung ra INT1 và lưu véc-tơ trọng lực làm mốc nghiêng (mục 12) |
| Bản tin | Không gửi gì cho đến khi thức. Không có lần thức định kỳ |
| `nasa_reporter` | Mỗi 8 giây (`SLEEP_POLL_SEC`): nuôi watchdog phần mềm và phần cứng, đọc chân ACC, ngắt INT1 và góc nghiêng (4 mẫu, khoảng 160 ms). Không chạy gì khác |
| `gpio_status` | Chờ tối đa 60 giây / lần (`SLEEP_IDLE_TASK_SEC`), chỉ canh `nasa_reporter` treo |
| `gnss_task`, `SmsRecvTask`, `agps_task`, `ntp_sync` | Chờ 60 giây / lần; `SmsRecvTask` không hỏi SIM |

### Điều kiện thức

Chỉ ACC và cảm biến gia tốc đánh thức:

| Nguồn | Cách phát hiện |
|---|---|
| ACC bật | `nasa_reporter` đọc chân ACC mỗi 8 giây, thấy mức bật thì đọc lại sau 100 ms rồi thức. Ngắt cạnh xuống chân ACC chỉ làm thức sớm hơn; nếu chân vẫn báo tắt thì coi là ngắt giả, ghi log và ngủ lại |
| Rung / bị kéo, chở | LIS3DSH báo đủ 3 ngắt trong 60 giây (mặc định, chỉnh bằng lệnh 43). Một lần rung đơn (đóng cửa, xe khác chạy qua) không đủ |
| Bị nghiêng (cẩu lên xe cứu hộ, kéo êm) | Véc-tơ trọng lực lệch quá 8° (`MOTION_TILT_DEG`, 0 = tắt) so với lúc vào ngủ, đọc lại sau 1 giây vẫn lệch. Đi theo bật / tắt thức khi rung (lệnh 43) |

Khi thức: bật sóng (CFUN=1), bật GNSS, đánh thức `gpio_status`, rồi kích hoạt PDP, kết nối, đăng nhập và gửi `!NASA,2` ngay. Thức vì rung hoặc nghiêng thì phải chờ lại đủ thời gian chờ (30 phút) mới ngủ tiếp, để kịp phát hiện xe bị dời (mục dưới).

### Xe di chuyển khi tắt ACC

Lúc dây ACC báo tắt, bộ lọc chạy / dừng của GPS coi xe đứng yên bất kể vận tốc. `NoAccMovePoll()` trong [app_nasa.c](app_nasa.c) xét riêng trường hợp xe bị đẩy, kéo hoặc chở đi khi tắt máy (chạy mỗi vòng lúc thức, kể cả trong 30 phút chờ trước khi ngủ):

| Bước | Điều kiện |
|---|---|
| Chỗ đỗ | Fix GNSS thật đầu tiên lúc đứng yên sau khi tắt ACC; chưa có thì tạm lấy vị trí GNSS cuối đã lưu |
| Bắt đầu | Vận tốc GPS (fix thật) > ngưỡng lệnh 10 liên tục 10 giây (`NOACC_MOVE_CONFIRM_SEC`) và cách chỗ đỗ ≥ 50 m (`NOACC_MOVE_MIN_DIST_M`). Không biết chỗ đỗ thì phải liên tục 30 giây |
| Khi đang bị dời | Gửi `!NASA,2` ngay (bit ACC = 0, vận tốc > 0), chu kỳ gửi như lúc chạy, không ngủ; SMS cảnh báo tới số chủ xe 1, 2 (lệnh 17, 18), cách nhau tối thiểu 30 phút (`NOACC_MOVE_SMS_*`) |
| Kết thúc | Đứng yên (hoặc mất fix) 300 giây (`NOACC_MOVE_END_SEC`), hoặc ACC bật |

SMS: `CANH BAO <biển số>: xe di chuyen khi tat may, 12km/h, cach cho do 80m https://maps.google.com/maps?q=<lat>,<lon>`. Tắt cả tính năng bằng `NOACC_MOVE_ENABLE 0`, chỉ tắt SMS bằng `NOACC_MOVE_SMS_ENABLE 0` trong [app_config.h](app_config.h). `sa,42` báo `dang thuc (xe di chuyen khi tat ACC)` khi đang trong trạng thái này.

### Lệnh 42

```text
na,42,<0|1>[,<chờ phút>]     ví dụ: na,42,1,30
sa,42
```

Trả lời: `sa,42 ngu sau: bat, cho 30 phut, thuc khi ACC bat / rung, dang thuc, acquy 12.7V`. Tham số thứ 3 (chu kỳ thức) của bản trước vẫn được nhận nhưng bị bỏ qua.

### Log và đo dòng

| Log | Ý nghĩa |
|---|---|
| `[SLEEP] ACC off & still: sleep after 1800s` | Bắt đầu đếm thời gian chờ |
| `[SLEEP] enter: ...` | Vào ngủ |
| `[SLEEP] USB connected: module stays in full power ...` | Đang cắm USB, module sẽ không ngủ thật |
| `[SLEEP] system sleep enable ret=0 (now=1, usb=0)` | Cờ sleep hệ thống và trạng thái USB |
| `[SLEEP] asleep 600s: sleep flag=1 usb=0 V=12.6 tilt=0.4 deg` | Mỗi 10 phút khi đang ngủ (`SLEEP_STATUS_LOG_SEC`) |
| `[SLEEP] motion #1/3 in 60s window (OUTS=0x..)` | Một ngắt rung |
| `[SLEEP] tilt armed: g=(..) wake if > 8 deg` | Đã lưu mốc nghiêng lúc vào ngủ |
| `[SLEEP] tilt 9.5 / 9.8 deg > 8 deg: ...` | Bị nghiêng, sắp thức (`... -> transient, ignore` nếu lần đọc lại hết lệch) |
| `[NOACC] start: ACC off but 12.0km/h for 10s, 80m from park (fix) ...` | Xe bị dời khi tắt ACC; `[NOACC] SMS to ...`, `[NOACC] end: ...` |
| `[SLEEP] ACC interrupt but pin 37 high (ACC off) -> ignore` | Ngắt ACC giả, ngủ lại |
| `[SLEEP] exit: <lý do> after ...s asleep` | Thức: `ACC interrupt`, `ACC pin`, `motion`, `tilt`, `sleep disabled`... |
| `[NET] CFUN=4 OK` / `[NET] CFUN=1 OK` | Tắt / bật sóng lúc vào / ra khỏi ngủ |

Khi đo dòng lúc ngủ phải **rút USB**, vì log qua USB giữ module thức. Kết quả đo trên board ngày 03/11/2026 ở 12 V: 0,09 A khi thức và cắm USB, 0,06 A khi ngủ. Mức 0,06 A vẫn cao hơn nhiều so với lúc module ngủ thật (vài mA). Chưa tách được phần nào thuộc module, phần nào thuộc các mạch khác trên board (nguồn, RS232, anten GPS); cần đo riêng dòng vào chân VBAT.

## 12. Cảm biến gia tốc LIS3DSH

Driver: [drv_lis3dsh.c](drv_lis3dsh.c), bus qua [hal/hal_i2c.h](hal/hal_i2c.h). Nếu không có cảm biến hoặc khởi tạo lỗi, firmware chạy như cũ, chỉ thiếu tính năng thức khi rung.

### Kết nối

| Tín hiệu | Nối | Ghi chú |
|---|---|---|
| SCL / SDA | I2C kênh 0 (CI2C, chân 38 / 37 của module) | 400 kHz, kéo lên Vdd_IO |
| CS | Vdd_IO | Chọn giao tiếp I2C |
| SEL / SDO | GND | Địa chỉ 7 bit `0x1E` (driver vẫn thử `0x1D` cho board khác) |
| INT1 | pad 12 (GPIO_14, chân 36) | Ngắt chuyển động, tích cực mức thấp, chốt |
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

Khi vào ngủ, `LIS3DSH_MotionArm()` nạp state machine. Chương trình giống nhau: `STx_1 = 0x05` (một trục bất kỳ vượt THRS1), `STx_2 = 0x11` (báo ngắt rồi chạy lại), MASK = ±X ±Y ±Z, ngắt chốt ra INT1, đơn vị ngưỡng 15,6 mg/LSB. Có hai kiểu:

| Kiểu | State machine | So sánh | Độ nhạy |
|---|---|---|---|
| `3truc` (ưu tiên) | SM2, `SETT2.RADI` + constant shift `CS_X/Y/Z` (`0x13`–`0x15`) = trọng lực lúc đứng yên | \|a − g\| > độ lệch | Cả 3 trục cùng nhạy 150 mg: bắt được đẩy / kéo theo phương ngang |
| `abs` (dự phòng) | SM1 | \|a\| > trục lớn nhất lúc đứng yên + độ lệch | Chỉ trục mang trọng lực nhạy; trục ngang phải vượt khoảng 1,15 g |

Nghĩa bit `D_CS` và thang đo `CS` chưa đối chiếu được với tài liệu gốc, nên mỗi lần nạp driver tự kiểm 2 bước trên chip (khoảng 200 ms mỗi bước, thử cả 2 giá trị `D_CS`):

1. `CS = 0`, ngưỡng = nửa trục lớn nhất: trục mang trọng lực **phải** ngắt (SM2 và INT1 chạy, đang so dữ liệu sau CS).
2. `CS = g`, cùng ngưỡng: lúc đứng yên **không được** ngắt (CS đúng dấu, đúng thang đo).

Qua cả 2 bước thì nạp ngưỡng thật; không qua thì lùi về kiểu `abs` và ghi log. `sa,43` cho biết kiểu của lần nạp gần nhất.

- `SleepPoll` đọc `OUTS2` (`0x7F`, kiểu `abs` là `OUTS1` `0x5F`) để xóa ngắt và đếm. Nếu chân INT1 không hỗ trợ đánh thức, firmware vẫn đọc mức chân mỗi vòng ngủ (8 giây). Nạp lỗi thì không đọc INT1 (tránh thức nhầm vì chân nằm mức thấp), chỉ còn ACC và nghiêng.
- Mỗi vòng ngủ còn đọc véc-tơ trọng lực (`LIS3DSH_ReadGravity`) so với mốc lúc vào ngủ để phát hiện nghiêng (mục 11).
- Khi thức hẳn thì tắt state machine (`LIS3DSH_MotionDisarm()`).

Cần kiểm trên board: log `[LIS3DSH] SM2 check ...` và `motion armed DELTA` lúc vào ngủ; đẩy nhẹ ngang thân xe (không chỉ gõ từ trên xuống) phải thấy `[SLEEP] motion #1/3`.

### Xác nhận vào cua (tăng tần suất gửi vị trí)

Khi xe đang thức và chạy từ 8 km/h, `TurnPoll()` trong [app_nasa.c](app_nasa.c) chạy mỗi vòng `nasa_reporter` (khoảng 1 giây):

1. Đọc trung bình các mẫu trong FIFO của cảm biến (khoảng 25 mẫu / 1 giây), để lọc rung xóc mặt đường.
2. Ước lượng véc-tơ trọng lực bằng lọc thông thấp: nhanh khi xe đứng, rất chậm khi xe chạy (hằng số 60 giây), đóng băng khi đang cua. Nhờ vậy không cần biết cảm biến lắp theo hướng nào.
3. Gia tốc nằm ngang = phần vuông góc với trọng lực, nên xóc lên xuống bị loại. Trừ tiếp phần gia tốc dọc tính từ thay đổi vận tốc GPS (tăng / giảm tốc), phần còn lại là gia tốc ngang.
4. Gia tốc ngang lớn hơn gia tốc dọc và vượt ngưỡng liên tục 2 giây thì nhận là vào cua. Ngưỡng thoát thấp hơn ngưỡng vào 30%, và phải dưới ngưỡng 2 giây mới trả chu kỳ thường.

| Mức | Gia tốc ngang | Ví dụ ở 40 km/h | Chu kỳ gửi |
|---|---|---|---|
| Cua | ≥ 120 mg (`ACCEL_TURN_MG`) | quay khoảng 6°/s | Chu kỳ chạy × 0,5 (như rule ≥15°) |
| Cua gấp | ≥ 250 mg (`ACCEL_TURN_SHARP_MG`) | quay khoảng 12°/s | 2 giây (như rule ≥30°) |

Rule theo hướng GPS vẫn giữ nguyên; chu kỳ thực tế là mức ngắn hơn của hai cách. Cảm biến cho tín hiệu ngay khi xe bắt đầu cua, còn hướng GPS phải lệch đủ 15° / 30° so với bản tin trước mới có tác dụng. Tắt tính năng bằng `ACCEL_TURN_ENABLE 0` trong [app_config.h](app_config.h).

FIFO tự tắt khi self-test và khi nạp chương trình phát hiện rung lúc vào ngủ; lần đọc tiếp theo tự bật lại.

### Chỉnh độ nhạy (lệnh 43)

```text
na,43,<0|1>[,<+mg>[,<số ngắt>[,<cửa sổ giây>]]]   ví dụ: na,43,1,150,3,60
sa,43
```

Cú pháp đầy đủ của lệnh 43 (cả bật / tắt cảm biến) và câu trả lời xem mục 7.

| Tham số | Mặc định | Khoảng | Tác dụng |
|---|---|---|---|
| `0` / `1` | 1 | | Tắt / bật thức khi rung |
| `+mg` | 150 | 30–900 | Kiểu `3truc`: độ lệch trên mỗi trục so với lúc đứng yên; kiểu `abs`: so với trục lớn nhất. Nhỏ = nhạy hơn |
| Số ngắt | 3 | 1–20 | Cần bao nhiêu ngắt thì thức. Lớn = ít thức nhầm hơn |
| Cửa sổ | 60 s | 10–600 | Các ngắt phải nằm trong khoảng này |

Mỗi lần đang ngủ chỉ đếm được tối đa một ngắt mỗi vòng 8 giây (`SLEEP_POLL_SEC`), nên với 3 ngắt / 60 giây thì xe phải rung liên tục khoảng 16–24 giây.

#### Ý nghĩa của `+mg`

mg là mili-g (1 g = 9,81 m/s², gia tốc trọng trường), nên 150 mg = 0,15 g ≈ 1,5 m/s². Ở kiểu `3truc`, mỗi lần gia tốc trên một trục bất kỳ (X, Y hoặc Z) lệch quá 150 mg so với lúc xe đứng yên, cảm biến báo **một ngắt**. Một ngắt chưa đủ để thức, phải đủ số ngắt trong cửa sổ (mặc định 3 ngắt / 60 giây). Nhờ vậy một cú đóng cửa đơn lẻ không làm thiết bị thức.

Ngưỡng trong chip đi theo bước 15,6 mg, nên giá trị đặt được làm tròn. `sa,43` hiện giá trị thực ở mục `nap`.

| | Giá trị đặt | Thực nạp vào chip |
|---|---|---|
| Min | 30 mg | 31 mg (2 bước) |
| Mặc định | 150 mg | 156 mg (10 bước) |
| Max | 900 mg | 906 mg (58 bước) |

- Gửi `na,43` ngoài khoảng 30–900 thì giá trị bị kẹp về biên gần nhất; cấu hình đã lưu mà ngoài khoảng thì dùng lại 150.
- Max 900: thanh ghi ngưỡng tối đa 127 bước ≈ 1984 mg. Kiểu `abs` (dự phòng) phải tính cả trọng lực (≈ 1000 mg) cộng độ lệch, nên độ lệch chỉ lên được khoảng 980 mg; 900 là mức an toàn.
- Min 30: thấp hơn nữa thì nhiễu cảm biến và rung nền dễ báo giả. Khi dùng thực tế nên đặt **từ khoảng 50 mg trở lên**: ở kiểu `3truc`, trọng lực bị trừ cũng được làm tròn theo bước 15,6 mg (sai tới ±8 mg), cộng thêm trôi nhiệt, nên 30 mg có thể tự ngắt khi xe đứng yên.
- Ở kiểu `abs`, 150 mg là mức vượt thêm so với trục đang chịu trọng lực; hai trục ngang phải vượt khoảng 1,15 g nên gần như không bắt được đẩy / kéo ngang.

Mức gia tốc từng tình huống dưới đây chỉ là ước lượng, cần đo thật trên xe:

| Tình huống | Gia tốc ước lượng |
|---|---|
| Đẩy xe, kéo xe êm | khoảng 50–150 mg |
| Người lên xe, đóng cửa | khoảng 100–300 mg, rất ngắn |
| Xóc mặt đường khi bị kéo | khoảng 200 mg trở lên |

| `+mg` | Dùng khi |
|---|---|
| 60–100 | Cần nhạy, bắt được đẩy / kéo nhẹ; dễ thức nhầm hơn |
| 150 (mặc định) | Cân bằng giữa độ nhạy và thức nhầm |
| ≥ 300 | Xe đỗ cạnh đường đông, hay bị rung |

Cảm biến lấy mẫu 25 lần / giây (40 ms một mẫu), nên cú gõ quá ngắn có thể lọt.

Cách chỉnh (theo dõi log qua cổng debug, rút USB thì module mới ngủ thật):
1. Cho thiết bị ngủ sớm để thử: `na,42,1,1` (ngủ sau 1 phút khi ACC tắt, đứng yên). Thử xong trả lại `na,42,1,30`.
2. Khi đang ngủ, thử các tình huống: đóng cửa, người lên xe, gõ vào thân xe, đẩy hoặc kéo xe. Mỗi ngắt có một dòng `[SLEEP] motion #n/3 ...`; đủ số ngắt thì `[SLEEP] exit: motion`.
3. Rung nhẹ (đóng cửa) mà ra nhiều ngắt thì tăng `+mg` hoặc tăng số ngắt; kéo xe mà ít ngắt thì giảm `+mg`.

Giá trị lưu trong cấu hình (khóa `mwoff`, `mdel`, `mev`, `mwin`).

### Kiểm tra lúc chạy và bật / tắt (`sa,43`, `na,43,off` / `on`)

Khi thiết bị đang thức, `AccelHealthPoll()` trong [app_nasa.c](app_nasa.c) kiểm tra cảm biến mỗi 30 giây (`ACCEL_HEALTH_SEC`, 0 = tắt):

| Kiểm tra | Coi là hỏng khi |
|---|---|
| Đọc được `WHO_AM_I` = `0x3F` | Lỗi I2C hoặc sai ID 3 lần liên tiếp (`ACCEL_FAIL_MAX`) |
| CTRL_REG4 còn đúng cấu hình | Sai ngay lần đầu: chip đã tự reset (thường do sụt áp) |
| X/Y/Z thô thay đổi giữa các lần đọc | Giống hệt nhau 10 lần liên tiếp (`ACCEL_STUCK_MAX`), tức khoảng 5 phút |
| \|g\| trong khoảng 400–1800 mg (`ACCEL_G_MIN_MG` / `ACCEL_G_MAX_MG`) | Ngoài khoảng 5 lần liên tiếp (`ACCEL_BAD_G_MAX`) |

Khi hỏng: khởi tạo lại cảm biến, chạy self-test, bật lại đánh thức bằng INT1. Không có cảm biến thì cứ 5 phút (`ACCEL_REINIT_SEC`) thử khởi tạo lại một lần. Lúc ngủ thì không kiểm tra. Xem kết quả bằng `sa,43`.

`na,43,off` tắt hẳn cảm biến: soft reset về chế độ power-down của chip, lưu vào cấu hình (khóa `aoff`), nên khởi động lại vẫn tắt. Khi tắt thì không thức khi rung (lúc ngủ chỉ dây ACC đánh thức), không xác nhận vào cua, không log `[ACCEL]`, không kiểm tra định kỳ. `na,43,on` khởi tạo lại và chạy self-test. Cú pháp và trả lời của lệnh xem mục 7.

### Log

| Log | Ý nghĩa |
|---|---|
| `[LIS3DSH] OK addr=0x1E ch=0 CTRL4=0x4F` | Khởi tạo xong |
| `[LIS3DSH] self-test PASS: dX=-174 dY=-181 dZ=464 mg` | Kết quả self-test |
| `[ACCEL] x=-700 y=323 z=619 mg` | Khi thức, mỗi 10 giây (`ACCEL_LOG_SEC`, 0 = tắt), để kiểm tra lắp đặt |
| `[LIS3DSH] SM2 check SETT2=0x19: CS=0 fired=1 (need 1), CS=g fired=0 (need 0)` | Tự kiểm kiểu `3truc` |
| `[LIS3DSH] motion armed DELTA: g=(-700,323,619) CS=(-45,21,40) thr=10 (+-156 mg all axes)` | Đã nạp kiểu `3truc` lúc vào ngủ |
| `[LIS3DSH] motion armed ABS: g=(..) thr=54 (843 mg, gravity axis only)` | Tự kiểm không qua, đã lùi về kiểu `abs` |
| `[TURN] turn: lat=150mg long=20mg v=42km/h yaw~7deg/s -> period 5s` | Bắt đầu cua (`turn` / `sharp`) và chu kỳ gửi mới |
| `[TURN] end: ... -> period 10s` | Hết cua, trả chu kỳ thường |
| `[ACCEL/HEALTH] I2C error (WHO_AM_I=-1) 1/3` | Một lần kiểm tra lỗi, chưa tới ngưỡng |
| `[ACCEL/HEALTH] FAULT #1: loi I2C -> re-init` | Phát hiện hỏng, khởi tạo lại |
| `[ACCEL/HEALTH] re-init OK (#1)` / `re-init failed, retry in 300s` | Kết quả khởi tạo lại |
| `[ACCEL/HEALTH] self-test (re-init) PASS: ...` | Self-test sau khởi tạo lại |
| `[ACCEL] disabled by cmd 43 off -> power down OK` | Đã tắt cảm biến (`na,43,off`) |
| `[LIS3DSH] disabled (na,43,off) -> power down OK` | Khởi động khi cảm biến đang tắt |
| `[ACCEL] enabled by cmd 43 on -> init` | Bật lại (`na,43,on`) |

## 13. Phần cứng

| Tín hiệu | Pad | Ghi chú |
|---|---|---|
| LED mạng (LED_NET) | 74 (GPIO_03, chân 7) | Nháy khi đã đăng nhập Server, sáng khi có sóng, tắt khi mất sóng |
| LED nguồn (LED_MEM) | 32 (GPIO_01, chân 5) | Trạng thái bộ nhớ backup: sáng = bộ nhớ trong (file EFS), nháy = bộ nhớ ngoài (flash SPI), tắt = cả hai lỗi |
| LED GNSS (LED_GPS) | 80 (GPIO_00, chân 4) | Nháy khi có từ 5 vệ tinh |
| DOUT | 77 (GPIO_08, chân 48) | Rơ-le, qua Q4 hở collector ra J2 chân 4 |
| ACC_IN | 28 (GPIO_09, chân 52) | Mức thấp = ACC bật; ngắt cạnh xuống đánh thức khi ngủ |
| CS flash | 17 (GPIO_35/SSP0_CS, chân 12) | Nối thẳng CS#: GPIO mức thấp = chọn flash (`FLASH_SPI_CS_ACTIVE_LEVEL`); driver tự thử mức ngược lại nếu không đọc được mã chip |
| INT1 LIS3DSH | 12 (GPIO_14, chân 36) | Ngắt chuyển động, đánh thức khi ngủ |
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
