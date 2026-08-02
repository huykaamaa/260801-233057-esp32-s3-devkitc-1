# Code Map — Phòng Cân Tim

> Bản đồ code cho developer/session sau. Cập nhật **cùng commit** khi thêm file mới hoặc tách file (xem `CLAUDE.md` mục 1-3, mục Documentation Requirements).

Cập nhật lần cuối: 2026-08-02.

---

## Tổng quan luồng file

```
cantim_mqtt_new.cpp  (setup/loop, đọc RS485, quyết định state)
        │
        ├── globals.h        (extern config/state dùng chung toàn firmware)
        ├── sensor_logic.h/.cpp  (parse dòng RS485, so sánh ngưỡng — pure C++, không phụ thuộc Arduino)
        ├── mqtt.h/.cpp       (MQTT client + gửi OSC)
        ├── web.h/.cpp        (HTTP handler: /data /save /test_mqtt /test_osc)
        └── html.h/.cpp       (render trang cấu hình HTML)
```

Nguyên tắc tách file hiện tại (đã đúng theo mục 2 trong `CLAUDE.md`):
- **Business logic** (parse RS485, so sánh ngưỡng, quyết định FULL/MISSING) nằm ở `sensor_logic.*` — không nhúng trong `html.cpp` hay `web.cpp`.
- **Backend** (`cantim_mqtt_new.cpp`, `mqtt.cpp`, `web.cpp`) tách khỏi **frontend** (`html.cpp`) — `html.cpp` chỉ render, không quyết định nghiệp vụ.

---

## `src/globals.h` (63 dòng)

Header trung tâm — mọi file khác include để dùng chung config/state runtime. Không có business logic, chỉ khai báo `extern` + hằng số pin/timeout.

**Hằng số:**
- `LOG(fmt, ...)` — macro wrapper `Serial.printf` (luôn bật, dùng cho log lỗi/trạng thái quan trọng theo mục 9 CLAUDE.md).
- `ETH_CS/MOSI/MISO/SCK/INT/RST` — chân SPI cho W5500.
- `RS485_RX/TX` — chân UART1 đọc dữ liệu sensor.
- `DEVICE_NUM = 3` — số lượng sensor (3 cảm biến khoảng cách).
- `RS485_TIMEOUT = 5000` (ms) — quá thời gian này không nhận dữ liệu từ 1 sensor → coi là OFFLINE.

**Biến toàn cục (extern, định nghĩa thật trong `cantim_mqtt_new.cpp`):**
- `mqtt`, `mqttConnected`, `mqttEnabled` — handle + trạng thái MQTT client.
- `mqttServer/Port/User/Pass/Topic/FullValue/MissingValue` (`char[]` cố định, KHÔNG dùng `String` — mục 6 CLAUDE.md) — cấu hình MQTT publish, load/save qua Preferences.
- `oscEnabled`, `messengerEnabled`, `oscIp`, `oscPort`, `oscAddressFull`, `oscAddressMissing`, `oscValueFull`, `oscValueMissing` — cấu hình OSC output khi FULL/MISSING.
- `rsDistance[DEVICE_NUM]`, `lastRS485[DEVICE_NUM]` — khoảng cách hiện tại + timestamp lần nhận cuối của từng sensor.
- `sensorEnabled[DEVICE_NUM]` — sensor nào được tính vào điều kiện FULL.
- `distanceMin[DEVICE_NUM]`, `distanceMax[DEVICE_NUM]` — ngưỡng khoảng cách "có người" của từng sensor.
- `lastState`, `actionDone`, `stateTimer`, `confirmTime` — máy trạng thái debounce (xem `checkDistance()` bên dưới).
- `eth_connected` — cờ Ethernet đã có IP hay chưa.

**Hàm:** `saveDistanceConfig()` — định nghĩa trong `cantim_mqtt_new.cpp`, ghi toàn bộ config trên vào Preferences (namespace `"distance"`).

---

## `src/cantim_mqtt_new.cpp` (320 dòng) — entry point

