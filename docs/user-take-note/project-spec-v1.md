# Firmware Audit Handoff – Phòng Cân Tim

## 1. Mục tiêu dự án

Firmware chạy trên ESP32-C3.

Chức năng chính:

* Đọc dữ liệu từ cảm biến khoảng cách VL53L1X.
* Xác định trạng thái có người/không người dựa trên khoảng cách cấu hình.
* Publish dữ liệu qua MQTT.
* Cung cấp Web UI để cấu hình.
* Lưu toàn bộ cấu hình bằng Preferences (NVS).
* Giao tiếp mạng qua Ethernet W5500.

Firmware được thiết kế để hoạt động độc lập sau khi cấp nguồn, không cần Serial Monitor.

---

# 2. Phần cứng

MCU

* ESP32-C3

Network

* W5500 Ethernet
* SPI

Sensor

* VL53L1X

Storage

* Preferences (NVS)

Communication

* MQTT
* HTTP WebServer

Debug

* USB CDC (chỉ dùng khi debug)

---

# 3. Kiến trúc tổng thể

```
VL53L1X
    │
    ▼
Read Distance
    │
    ▼
Distance Filter
    │
    ▼
State Detection
    │
 ┌──┴────────────┐
 │               │
 ▼               ▼
MQTT          Web UI
 │               │
 └──────┬────────┘
        ▼
 Preferences
```

**(Mới, 2026-08-02)** `Web UI` → các route thay đổi trạng thái/cấu hình (`/save`,
`/test_mqtt`, `/test_osc`) đi qua 1 bước **HTTP Basic Auth** trước khi chạm tới `Preferences`
— xem mục Web Server bên dưới và F6 ở §8.

---

# 4. Chức năng hiện có

## Ethernet

* Khởi tạo W5500.
* Chờ kết nối Ethernet (DHCP, timeout `ETH_WAIT_MS = 10000`).
* Có callback báo trạng thái mạng.
* **(Mới, F19, 2026-08-02)** Nếu hết timeout mà vẫn chưa lấy được IP qua DHCP (mạng không có
  DHCP server) → tự áp **IP tĩnh dự phòng `192.168.99.199`** (gateway `192.168.99.1`, mask
  `255.255.255.0`) thay vì để thiết bị không có IP vĩnh viễn — xem
  `docs/user-guide/web-ui-guide.md` mục 1 cho hướng dẫn truy cập bằng IP này.
* **(Mới, 2026-08-02)** 3 giá trị IP tĩnh dự phòng trên giờ sửa được qua Web UI, tab **"Mạng
  (Ethernet)"** riêng (trước đó chỉ sửa được qua NVS trực tiếp) — xem
  `docs/user-guide/web-ui-guide.md` mục 5c.
* **(Mới, F34, 2026-08-03)** `ETH.config()` ở nhánh static fallback giờ truyền gateway làm
  DNS1 (`ETH.config(ip, gw, mask, gw)`) — trước đó DNS để trống nên broker nhập bằng hostname
  không resolve được khi rơi vào fallback.
* **(Mới, F32-uu-tien-ip-tinh, 2026-08-03)** Thêm tick **"Ưu tiên IP tĩnh (bỏ qua DHCP)"**
  trên tab Mạng (`ethUseStaticFirst`, NVS key `eth_first`). Bật lên → `setup()` áp IP tĩnh
  ngay từ đầu, bỏ qua hoàn toàn 10s chờ DHCP; nếu IP/gateway/netmask nhập sai thì tự động lùi
  về DHCP-rồi-fallback như cũ, không bị kẹt. Port từ project `gia_sach`.

## MQTT

* Kết nối broker.
* Tự reconnect.
* Publish dữ liệu sensor.
* Các thông số cấu hình qua Web UI.

## Web Server

**(Mới, F6, 2026-08-02)** `/save`, `/test_mqtt`, `/test_osc` yêu cầu **HTTP Basic Auth**
(username/password mặc định `admin`/`admin`, đổi được qua panel Admin Auth trên chính Web
UI). Trang chủ `/` và endpoint polling `/data` (đọc trạng thái realtime) **không** yêu cầu
đăng nhập. Đây là thay đổi kiến trúc user-visible: trước đây bất kỳ ai trên cùng mạng LAN
đều POST được vào `/save` không cần xác thực.

