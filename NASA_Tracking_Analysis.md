# Phân tích logic chức năng NASA Tracking (SIMCOM A7677S)

Dựa vào mã nguồn của chương trình trong thư mục không gian làm việc (`sc_application.c` và `app_nasa.c`), đây là một firmware định vị, giám sát hành trình (Tracking) liên lạc qua giao thức cấu trúc dạng bản tin `!NASA,...`. 

Chương trình được vận hành dựa trên các tiến trình song song (Tasks) và một máy trạng thái (State Machine) báo cáo dữ liệu định vị.

## 1. Kiến trúc Task (Tiến trình song song)
Khi ứng dụng khởi động từ điểm neo `userSpace_Main` (trong `sc_application.c`), nó khởi tạo các API, đọc cấu hình, chuẩn bị SMS, Mạng (Network) và GPS. Sau đó, nó tạo ra 3 task chính chạy độc lập:

1. **`SmsRecvTask`**: 
   - Chờ và nhận tin nhắn SMS thông qua URC.
   - Khi có tin nhắn đến, task sẽ đọc nội dung và gọi hàm thực thi cấu hình hoặc lệnh tương ứng thông qua `SMS_ExtractAndExecuteSmsBody()`.
2. **`gpio_status` Task**:
   - Chạy vòng lặp vô hạn theo chu kỳ 100ms.
   - Xử lý chống nhiễu (debounce) cho chân ACC (khóa điện).
   - Kiểm tra chất lượng mạng (CSQ) và báo trạng thái GNSS/Mạng/Nguồn thông qua các LED hiển thị theo các mức nháy khác nhau.
3. **`nasa_reporter` Task**:
   - Nhiệm vụ quan trọng nhất, chịu trách nhiệm kết nối lên server và gửi bản tin tracking.
   - Trong task này, hàm `NASA_RunStep()` được gọi liên tục trong một vòng lặp vô hạn để vận hành một **Máy trạng thái (State Machine)**.

---

## 2. Máy trạng thái báo cáo (NASA_RunStep)
Logic báo cáo hành trình nằm ở hàm `NASA_RunStep()` trong `app_nasa.c`. Nó được chia thành 5 trạng thái: `STATE_INIT`, `STATE_CONNECT`, `STATE_LOGIN`, `STATE_TRACKING` và `STATE_ERROR_RETRY`.

### Lưu đồ giải thuật báo cáo hành trình

```mermaid
stateDiagram-v2
    [*] --> STATE_INIT : Khởi động / Mất kết nối
    
    STATE_INIT --> STATE_CONNECT : Activate PDP thành công
    STATE_INIT --> STATE_ERROR_RETRY : Lỗi PDP / Device Disable
    
    STATE_CONNECT --> STATE_LOGIN : Kết nối Server TCP/UDP OK
    STATE_CONNECT --> STATE_ERROR_RETRY : Kết nối thất bại
    
    STATE_LOGIN --> STATE_TRACKING : Gửi bản tin Login, nhận ACK OK
    STATE_LOGIN --> STATE_ERROR_RETRY : Gửi lỗi / Timeout đợi ACK
    
    STATE_TRACKING --> STATE_TRACKING : Lặp định kỳ báo vị trí
    STATE_TRACKING --> STATE_ERROR_RETRY : Gửi dữ liệu lỗi / Mạng rớt / Socket đứt
    STATE_TRACKING --> STATE_INIT : Device Disable
    
    STATE_ERROR_RETRY --> STATE_INIT : Disconnect & Chờ (10s)
```

---

## 3. Lưu đồ chi tiết vòng lặp TRACKING
Khi thiết bị ở trạng thái `STATE_TRACKING`, nó sẽ hoạt động theo quy trình như sau:

```mermaid
flowchart TD
    A[Bắt đầu STATE_TRACKING] --> B{Thiết bị còn được kích hoạt không?}
    B -- Không --> B1[Ngắt kết nối, về STATE_INIT]
    B -- Có --> C{Có dữ liệu mạng đến hoặc rớt mạng?}
    C -- Có lỗi --> C1[Về STATE_ERROR_RETRY]
    C -- Mạng OK --> D{Có yêu cầu phát lại dữ liệu mù <br/>hoặc hàng đợi Backup có DL?}
    D -- Có --> D1[Gửi dồn các bản tin cũ]
    D1 --> E
    D -- Không --> E
    E[Lấy dữ liệu GpsSnapshot_t <br/> & Kiểm tra ngày mới để reset ODO] --> F[Phân tích đỗ xe <br/> NASA_ParkUpdate]
    F --> G{Trạng thái ACC thay đổi?}
    G -- Có --> G1[Đánh dấu gửi ngay bản tin khẩn]
    G1 --> H
    G -- Không --> H{Đã tới thời gian gửi định kỳ? <br/>Tính toán theo chu kỳ dừng/chạy}
    H -- Có (Hoặc có yêu cầu gửi khẩn) --> I[Tổng hợp Device Status, Csq, Voltage]
    I --> J[Tạo gói Tracking !NASA,2,...]
    J --> K[Gửi TCP/UDP]
    K --> |Gửi thành công| L[Ghi lưu vào vòng 10s Backup để backup]
    K --> |Lỗi gửi mạng| L1[Đẩy bản tin vào Backup chờ phát lại]
    L1 --> C1
    H -- Chưa tới thời gian gửi --> M[Ghi bản ghi vào vòng Backup 10s]
    L --> N[Sleep 1 khoảng nhỏ và lặp lại vòng lặp]
    M --> N
    N --> B
```

### Giải thích các khối nghiệp vụ chính trong TRACKING:
- **Chu kỳ động (Adaptive Period)**: Nếu ACC bật, thời gian gửi (Period) được điều chỉnh linh hoạt bằng thuật toán tính góc quay (Heading). Nếu xe vào cua ngoặt, nó sẽ giảm thời gian báo vị trí (tăng tần suất). Nếu xe đỗ, sẽ lấy chu kỳ `PeriodStopped`.
- **Dữ liệu điểm mù (Backup)**: Bất cứ bản tin nào lỗi gửi (do mất sóng, rớt mạng) đều được đẩy vào hệ thống lưu trữ dự phòng `Backup_Push`. Các bản tin này sẽ được gửi dồn khi máy kết nối lại và vào được trạng thái `STATE_TRACKING`.
- **Phân tích đỗ xe (Park Update)**: Tính toán thông minh tốc độ xe và thời gian dừng. Gửi bản tin số 1 (Bắt đầu đỗ), số 2 (Đang đỗ định kỳ), và số 3 (Bắt đầu di chuyển lại).
- **Hàng rào thiết bị (Device Status)**: Các thông số như cảnh báo tốc độ, dòng điện yếu (Low Battery), đăng nhập tài xế, và trạng thái ACC được bitmask vào trường báo cáo.