File chính: định nghĩa biến toàn cục thật (khớp `extern` trong `globals.h`), `setup()`, `loop()`, đọc RS485, máy trạng thái FULL/MISSING.

- `WiFiEvent(event)` — callback `WiFi.onEvent`, cập nhật `eth_connected` theo sự kiện `ARDUINO_EVENT_ETH_*`.
- `setup()`:
  1. Khởi động `Serial` (chờ tối đa 2s, không block vô hạn).
  2. `RS485.begin(...)` trên UART1.
  3. `prefs.begin("distance", false)` → load toàn bộ config đã liệt kê ở `globals.h` (mỗi field có `getXxx()` tương ứng với `putXxx()` trong `saveDistanceConfig()` — xem lưu ý Bug 1 bên dưới).
  4. Khởi tạo SPI + Ethernet W5500 (`ETH.begin`). Chờ có IP tối đa `ETH_WAIT_MS = 10000` bằng vòng lặp có timeout dựa trên `millis()` (không phải `while(!eth_connected)` vô hạn — đã fix so với Bug 2 trong spec doc cũ), nếu hết giờ vẫn tiếp tục boot không có mạng.
  5. Đăng ký route HTTP (`/`, `/data`, `/save`, `/test_mqtt`, `/test_osc`), `server.begin()`, `oscUdp.begin(9000)`.
  6. `mqttInit()`.
- `loop()`: `server.handleClient()` (chỉ khi `eth_connected`) → `readRS485()` → `checkDistance()`. Không có `delay()` dài, không block.
- `readRS485()` — đọc byte từ UART1 vào buffer `char[128]` (có kiểm tra tràn, log `"RS485 buffer overflow"` và reset `bufPos` nếu vượt), tách theo `\n`, gọi `parseSensorLine()` (từ `sensor_logic.h`) để tách `id,distance`, ghi vào `rsDistance[]`/`lastRS485[]` qua `isValidDeviceId()`.
- `checkDistance()` — máy trạng thái debounce:
  - Đếm số sensor **enabled** đang trong ngưỡng (`isDistanceInRange()`) và chưa timeout (`RS485_TIMEOUT`).
  - `currentState = true` (FULL) chỉ khi **tất cả** sensor enabled đều trong ngưỡng.
  - Có nhánh `startupWaiting` riêng cho lần đầu sau boot (chờ `confirmTime` ổn định trước khi publish state đầu tiên), sau đó chuyển sang nhánh debounce thường: đổi state → reset `stateTimer`; giữ ổn định đủ `confirmTime` ms → gọi `triggerFull()`/`triggerMissing()` một lần (`actionDone` chặn gọi lặp).
- `saveDistanceConfig()` — ghi toàn bộ config vào Preferences namespace `"distance"` (đối xứng với phần load trong `setup()`).

⚠️ **Lưu ý khi sửa Preferences key:** phần load (`setup()`) và save (`saveDistanceConfig()`) dùng cùng danh sách string key (`"osc_addr_full"`, `"osc_addr_miss"`, `"mqtt_ip"`, ...). Đây từng là nguồn Bug 1 (spec doc §8) — key load/save lệch tên khiến field mất sau reboot. Khi thêm field mới, đối chiếu kỹ 2 vị trí này (mục 4 CLAUDE.md).

---

## `include/sensor_logic.h` + `src/sensor_logic.cpp` (14 + 31 dòng) — pure logic

Logic thuần C++ (`cstdlib`/`cstring` only, **không** include Arduino) tách ra từ `cantim_mqtt_new.cpp` để build/test được trên native toolchain (PC), không cần ETH/WiFi/WebServer/Preferences.

- `parseSensorLine(line, id&, distance&)` — parse chuỗi `"id,distance"` đã trim (vd `"2,455"`), dùng `strtol`/`memchr` thay cho `String::indexOf`/`substring`. Trả `false` nếu không có dấu phẩy hoặc dấu phẩy ở đầu chuỗi.
- `isDistanceInRange(distance, min, max)` — so sánh ngưỡng inclusive (`min <= d <= max`).
- `isValidDeviceId(id, deviceCount)` — kiểm tra `id` là chỉ số 1-based hợp lệ (`1..deviceCount`).