Cho phép cấu hình:

* MQTT Server (F33: submit rỗng = giữ nguyên, không còn ghi đè thành chuỗi rỗng làm chết MQTT)
* MQTT Port (validate 1-65535, F16)
* MQTT Username
* MQTT Password — **(Mới, F32, 2026-08-03)** ô Password không còn hiện lại giá trị đã lưu
  trong HTML nữa (trước đó `/` không cần đăng nhập nên ai vào được LAN cũng đọc được password
  broker bằng View Source/curl); để trống khi Save = giữ nguyên. Xóa trắng Username = chuyển
  hẳn sang kết nối anonymous (không gửi username lẫn password) — đường thoát duy nhất để "xóa"
  password vì ô này không đọc lại được nữa.
* MQTT Topic

Thông số sensor:

* Distance Min
* Distance Max (validate Min <= Max, F16)

OSC:

* Full Address / Missing Address (validate bắt buộc bắt đầu bằng `/`, 4.9)
* Full Value / Missing Value (int32, encode big-endian đúng chuẩn OSC 1.0, F9)
* OSC Port (validate 1-65535, F16)

Confirm Time (ms) — clamp vào khoảng [50, 60000] thay vì nhận nguyên giá trị nhập (F15).

**(Mới, F6)** Admin Auth — username/password cho Basic Auth ở trên.

Sau khi Save:

* validate từng field, từ chối (không lưu) field không hợp lệ thay vì âm thầm sửa/tràn buffer
* ghi Preferences
* cập nhật biến runtime
* phản hồi trung thực số lỗi ghi NVS thay vì luôn báo "Saved OK" (F2, xem Bug 1 bên dưới)

---

# 5. Thuật toán

Mỗi chu kỳ:

```
Read Distance

↓

Compare with Min / Max

↓

Detected ?

↓

Publish MQTT

↓

Update Web
```

Điều kiện:

```
distanceMin <= distance <= distanceMax
```

→ Detected

Ngược lại

→ Not detected

---

# 6. Preferences

Firmware sử dụng Preferences để lưu toàn bộ cấu hình.

Sau boot:

```
begin()

↓

read all keys

↓

copy to RAM

↓

Web hiển thị giá trị
```

Khi Save:

```
POST

↓

validate

↓

putString / putInt

↓

update runtime variables
```

---

## 6b. Bảo mật thông tin đăng nhập MQTT

Firmware **không** hardcode MQTT username/password mặc định trong source code.

* `mqttUser` / `mqttPass` khởi tạo rỗng (`""`) trong code.
* Thông tin đăng nhập thật phải được nhập qua Web UI ở lần setup đầu tiên, sau đó được lưu vào Preferences (NVS) và dùng cho các lần boot sau.
* Không commit credentials thật vào git dưới bất kỳ hình thức nào (kể cả làm giá trị mặc định trong `.cpp`).
* Nếu broker đổi mật khẩu, chỉ cần cập nhật qua Web UI — không cần build lại firmware.

---

# 7. Hiện trạng

Đang hoạt động:

* Ethernet
* MQTT
* Web UI
* Sensor
* Preferences
* MQTT reconnect
* Publish dữ liệu

---

# 8. Các lỗi đã phát hiện

## Bug 1 — ✅ FIXED (2026-08-02, F1/F29/F2/F4)

OSC Full Address không được restore sau reboot.

Hiện tượng:

* Full Value còn.
* Full Address mất.

Khả năng:

* sai key Preferences
* thiếu putString()
* thiếu getString()
* sai thứ tự load
* buffer overwrite

