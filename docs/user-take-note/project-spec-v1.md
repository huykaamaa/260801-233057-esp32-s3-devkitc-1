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

---

# 4. Chức năng hiện có

## Ethernet

* Khởi tạo W5500.
* Chờ kết nối Ethernet.
* Có callback báo trạng thái mạng.

## MQTT

* Kết nối broker.
* Tự reconnect.
* Publish dữ liệu sensor.
* Các thông số cấu hình qua Web UI.

## Web Server

Cho phép cấu hình:

* MQTT Server
* MQTT Port
* MQTT Username
* MQTT Password
* MQTT Topic

Thông số sensor:

* Distance Min
* Distance Max

OSC:

* Address
* Full Address
* Value
* Full Value

Sau khi Save:

* ghi Preferences
* cập nhật biến runtime

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

## Bug 1

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

Cần audit.

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