Được gọi từ `readRS485()`/`checkDistance()` trong `cantim_mqtt_new.cpp`, và test trực tiếp trong `test/test_sensor_logic/test_sensor_logic.cpp` qua env `native` trong `platformio.ini`.

---

## `src/mqtt.h` + `src/mqtt.cpp` (8 + 104 dòng)

MQTT client (ESP-IDF `esp_mqtt_client`, không phải PubSubClient) + gửi gói OSC qua UDP.

- `mqttInit()` — build URI `mqtt://server:port`, set client_id `"CAN_TIM"`, username/password nếu có, `esp_mqtt_client_init` + `_register_event` + `_start`. Gọi lại mỗi khi user đổi MQTT server/port/user/pass trên Web UI (`web.cpp::handleSave`).
- `triggerFull()` / `triggerMissing()` — publish `mqttFullValue`/`mqttMissingValue` lên `mqttTopic` (nếu `mqttEnabled` và client đã init), đồng thời gọi `sendOscState()` với address/value tương ứng state. Được gọi từ `checkDistance()` trong `cantim_mqtt_new.cpp` và từ `handleTestMQTT()`/`handleTestOSC()` trong `web.cpp`.
- `mqttEvent(...)` — callback ESP-IDF, cập nhật `mqttConnected` theo `MQTT_EVENT_CONNECTED`/`DISCONNECTED`.
- `sendOscValue()`/`sendOscState()` (static + wrapper) — tự encode packet OSC tối thiểu (address string padded 4-byte + type tag `",i"` + int32 value), không dùng thư viện OSC ngoài. Bỏ qua nếu `!oscEnabled`, IP rỗng, hoặc port = 0.
- `writeOscString()` (static) — helper ghi string kèm zero-padding theo chuẩn OSC.

---

## `src/web.h` + `src/web.cpp` (6 + 190 dòng)

HTTP handler — nhận request, gọi logic có sẵn (`saveDistanceConfig()`, `mqttInit()`, `triggerFull()`), **không** tự chứa business logic tính toán.

- `handleData()` — GET `/data`, trả HTML fragment (trạng thái MQTT/OSC/FULL-MISSING + khoảng cách từng sensor) để trang chủ poll bằng JS (`fetch` mỗi 100ms, xem `html.cpp`).
- `handleSave()` — POST `/save`, đọc toàn bộ field form (luôn `hasArg()` trước khi `arg()` — đúng mục 7 CLAUDE.md), copy vào buffer config bằng `strncpy` + null-terminate, gọi `saveDistanceConfig()`. Nếu MQTT server/port/user/pass đổi → restart MQTT client (`esp_mqtt_client_stop/destroy` rồi `mqttInit()` lại).
- `handleTestMQTT()` / `handleTestOSC()` — POST, gọi `triggerFull()` để test thủ công (cả 2 handler hiện đều gọi `triggerFull()`, chưa có nhánh test riêng MISSING — xem ghi chú audit bên dưới).

⚠️ **Ghi chú audit (chưa fix, chưa xác nhận có phải bug hay cố ý):** `handleTestOSC()` gọi `triggerFull()` giống hệt `handleTestMQTT()` thay vì hàm test OSC riêng — 2 nút test hiện có hành vi giống nhau. Cần hỏi lại chủ dự án trước khi đổi hành vi.

---

## `src/html.h` + `src/html.cpp` (3 + 208 dòng)

Frontend thuần render — không có quyết định nghiệp vụ, chỉ đọc biến config/state đã có sẵn để build chuỗi HTML.