**Root cause xác nhận:** sai key Preferences — nhưng không phải "sai" theo nghĩa typo,
mà là **quá dài**. NVS giới hạn tên key `NVS_KEY_NAME_MAX_SIZE=16` byte KỂ CẢ NUL =>
15 ký tự dùng được. `"osc_address_full"` (16 ký tự), `"osc_address_missing"` (19),
`"osc_value_missing"` (17) đều vượt giới hạn này — `Preferences::putString()`/`getString()`
âm thầm trả về lỗi (không exception, không log), giống hệt "chưa từng lưu". `osc_value_full`
(14 ký tự) vừa đủ nên sống sót — đúng khớp hiện tượng "Full Value còn, Full Address mất".

Nguyên nhân kiến trúc sâu hơn (F29): key NVS dùng CHUNG literal với tên field HTML form
(`server.hasArg(...)` trong web.cpp/html.cpp) — HTTP arg name không giới hạn độ dài, NVS
key thì có. Đổi tên field "cho rõ nghĩa" vô tình phá NVS.

**Fix:** đổi 3 key NVS thành `osc_addr_full`/`osc_addr_miss`/`osc_value_miss` (≤15 ký tự,
TÁCH biệt khỏi tên field HTML/hasArg — không đổi tên field HTML). Thêm macro `NVS_KEY(...)`
(template + `static_assert`) trong `globals.h`, bọc quanh MỌI literal key NVS hiện có để lớp
bug này fail ở BUILD TIME thay vì âm thầm mất data ở field. Thêm `cfg_ver` (schema version,
`CFG_VERSION` trong `globals.h`) — log cảnh báo ở boot nếu version cũ/thiếu, KHÔNG tự xoá NVS
(xem `tools/full_erase.sh` nếu cần xoá tay). `saveDistanceConfig()` giờ trả về số lượng
`put*()` thất bại (F2) thay vì `void`; `handleSave()` trong `web.cpp` phản hồi trung thực
("Saved OK" chỉ khi thật sự OK) thay vì luôn báo "Saved OK".

---

## Bug 2

Nếu Ethernet chưa cắm dây:

```
while(!eth_connected)
```

Firmware bị block.

Cần chuyển sang timeout hoặc non-blocking.

---

## Bug 3

Kiểm tra toàn bộ Preferences.

Đặc biệt:

* key name
* giới hạn buffer
* strcpy()
* char array overflow
* String ↔ char[]
* thứ tự load/save

---

# 9. Điều cần audit

## Memory

Kiểm tra:

* stack usage
* heap usage
* String fragmentation
* char buffer overflow

---

## Preferences

Kiểm tra:

* tất cả key đều có put/get tương ứng
* key name thống nhất
* lỗi copy buffer
* lỗi mất dữ liệu sau reboot

---

## Networking

Đánh giá:

* reconnect
* timeout
* Ethernet callback
* MQTT reconnect
* khả năng block loop()

---

## Web Server

Kiểm tra:

* HTML generation
* form parsing
* URL decode
* buffer overflow
* HTML escaping

---

## MQTT

Kiểm tra:

* reconnect logic
* publish frequency
* retain/QoS (nếu cần)
* mất mạng khi broker offline

---

## Sensor

Kiểm tra:

* polling rate
* timeout
* lỗi I2C
* trường hợp sensor disconnect

---

## Runtime

Đánh giá:

* loop() có bị block
* delay()
* watchdog
* race condition
* callback thread safety

---

# 10. Mục tiêu audit

Không yêu cầu thêm tính năng mới.

Ưu tiên:

1. Tìm bug tiềm ẩn.
2. Tìm lỗi logic.
3. Tìm memory leak.
4. Tìm buffer overflow.
5. Tìm deadlock/blocking.
6. Tìm nguyên nhân mất dữ liệu Preferences.
7. Đề xuất cải tiến kiến trúc nếu cần nhưng giữ nguyên hành vi hiện tại.

---

# 11. Kỳ vọng đầu ra

Sau audit mong muốn có:

* Danh sách lỗi theo mức độ nghiêm trọng.
* Phân tích nguyên nhân gốc (root cause).
* Đề xuất cách sửa cụ thể.
* Đánh giá độ ổn định tổng thể của firmware.
* Khuyến nghị về hiệu năng, độ tin cậy và khả năng bảo trì.