- `handleRoot()` — GET `/`, trả về toàn bộ trang cấu hình (inline CSS + inline JS poll `/data`):
  - Panel **Sensor Configuration** — 1 block/sensor (`DEVICE_NUM` = 3): checkbox enable, MIN/MAX distance.
  - Panel **MQTT Settings** — enable, IP, port, user, password, topic, FULL/MISSING message.
  - Panel **OSC Settings** — enable, IP, port, FULL/MISSING address + value.
  - Panel **Confirm Settings** — thời gian debounce (ms).
  - Nút **SAVE SETTINGS** (POST `/save`), **Test MQTT** / **Test OSC** (POST `/test_mqtt` / `/test_osc`).
  - Div `#d` (trạng thái realtime) được JS `fetch('/data')` cập nhật mỗi 100ms.

⚠️ **Chưa escape HTML khi render lại giá trị user nhập** (mục 7 CLAUDE.md) — các trường như `mqttTopic`, `oscAddressFull`... được nối thẳng (`html += mqttTopic`) vào `value='...'` mà không escape ký tự `'`/`<`/`>`. Nằm trong danh sách audit của `docs/user-take-note/project-spec-v1.md` §9 (Web Server), chưa fix.

---

## `test/test_sensor_logic/test_sensor_logic.cpp`

Unity test cho `sensor_logic.cpp` — chạy trên PC (env `native`), không cần board. Test: parse dòng hợp lệ, thiếu dấu phẩy, dấu phẩy ở đầu, chuỗi rỗng, biên ngưỡng inclusive, id hợp lệ/không hợp lệ.

Chạy: xem lệnh trong `platformio.ini`.

---

## `platformio.ini`

Hai environment:
- **`esp32-s3-devkitc-1`** — build thật cho board (Arduino framework, `pioarduino` platform, `ARDUINO_USB_CDC_ON_BOOT=1`). Build: `pio run -e esp32-s3-devkitc-1`. Flash: thêm `-t upload`.
- **`native`** — build/test trên PC cho phần logic thuần (`sensor_logic.cpp`). Loại trừ mọi `.cpp` phụ thuộc Arduino/ETH/WiFi/WebServer/Preferences (`cantim_mqtt_new.cpp`, `html.cpp`, `mqtt.cpp`, `web.cpp`) khỏi `build_src_filter`. Chạy: `pio test -e native`.

---

## Ghi chú trạng thái (so với `docs/user-take-note/project-spec-v1.md`)

Spec doc gốc mô tả sensor là **VL53L1X qua I2C** — nhưng code thật hiện tại đọc dữ liệu khoảng cách qua **RS485/UART** (biến `RS485`, `rsDistance[]`, giao thức text `"id,distance\n"`), không phải I2C VL53L1X trực tiếp. Nhiều khả năng có 1 module RS485 trung gian đọc VL53L1X rồi gửi text qua UART — **cần xác nhận lại phần cứng thật** trước khi tin theo mô tả I2C trong spec doc.

Ngoài ra, so với danh sách bug ở spec doc §8 (chưa cập nhật trạng thái trong file đó — cần user xác nhận "xong" trước khi đánh dấu theo Definition of Done):
- **Bug 1** (OSC Full Address mất sau reboot) — nguyên nhân là **Preferences key lệch tên** giữa load (`osc_address_full`/`osc_address_missing`) và save (`osc_addr_full`/`osc_addr_miss`); trong code hiện tại 2 phía đã dùng chung key `osc_addr_full`/`osc_addr_miss`, có vẻ đã fix.
- **Bug 2** (`while(!eth_connected)` block vô hạn) — hiện đã có timeout `ETH_WAIT_MS = 10000` trong `setup()`, không còn block vô hạn.
- **Bug 3** (audit toàn bộ Preferences: key name, buffer, strcpy...) — vẫn còn phần dùng `strncpy` cho toàn bộ input form mà **chưa validate độ dài trước khi copy** (mục 6 CLAUDE.md: "Dữ liệu từ Web form → validate độ dài **trước khi** copy vào buffer") — `strncpy` tự cắt bớt nên không tràn buffer, nhưng không cảnh báo user khi giá trị bị cắt.
