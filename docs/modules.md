# Code Map — Phòng Cân Tim

> Bản đồ code cho developer/session sau. Cập nhật **cùng commit** khi thêm file mới hoặc tách file (xem `CLAUDE.md` mục 1-3, mục Documentation Requirements).

Cập nhật lần cuối: 2026-08-03 (thêm module `relay.h`/`relay.cpp` — kích relay reset nguồn node vệ tinh khi sensor OFFLINE).

---

## Tổng quan luồng file

```
cantim_mqtt_new.cpp  (setup/loop, đọc RS485, quyết định state)
        │
        ├── globals.h        (extern config/state dùng chung toàn firmware)
        ├── sensor_logic.h/.cpp  (parse dòng RS485, so sánh ngưỡng — pure C++, không phụ thuộc Arduino)
        ├── mqtt.h/.cpp       (MQTT client + gửi OSC)
        ├── relay.h/.cpp      (kích relay reset nguồn node vệ tinh qua GPIO)
        ├── web.h/.cpp        (HTTP handler: /data /save /test_mqtt /test_osc /test_relay)
        └── html.h/.cpp       (render trang cấu hình HTML)
```

Nguyên tắc tách file hiện tại (đã đúng theo mục 2 trong `CLAUDE.md`):
- **Business logic** (parse RS485, so sánh ngưỡng, quyết định FULL/MISSING) nằm ở `sensor_logic.*` — không nhúng trong `html.cpp` hay `web.cpp`.
- **Backend** (`cantim_mqtt_new.cpp`, `mqtt.cpp`, `web.cpp`) tách khỏi **frontend** (`html.cpp`) — `html.cpp` chỉ render, không quyết định nghiệp vụ.

---

## `src/globals.h` (95 dòng)

Header trung tâm — mọi file khác include để dùng chung config/state runtime. Không có business logic, chỉ khai báo `extern` + hằng số pin/timeout.

**Hằng số:**
- `LOG(fmt, ...)` — macro wrapper `Serial.printf` (luôn bật, dùng cho log lỗi/trạng thái quan trọng theo mục 9 CLAUDE.md).
- `NVS_KEY(s)` — template + `static_assert`, bọc quanh MỌI literal key NVS/Preferences để fail BUILD nếu key >15 ký tự (giới hạn `NVS_KEY_NAME_MAX_SIZE`). Thêm sau Bug 1 (F1/F29) — xem chú thích trong file để hiểu vì sao key NVS phải TÁCH khỏi tên field HTML.
- `CFG_VERSION = 1` — schema version cho namespace Preferences `"distance"`, đối chiếu với key `cfg_ver` lúc boot (xem `setup()`/`saveDistanceConfig()` trong `cantim_mqtt_new.cpp`). Chỉ log cảnh báo, KHÔNG tự xoá NVS.
- `ETH_CS/MOSI/MISO/SCK/INT/RST` — chân SPI cho W5500.
- `RS485_RX_MAIN = 42` / `RS485_RX_BACKUP = 18` — chân RX của **2 module RS485** (chính / dự phòng) đọc dữ liệu sensor, chạy trên UART2. Chỉ một module được đọc tại một thời điểm; chọn qua radio "Nguồn RS485" trên Web UI, lưu NVS key `rs485_bk`, đổi xong áp dụng ngay bằng `rs485ApplyRxPin()` (không cần reboot). GPIO 42 = MTMS (JTAG ngoài) nhưng ESP32-S3 mặc định dùng USB Serial/JTAG tích hợp nên 42 là GPIO thường; không phải chân strapping (0/3/45/46), không dính SPI flash nội (26-32) hay PSRAM octal (33-37).
- `rs485UseBackup` / `rs485ActiveRxPin()` / `rs485ApplyRxPin()` — cờ chọn nguồn + chân đang dùng + hàm `end()`+`begin()` lại UART. Xem `cantim_mqtt_new.cpp`.
- `otaUrl[96]` (NVS key `ota_url`) / `otaUrlPending` / `otaUrlTick()` (2026-08-10) — OTA kéo firmware từ URL đã lưu, dùng `HTTPUpdate` có sẵn trong core (không thêm `lib_deps`). Route `/update_url` (`handleUpdateUrl()` trong `web.cpp`, gated `requireAuth()`) chỉ **đặt cờ**; `otaUrlTick()` ở **cuối** `loop()` mới thực sự tải, và còn chờ thêm 500ms cho response rời socket — gọi `httpUpdate.update()` thẳng trong handler thì nó chặn ~20s rồi reboot giữa chừng, trình duyệt báo lỗi mạng dù update chạy đúng. Chỉ nhận `http://`, `https://` bị từ chối ngay lúc lưu (cần TLS + chứng chỉ, ~100KB flash). URL dài quá `sizeof(otaUrl)` cũng bị từ chối chứ không cắt cụt — URL cụt vẫn hợp lệ cú pháp nhưng trỏ chỗ khác, lỗi chỉ lộ ra lúc đang tải.
- `RS485_TX = -1` — không gán chân TX. Thiết bị **chỉ nghe**: node cảm biến tự đẩy số đo lên, firmware không bao giờ phát gì ra bus.
- `BTN_MANUAL_PIN = 1` / `BTN_MANUAL_PIN2 = 2` / `BTN_MANUAL_COUNT = 2` — **2 nút nhấn tay song song**, chức năng y hệt nhau (`INPUT_PULLUP`, nhấn = kéo xuống mass). Mỗi nút có bộ debounce riêng chứ không OR chung mức đọc 2 chân: nút chập/kẹt ở mức nhấn sẽ không ghim chết đường tín hiệu và làm nút kia vô dụng.
- `MANUAL_STEP_HOLD_MS = 5000` + `manualStepAt` — hai bước bấm tay liên tiếp phải cách nhau ít nhất 5s, **cả chiều vào lẫn chiều ra** (vào chế độ tay rồi mới thoát được, và thoát xong mới kích FULL lại được). Bấm trong khoảng này bị **bỏ qua hẳn**, không xếp hàng lại. Chặn cú bấm nhân hai lần / hai người cùng bấm bắn 2 cue ngược nhau cách nhau vài phần giây. `manualStepAt == 0` (chưa bấm lần nào kể từ boot) = không khoá, nếu không thì 5 giây đầu sau khi cấp điện nút sẽ câm như hỏng. Xem `manualToggleStep()` trong `cantim_mqtt_new.cpp`; `handleData()` hiện đếm ngược ở băng cam (đang trong chế độ tay) hoặc băng xám (vừa thoát ra).
- `DEVICE_NUM = 3` — số lượng sensor (3 cảm biến khoảng cách).
- `RS485_TIMEOUT = 5000` (ms) — quá thời gian này không nhận dữ liệu từ 1 sensor → coi là OFFLINE.

**Biến toàn cục (extern, định nghĩa thật trong `cantim_mqtt_new.cpp`):**
- `mqtt`, `mqttConnected`, `mqttEnabled` — handle + trạng thái MQTT client.
- `mqttServer/Port/User/Pass/Topic/FullValue/MissingValue` (`char[]` cố định, KHÔNG dùng `String` — mục 6 CLAUDE.md) — cấu hình MQTT publish, load/save qua Preferences.
- `oscEnabled`, `oscIp`, `oscPort`, `oscAddressFull`, `oscAddressMissing`, `oscValueFull`, `oscValueMissing` — cấu hình OSC output khi FULL/MISSING.
- `rsDistance[DEVICE_NUM]`, `lastRS485[DEVICE_NUM]` — khoảng cách hiện tại + timestamp lần nhận cuối của từng sensor.
- `sensorEnabled[DEVICE_NUM]` — sensor nào được tính vào điều kiện FULL.
- `sensorOfflineAlerted[DEVICE_NUM]` (2026-08-03) — đã gửi MQTT cảnh báo OFFLINE cho sensor này trong lần mất tín hiệu hiện tại chưa; reset về `false` ngay khi sensor online trở lại (xem `checkDistance()` bên dưới) để lần rớt tiếp theo lại được cảnh báo.
- `distanceMin[DEVICE_NUM]`, `distanceMax[DEVICE_NUM]` — ngưỡng khoảng cách "có người" của từng sensor.
- `lastState`, `actionDone`, `stateTimer`, `confirmTime` — máy trạng thái debounce (xem `checkDistance()` bên dưới).
- `confirmTimeMissing` (2026-08-03) — confirm time riêng dùng khi `currentState == false` (đang xác nhận MISSING); `confirmTime` giờ chỉ áp dụng cho FULL. Mặc định `2000` (2x mặc định `confirmTime = 1000`), NVS key `confirm_miss`, sửa qua panel Confirm Settings.
- `publishedState` — trạng thái FULL/MISSING **đã thực sự publish** (MQTT/OSC) lần gần nhất, tách riêng khỏi `lastState` (chỉ track input debounce thô). Thêm sau finding 4.1: dùng `lastState` làm proxy cho "đã publish gì" khiến nhiễu quanh ngưỡng có thể re-fire cùng 1 cue FULL/MISSING dù chưa có thay đổi occupancy thật. `checkDistance()` giờ gate việc gọi `triggerFull()`/`triggerMissing()` bằng `currentState != publishedState` **cộng thêm** debounce `actionDone`/`confirmTime` cũ, không thay thế nó.
- `eth_connected` — cờ Ethernet đã có IP hay chưa (set qua `WiFiEvent()` khi DHCP thành công, HOẶC set thẳng trong `setup()` khi rơi vào static-IP fallback — xem `ethStaticIp` bên dưới, `ETH.config()` không tự bắn `ARDUINO_EVENT_ETH_GOT_IP`).
- `diagApActive` (bool), `diagApStartMs` (unsigned long), `DIAG_AP_DURATION_MS` (const, 5 phút) — file-local `static` trong `cantim_mqtt_new.cpp` (không extern, không dùng ở file khác). State cho WiFi AP chẩn đoán, xem `startDiagAp()` bên dưới.
- `authUser[32]`, `authPass[32]` — username/password cho HTTP Basic Auth trên `/save`, `/test_mqtt`, `/test_osc` (F6). Mặc định `"admin"`/`"admin"`, log rõ ra Serial mỗi lần boot nếu vẫn còn giá trị mặc định. Load/save qua NVS key `auth_user`/`auth_pass`, sửa qua panel **Admin Auth** trên Web UI (`html.cpp`).
- `ethStaticIp[16]`, `ethStaticGateway[16]`, `ethStaticNetmask[16]` — địa chỉ IP tĩnh dùng khi DHCP không thành công sau `ETH_WAIT_MS` (F19), hoặc dùng ngay từ đầu nếu `ethUseStaticFirst` bật (xem dưới). Mặc định `192.168.99.199` / gateway `192.168.99.1` / netmask `255.255.255.0`. NVS key `eth_ip`/`eth_gw`/`eth_mask`, load/save đầy đủ, sửa được qua panel **Mạng (Ethernet)** trên Web UI (`html.cpp`) — xem field `eth_ip`/`eth_gw`/`eth_mask` trong `web.cpp::handleSave()`.
- `ethUseStaticFirst` (bool) — **"Ưu tiên IP tĩnh"** (2026-08-03, port từ project `gia_sach`): bật thì `setup()` áp `ethStaticIp`/`ethStaticGateway`/`ethStaticNetmask` ngay lập tức, bỏ qua hoàn toàn vòng chờ DHCP (`ETH_WAIT_MS`); IP/gateway/netmask parse lỗi thì tự lùi về nhánh DHCP-rồi-fallback cũ, không kẹt. Mặc định `false`. NVS key `eth_first`, sửa qua checkbox cùng tên trên tab Mạng.
- `RELAY_PIN_COUNT` (4), `relayPins[4]` (`const uint8_t`, giá trị cố định `{4,5,6,7}`, không đổi qua Web UI), `relayPinEnabled[4]` (chân nào được tick), `relayActiveHigh` (true=kích HIGH, false=kích LOW), `relayPulseMs` (thời gian giữ mức kích trước khi tự nhả, clamp `[200,30000]`) (2026-08-03) — cấu hình cho tính năng kích relay reset nguồn node vệ tinh, xem `relay.h`/`relay.cpp` và `checkDistance()` bên dưới. NVS key `relay_p0..relay_p3`/`relay_hi`/`relay_ms`.

**Hàm:** `int saveDistanceConfig()` — định nghĩa trong `cantim_mqtt_new.cpp`, ghi toàn bộ config trên vào Preferences (namespace `"distance"`). Trả về số lượng `put*()` thất bại (0 = OK hết), hoặc `-1` nếu `prefs.begin()` thất bại (chưa ghi được gì) — đổi từ `void` sau F2 để `handleSave()` trong `web.cpp` biết thật sự đã lưu hay chưa.

---

## `src/cantim_mqtt_new.cpp` (~563 dòng, đã vượt soft-cap 400 dòng CLAUDE.md mục 3 — chưa vượt hard cap 600, chưa bắt buộc tách nhưng cân nhắc cho task sau) — entry point

File chính: định nghĩa biến toàn cục thật (khớp `extern` trong `globals.h`), `setup()`, `loop()`, đọc RS485, máy trạng thái FULL/MISSING.

- `WiFiEvent(event)` — callback `WiFi.onEvent`, cập nhật `eth_connected` theo sự kiện `ARDUINO_EVENT_ETH_*`.
- `startDiagAp(isFallback)` (static) — bật WiFi SoftAP tạm thời, SSID `"CANTIM-DHCP-" + ip` (DHCP thành công) hoặc `"CANTIM-STATIC-" + ip` (rơi vào F19 fallback) tuỳ tham số `isFallback` do caller truyền vào (không mật khẩu, chỉ để đọc tên mạng, không cần kết nối thật) — tiền tố khác nhau để phân biệt IP thật (DHCP) với IP tĩnh cố định `192.168.99.199`, chỉ nhìn IP không thôi không biết được đường nào. Gọi 1 lần từ `setup()` ngay sau khi `eth_connected == true`, với `ethFallbackUsed` (biến local trong `setup()`, set `true` đúng lúc nhánh `ETH.config()` fallback thành công) làm tham số. Mục đích: kỹ thuật viên đọc IP thiết bị từ danh sách WiFi trên điện thoại, khỏi cần cắm USB + mở Serial Monitor. `loop()` tự tắt AP (`WiFi.softAPdisconnect(true)`) sau `DIAG_AP_DURATION_MS` (5 phút) để không tốn RF/điện liên tục sau khi qua giai đoạn chẩn đoán.
- `setup()`:
  1. Khởi động `Serial` (chờ tối đa 2s, không block vô hạn).
  2. `prefs.begin("distance", false)` (kiểm tra return value — thất bại thì log lỗi, GIỮ default trong RAM, không load) → load toàn bộ config đã liệt kê ở `globals.h` qua key bọc `NVS_KEY(...)` (mỗi field có `getXxx()` tương ứng với `putXxx()` trong `saveDistanceConfig()` — xem lưu ý Bug 1 bên dưới), gồm cả `authUser/authPass` và `ethStaticIp/Gateway/Netmask`. Đối chiếu `cfg_ver` với `CFG_VERSION` (`globals.h`), log cảnh báo nếu cũ/thiếu (không tự xoá). Nếu `authUser`/`authPass` vẫn là default `"admin"`/`"admin"` sau load, log rõ dòng cảnh báo ra Serial mỗi lần boot (F6) — thiết bị KHÔNG bao giờ âm thầm chạy với default không ai biết.
  3. `rs485ApplyRxPin()` — `RS485.begin(115200, SERIAL_8N1, ...)` trên UART2, chân RX lấy theo `rs485UseBackup` vừa load ở bước 2 (mặc định module chính, GPIO 42). Cố ý đặt SAU bước đọc NVS: mở UART sớm hơn thì mỗi lần boot với module dự phòng đều phải `end()`+`begin()` lại ngay, thừa một lần và làm người đọc tưởng chân chính mới là chân thật sự đang dùng.
  4. Khởi tạo SPI + Ethernet W5500 (`ETH.begin`). Nếu `ethUseStaticFirst` bật (2026-08-03, "Ưu tiên IP tĩnh") → thử `ETH.config(ip, gw, mask, gw)` áp IP tĩnh **ngay lập tức**, bỏ qua hẳn bước chờ DHCP bên dưới; parse lỗi hoặc `ETH.config()` fail thì log rồi rơi tiếp xuống nhánh DHCP như bình thường (không kẹt). Nhánh DHCP: chờ có IP tối đa `ETH_WAIT_MS = 10000` bằng vòng lặp có timeout dựa trên `millis()` (không phải `while(!eth_connected)` vô hạn — đã fix so với Bug 2 trong spec doc cũ). Hết giờ mà vẫn chưa có IP (không tìm thấy DHCP server) → gọi `ETH.config(ethStaticIp, ethStaticGateway, ethStaticNetmask, ethStaticGateway)` áp IP tĩnh fallback (F19, mặc định `192.168.99.199`/gw `192.168.99.1`/mask `255.255.255.0`; gateway truyền thêm làm DNS1 — F34, trước đó DNS để trống khiến broker nhập bằng hostname không resolve được ở nhánh này), set `eth_connected = true` thủ công (`ETH.config()` không tự bắn `ARDUINO_EVENT_ETH_GOT_IP` — sự kiện đó chỉ tới từ nhánh DHCP-client) rồi tiếp tục boot; nếu vẫn không parse được các địa chỉ static (chuỗi NVS hỏng) thì log lỗi và boot tiếp không mạng, giống hành vi cũ.
  5. Đăng ký route HTTP (`/`, `/data`, `/save`, `/test_mqtt`, `/test_osc`, `/test_relay`), `server.begin()`, `oscUdp.begin(9000)`.
  6. `mqttInit()`.
  7. `relayInit()` (2026-08-03, `relay.h`) — `pinMode(OUTPUT)` cho 4 chân `relayPins[]` + đưa về mức nghỉ theo `relayActiveHigh` hiện tại. Gọi trước `WiFi.onEvent`/`ETH.begin` trong `setup()`.
- `loop()`: `server.handleClient()` (chỉ khi `eth_connected`) → kiểm tra tắt Diag AP nếu quá `DIAG_AP_DURATION_MS` (so `millis()`, không block) → `readRS485()` → `checkDistance()` → `relayTick()` (2026-08-03, tự nhả xung relay sau `relayPulseMs`, xem `relay.h`). Không có `delay()` dài, không block.
- `readRS485()` — đọc byte từ UART2 (module RS485 đang được chọn) vào buffer `char[128]` (có kiểm tra tràn, log `"RS485 buffer overflow"` và reset `bufPos` nếu vượt), tách theo `\n`, gọi `parseSensorLine()` (từ `sensor_logic.h`) để tách `id,distance`, ghi vào `rsDistance[]`/`lastRS485[]` qua `isValidDeviceId()`. Thêm inter-byte timeout 200ms (`RS485_INTERBYTE_TIMEOUT_MS`, F27/4.4): nếu byte mới tới cách byte trước đó >200ms trong khi buffer đang có 1 dòng dở (chưa gặp `\n`), coi phần dở đó là "stale" (log rồi drop) trước khi append byte mới, thay vì âm thầm nối nó vào đầu dòng kế tiếp và làm hỏng cả 2 dòng.
- `checkDistance()` — máy trạng thái debounce:
  - Đếm số sensor **enabled** đang trong ngưỡng (`isDistanceInRange()`) và chưa timeout (`RS485_TIMEOUT`). Sensor **vừa chuyển** sang timeout (offline) gọi `triggerSensorOffline(i+1)` (`mqtt.cpp`) **và** `relayTrigger()` (`relay.h`, 2026-08-03) đúng 1 lần (gate bằng `sensorOfflineAlerted[i]`, xem `globals.h`) trước khi bị loại khỏi `requiredCount`. `relayTrigger()` tự no-op nếu không chân relay nào được tick hoặc đang giữa 1 xung khác.
  - `currentState = true` (FULL) chỉ khi **tất cả** sensor enabled đều trong ngưỡng.
  - `activeConfirmTime = currentState ? confirmTime : confirmTimeMissing` (2026-08-03) — MISSING dùng ngưỡng thời gian riêng, mặc định gấp đôi FULL, để giảm khả năng bắn nhầm MISSING khi người chỉ tạm rời sensor trong chốc lát.
  - Có nhánh `startupWaiting` riêng cho lần đầu sau boot (chờ `activeConfirmTime` ổn định trước khi publish state đầu tiên), sau đó chuyển sang nhánh debounce thường: đổi state → reset `stateTimer`; giữ ổn định đủ `activeConfirmTime` ms **và** `currentState != publishedState` → gọi `triggerFull()`/`triggerMissing()` một lần, rồi set `publishedState = currentState` (`actionDone` vẫn chặn gọi lặp trong cùng 1 lần ổn định; `publishedState` chặn thêm trường hợp nhiễu raw quanh ngưỡng làm `actionDone` bị reset dù chưa có gì mới để publish — finding 4.1). Cả 2 nhánh (`startupWaiting` và steady-state) đều ghi `publishedState`.
- `int saveDistanceConfig()` — `prefs.begin("distance", false)` (kiểm tra return value, trả `-1` sớm nếu fail); ghi toàn bộ config vào Preferences qua key bọc `NVS_KEY(...)` (đối xứng với phần load trong `setup()`), đếm số `put*()` trả về 0-byte-written coi là lỗi (ngoại lệ: `putString()` trên field cho phép rỗng như `mqtt_user`/`mqtt_pass` không tính là lỗi khi giá trị thật sự rỗng); ghi thêm `cfg_ver = CFG_VERSION`; trả về tổng số lỗi.

⚠️ **Lưu ý khi sửa Preferences key:** phần load (`setup()`) và save (`saveDistanceConfig()`) dùng cùng danh sách string key (`"osc_addr_full"`, `"osc_addr_miss"`, `"mqtt_ip"`, ...), mỗi literal PHẢI bọc `NVS_KEY(...)` (định nghĩa ở `globals.h`) để build fail nếu >15 ký tự thay vì âm thầm mất data. Đây từng là nguồn Bug 1 (spec doc §8, root cause: key NVS trùng tên field HTML và vượt giới hạn 15 ký tự — xem F29) — key load/save lệch tên hoặc quá dài khiến field mất sau reboot. Khi thêm field mới, đối chiếu kỹ 2 vị trí này (mục 4 CLAUDE.md), và KHÔNG dùng lại tên field HTML/hasArg làm key NVS.

---

## `include/sensor_logic.h` + `src/sensor_logic.cpp` (14 + 31 dòng) — pure logic

Logic thuần C++ (`cstdlib`/`cstring` only, **không** include Arduino) tách ra từ `cantim_mqtt_new.cpp` để build/test được trên native toolchain (PC), không cần ETH/WiFi/WebServer/Preferences.

- `parseSensorLine(line, id&, distance&)` — parse chuỗi `"id,distance"` đã trim (vd `"2,455"`), dùng `strtol`/`memchr` thay cho `String::indexOf`/`substring`. Trả `false` nếu không có dấu phẩy hoặc dấu phẩy ở đầu chuỗi. 4.5: kiểm tra `endptr` của cả 2 lần gọi `strtol()` (id và distance) — trả `false` nếu 1 trong 2 field không tiêu thụ được digit nào (vd payload rác `"1,"`, `"1,ERR"`, `"1,NaN"`, `"1,OK,250"`), thay vì để `strtol()` âm thầm trả về `0` và bị hiểu nhầm thành 1 reading thật (khoảng cách 0mm).
- `isDistanceInRange(distance, min, max)` — so sánh ngưỡng inclusive (`min <= d <= max`).
- `isValidDeviceId(id, deviceCount)` — kiểm tra `id` là chỉ số 1-based hợp lệ (`1..deviceCount`).

Được gọi từ `readRS485()`/`checkDistance()` trong `cantim_mqtt_new.cpp`, và test trực tiếp trong `test/test_sensor_logic/test_sensor_logic.cpp` qua env `native` trong `platformio.ini`.

---

## `src/mqtt.h` + `src/mqtt.cpp` (8 + 104 dòng)

MQTT client (ESP-IDF `esp_mqtt_client`, không phải PubSubClient) + gửi gói OSC qua UDP.

- `mqttInit()` — build URI `mqtt://server:port`, set client_id `"CAN_TIM"`. Username **và** password chỉ set khi `mqttUser` khác rỗng (F32, 2026-08-03: password chỉ gửi kèm khi username không rỗng, không còn độc lập với nhau nữa) — vì ô Password trên Web UI không còn round-trip giá trị đã lưu (xem `html.cpp`/`web.cpp` bên dưới), xóa trắng Username qua form là cách duy nhất để chuyển hẳn sang kết nối anonymous. `esp_mqtt_client_init` + `_register_event` + `_start`. Gọi lại mỗi khi user đổi MQTT server/port/user/pass trên Web UI (`web.cpp::handleSave`).
- `triggerFull()` / `triggerMissing()` — publish `mqttFullValue`/`mqttMissingValue` lên `mqttTopic`, đồng thời gọi `sendOscState()` với address/value tương ứng state. Publish MQTT chỉ chạy khi **cả** `mqttEnabled` **và** `mqtt && mqttConnected` (F3 — trước đây chỉ check `mqtt` khác NULL, publish trong lúc client tồn tại nhưng chưa/không còn connected sẽ bị esp-mqtt âm thầm drop; giờ log rõ "not connected" vs "not initialized" cho 2 trường hợp riêng). Dùng `esp_mqtt_client_enqueue(..., store=true)` thay vì `esp_mqtt_client_publish()` (F14/4.2 — bản `publish()` block trên `api_lock` với `INFINITE` wait, có thể treo `loop()` vài giây khi broker đang ở trạng thái half-open/reconnecting; `enqueue()` không block). Được gọi từ `checkDistance()` trong `cantim_mqtt_new.cpp` và từ `handleTestMQTT()`/`handleTestOSC()` trong `web.cpp`.
- `mqttEvent(...)` — callback ESP-IDF, cập nhật `mqttConnected` theo `MQTT_EVENT_CONNECTED`/`DISCONNECTED`. Lưu ý: `esp_mqtt_client_stop()`/`_destroy()` (gọi từ `web.cpp::handleSave()` khi user đổi broker) **không** bắn `MQTT_EVENT_DISCONNECTED` qua callback này — `handleSave()` phải tự set `mqttConnected = false` sau khi destroy (4.8), xem mục `web.cpp` bên dưới.
- `triggerSensorOffline(deviceId)` (2026-08-03) — cảnh báo MQTT riêng khi 1 sensor rớt RS485, **topic/payload cố định, không qua NVS/Web UI**: topic `"<mqttTopic>/error"`, payload `"SENSOR_<deviceId>_OFFLINE"`. Cùng pattern `mqttEnabled`/`mqttConnected` guard + `enqueue()` non-blocking như `triggerFull()`/`triggerMissing()`. Gọi từ `checkDistance()` trong `cantim_mqtt_new.cpp` khi phát hiện cạnh online→offline (không publish OSC, chỉ MQTT).
- `sendOscValue()`/`sendOscState()` (static + wrapper) — tự encode packet OSC tối thiểu (address string padded 4-byte + type tag `",i"` + int32 value big-endian), không dùng thư viện OSC ngoài. Bỏ qua nếu `!oscEnabled`, IP rỗng, hoặc port = 0. Int32 value được byte-swap thủ công sang big-endian trước khi ghi (F9 — ESP32 native little-endian, OSC 1.0 bắt buộc network byte order; trước fix, receiver chuẩn decode value=1 thành 16777216). Có kiểm tra phòng thủ address phải bắt đầu bằng `/` (4.9, xem thêm `web.cpp::handleSave` — validate chính nằm ở đó, đây chỉ là lớp chặn cuối).
- `writeOscString()` (static) — helper ghi string kèm zero-padding theo chuẩn OSC. Công thức padding là `4 - (len % 4)` (luôn ra 1..4 byte đệm) — **không phải** `(4 - (len % 4)) % 4` như bản cũ (F10: công thức cũ ra `0` byte đệm khi `len` là bội số của 4, tức là bỏ mất luôn NUL terminator, hỏng khoảng 1/4 số OSC address tuỳ độ dài, vd `"/composition/layers/1/clips/1/select"` 36 ký tự).

---

## `src/relay.h` + `src/relay.cpp` (2026-08-03)

Module điều khiển GPIO thuần (không phụ thuộc MQTT/Web) — kích 1 xung trên các chân đã tick
chọn để trigger relay cắt/nối nguồn cho các node vệ tinh RS485 khi 1 sensor báo OFFLINE, thay
thế thao tác "mở tủ, cắt nguồn tay" (xem `docs/user-take-note/project-spec-v1.md` §6). Biến
config (`relayPins[]`, `relayPinEnabled[]`, `relayActiveHigh`, `relayPulseMs`) khai báo `extern`
ở `globals.h`, định nghĩa + load/save NVS tập trung ở `cantim_mqtt_new.cpp` (đúng pattern mọi
field config khác trong project này) — file này chỉ chứa logic điều khiển chân.

- `relayInit()` — `pinMode(OUTPUT)` cho cả 4 chân + đưa về mức nghỉ (`idleLevel()`). Gọi 1 lần trong `setup()`.
- `relaySyncIdleLevel()` — áp lại mức nghỉ cho cả 4 chân, bỏ qua (không làm gì) nếu đang giữa 1 xung thật (`relayPulseActive`). Gọi từ `web.cpp::handleSave()` ngay sau khi cập nhật `relayPinEnabled[]`/`relayActiveHigh` — cần thiết vì đổi `relayActiveHigh` giữa chừng làm định nghĩa "mức nghỉ" đảo ngược, không sync lại thì 1 chân đang nghỉ theo định nghĩa cũ sẽ hoá thành đang kích theo định nghĩa mới mà không có xung nào chạy qua để tự sửa.
- `relayTrigger()` — kích mức `activeLevel()` lên các chân có `relayPinEnabled[p] == true`, bắt đầu đếm `relayPulseStartMs`. No-op nếu không chân nào được tick, hoặc đang giữa 1 xung khác (`relayPulseActive`) — tránh xung chồng xung khi nhiều sensor cùng báo offline gần nhau (không kéo dài thời gian xung, không kích lại từ đầu).
- `relayTick()` — gọi mỗi vòng `loop()`; nếu đang giữa xung và đã quá `relayPulseMs` thì đưa cả 4 chân về mức nghỉ, tắt `relayPulseActive`.

Gọi `relayTrigger()` từ `checkDistance()` (`cantim_mqtt_new.cpp`) cùng chỗ với `triggerSensorOffline()`, và từ `handleTestRelay()` (`web.cpp`) cho nút Test Relay trên Web UI.

---

## `src/web.h` + `src/web.cpp` (7 + ~457 dòng, đã vượt soft-cap 400 dòng CLAUDE.md mục 3)

HTTP handler — nhận request, gọi logic có sẵn (`saveDistanceConfig()`, `mqttInit()`, `triggerFull()`), **không** tự chứa business logic tính toán.

- `requireAuth()` (static) — F6: gate cho các endpoint thay đổi state (`/save`, `/test_mqtt`, `/test_osc`). Gọi `server.authenticate(authUser, authPass)`; thất bại thì tự gọi `server.requestAuthentication()` (trả 401 kèm header `WWW-Authenticate`, trình duyệt hiện popup Basic Auth) và trả `false` — mọi handler dùng nó phải `return` ngay khi nhận `false`, không làm gì thêm. GET `/` (`html.cpp::handleRoot`) và GET `/data` (`handleData()` bên dưới) **cố ý không** gọi hàm này — 2 route đó chỉ đọc/render trạng thái, gate chúng sẽ phá luôn cơ chế dashboard tự refresh.
- `parseValidatedLong()` (static) — F16: parse 1 field số dạng `String` sang `long`, chỉ chấp nhận nếu nằm trong `[minVal, maxVal]`; dùng chung cho validate `mqtt_port`/`osc_port` (range `1..65535`) để tránh vừa lỗi "`String::toInt()` trả 0 cho input rác" vừa lỗi "truncate khi ép kiểu hẹp hơn" (vd `"65537"` từng thành `uint16_t` = `1`).
- `handleData()` — GET `/data`, trả HTML fragment (trạng thái MQTT/OSC/FULL-MISSING + khoảng cách từng sensor) để trang chủ poll bằng JS (`fetch` mỗi 100ms, xem `html.cpp`). F17: nếu **không sensor nào đang enable** (`sensorEnabled[i]` toàn `false`), hiện badge cam riêng **"[!] NO SENSORS ENABLED"** thay vì badge đỏ MISSING thông thường — `checkDistance()` không đổi (vẫn hard-lock `currentState = false`/MISSING khi `requiredCount == 0`), đây chỉ là phân biệt hiển thị để operator nhận ra "chưa cấu hình" khác với "phòng thật sự trống".
- `handleSave()` — POST `/save`, **yêu cầu `requireAuth()` pass trước tiên** (F6). Đọc toàn bộ field form (luôn `hasArg()` trước khi `arg()` — đúng mục 7 CLAUDE.md), copy vào buffer config bằng `strncpy` + null-terminate, gọi `saveDistanceConfig()`. Validate trước khi accept, reject (không lưu, không âm thầm sửa) nếu sai thay vì lưu giá trị hỏng:
  - `mqtt_port`/`osc_port` — phải là số nguyên `1..65535` (F16, qua `parseValidatedLong()`).
  - `min{i}`/`max{i}` mỗi sensor — phải `min <= max`, đọc cả 2 nửa trước khi quyết định reject để không lưu nửa cặp (F16).
  - `osc_address_full`/`osc_address_missing` — phải bắt đầu bằng `/` nếu không rỗng (4.9, chuẩn OSC 1.0).
  - `confirm` (ms) — **clamp** (không reject) vào khoảng `[50, 60000]` trước khi gán vào `confirmTime` (kiểu `unsigned long`) (F15 — trước đây `"-1"` qua `String::toInt()` gán thẳng vào biến unsigned sẽ wrap thành `4294967295`, âm thầm vô hiệu hoá toàn bộ trigger FULL/MISSING).
  - `confirm_miss` (ms, 2026-08-03) — cùng logic clamp `[50, 60000]`, gán vào `confirmTimeMissing` riêng (xem `checkDistance()` ở `cantim_mqtt_new.cpp`).
  - `auth_user`/`auth_pass` — field mới (F6), chỉ ghi đè khi non-empty (đổi user không bắt buộc phải gõ lại pass và ngược lại).
  - `mqtt_ip` (F33, 2026-08-03) — chỉ ghi đè khi non-empty; trước fix, submit rỗng nhét thẳng vào `mqttServer` biến URI thành `mqtt://:1883`, khiến `esp_mqtt_client_init()` fail và MQTT chết hẳn cho tới khi sửa lại qua chính form này.
  - `mqtt_pass` (F32, 2026-08-03) — chỉ ghi đè khi non-empty, cùng pattern "để trống = giữ nguyên" với `auth_pass` (xem `html.cpp` bên dưới: ô này không còn hiện lại giá trị đã lưu nữa).
  - `eth_ip`/`eth_gw`/`eth_mask` — panel **Mạng (Ethernet)** trên Web UI, ghi vào `ethStaticIp`/`ethStaticGateway`/`ethStaticNetmask` (F19 static-IP fallback). Validate bằng `IPAddress::fromString()`, reject (không lưu) nếu không parse được thành IPv4 hợp lệ — cùng pattern "validate trước khi copy" với các field text khác.
  - `eth_static_first` (checkbox, 2026-08-03) — đọc qua `server.hasArg(...)` giống các checkbox khác (`mqtt_enable`, `osc_enable`), ghi thẳng vào `ethUseStaticFirst`.
  - `relay_p0..relay_p3` (checkbox, 2026-08-03) — chân nào được tick trên panel "Relay Reset", ghi vào `relayPinEnabled[]`. `relay_active_high` (checkbox) → `relayActiveHigh`. `relay_ms` — **clamp** (không reject) vào `[200, 30000]` → `relayPulseMs`, cùng kiểu xử lý với `confirm`/`confirm_miss` (F15). Sau khi cập nhật 2 field trên, gọi `relaySyncIdleLevel()` (`relay.h`) ngay lập tức để áp lại mức nghỉ theo cấu hình MỚI trước khi `saveDistanceConfig()` — quan trọng vì đổi `relay_active_high` giữa chừng đảo ngược định nghĩa "mức nghỉ" (xem `relay.cpp`).
  Nếu MQTT server/port/user/pass đổi → restart MQTT client (`esp_mqtt_client_stop/destroy` rồi `mqttInit()` lại), và set `mqttConnected = false` ngay sau destroy (4.8 — destroy không tự bắn `MQTT_EVENT_DISCONNECTED` nên UI sẽ kẹt ở "CONNECTED" nếu không set tay). Phản hồi JS `alert(...)` dựa trên số lỗi `saveDistanceConfig()` trả về (F2) — "Saved OK" chỉ khi thật sự 0 lỗi, ngược lại báo số lỗi hoặc "Save FAILED", cộng thêm ghi chú field nào bị reject nếu có (OSC address / MQTT port / OSC port / sensor min-max).
- `syncStateMachineAfterTestTrigger()` (static) — 4.6: set `lastState = publishedState = actionDone = true` + reset `stateTimer`, gọi sau khi Test button fire `triggerFull()`. Trước fix, bấm Test trong lúc máy đang ở MISSING khiến `checkDistance()` vẫn nghĩ "đã publish MISSING" (không hề biết vừa test-publish FULL) và giữ `actionDone=true` cho state cũ — receiver kẹt ở FULL cho tới lần chuyển trạng thái thật kế tiếp.
- `handleTestMQTT()` / `handleTestOSC()` — POST, cũng yêu cầu `requireAuth()` (F6). Gọi `triggerFull()` để test thủ công rồi `syncStateMachineAfterTestTrigger()` (cả 2 handler vẫn cùng gọi `triggerFull()`, chưa có nhánh test riêng MISSING — xem ghi chú audit bên dưới, KHÔNG đổi trong cluster này).
- `handleTestRelay()` (2026-08-03) — POST, yêu cầu `requireAuth()` (F6). Gọi thẳng `relayTrigger()` (`relay.h`) để kích thử relay thủ công — không đụng state machine FULL/MISSING (khác với 2 handler test ở trên), vì đây chỉ là test phần cứng GPIO/relay, không liên quan occupancy.

⚠️ **Ghi chú audit (chưa fix, chưa xác nhận có phải bug hay cố ý):** `handleTestOSC()` gọi `triggerFull()` giống hệt `handleTestMQTT()` thay vì hàm test OSC riêng — 2 nút test hiện có hành vi giống nhau. Cần hỏi lại chủ dự án trước khi đổi hành vi.

---

## `src/html.h` + `src/html.cpp` (3 + ~329 dòng)

Frontend thuần render — không có quyết định nghiệp vụ, chỉ đọc biến config/state đã có sẵn để build chuỗi HTML.

- `htmlEscape()` (static) — F7: escape `&`, `<`, `>`, `"`, `'` trước khi nối 1 giá trị config do user nhập vào HTML. Áp dụng tại các điểm interpolate `value='...'` (`mqttServer`, `mqttUser`, `mqttTopic`, `mqttFullValue`, `mqttMissingValue`, `oscIp`, `oscAddressFull`, `oscAddressMissing`, `authUser`, `ethStaticIp`/`ethStaticGateway`/`ethStaticNetmask`) — trước fix, 1 dấu `'` trong bất kỳ field nào cũng có thể phá cấu trúc `value='...'` của chính form đang render nó. **KHÔNG** áp dụng cho `mqttPass` nữa (F32 — field đó không còn render giá trị nào để escape, xem bên dưới).
- CSS `.field input[type=checkbox],.single input[type=checkbox]` (F36, 2026-08-03) — trước fix chỉ có `.field input[type=checkbox]`, nên checkbox nằm trong `div.single` (Enable MQTT, Enable OSC, Ưu tiên IP tĩnh...) bị `width:100%` của rule chung đè lên, dãn hết chiều rộng và đẩy nhãn ra xa bên phải.
- `handleRoot()` — GET `/`, trả về toàn bộ trang cấu hình (inline CSS + inline JS poll `/data`):
  - Panel **Sensor Configuration** — 1 block/sensor (`DEVICE_NUM` = 3): checkbox enable, MIN/MAX distance.
  - Panel **MQTT Settings** — enable, IP, port, user, password, topic, FULL/MISSING message. **(F32, 2026-08-03)** ô Password không còn `value=htmlEscape(mqttPass)` — chỉ còn `placeholder`, không render giá trị đã lưu (trang `/` không qua `requireAuth()` nên trước fix ai vào được LAN cũng đọc được password broker bằng View Source/curl); để trống khi Save = giữ nguyên (xem `web.cpp::handleSave`).
  - Panel **OSC Settings** — enable, IP, port, FULL/MISSING address + value.
  - Panel **Confirm Settings** — 2 ô debounce riêng (2026-08-03): `confirm` (FULL) và `confirm_miss` (MISSING, mặc định gấp đôi FULL).
  - Panel **Relay Reset (khi sensor OFFLINE)** (2026-08-03) — 4 checkbox `relay_p0..relay_p3` (nhãn "GPIO 4/5/6/7", tick được nhiều chân cùng lúc để gộp dòng cho relay cần dòng lớn), checkbox `relay_active_high` ("Kích mức HIGH", bỏ tick = LOW), ô số `relay_ms` (thời gian giữ xung, ms). Không tick chân nào = tắt tính năng.
  - Panel **Admin Auth** (mới, F6) — username hiển thị giá trị hiện tại (qua `htmlEscape()`), password luôn render rỗng (`value=''`) để không lộ pass hiện tại qua View Source; để trống 1 trong 2 field khi Save nghĩa là giữ nguyên giá trị cũ (xem `web.cpp::handleSave`).
  - **Tab "Mạng (Ethernet)"** — panel riêng (`#tab-network`, JS `showTab()` chuyển đổi 2 div `.tab-content` bằng class `active`, không reload trang) chứa checkbox **`eth_static_first`** ("Ưu tiên IP tĩnh", 2026-08-03, đọc `ethUseStaticFirst`) cộng `eth_ip`/`eth_gw`/`eth_mask` (F19 static-IP fallback / F34 DNS). Cùng 1 `<form action='/save'>` với tab "Cấu hình" — nút SAVE SETTINGS lưu cả 2 tab 1 lần, bất kể tab nào đang active.
  - Nút **SAVE SETTINGS** (POST `/save`), **Test MQTT** / **Test OSC** / **Test Relay** (POST `/test_mqtt` / `/test_osc` / `/test_relay`) — cả 4 route này giờ yêu cầu HTTP Basic Auth (F6, xem `web.cpp::requireAuth`), trình duyệt sẽ tự hỏi lại nếu chưa đăng nhập trong session.
  - Div `#d` (trạng thái realtime) được JS `fetch('/data')` cập nhật mỗi 100ms — route `/data` KHÔNG yêu cầu auth (chỉ đọc).

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
- **Bug 3** (audit toàn bộ Preferences: key name, buffer, strcpy...) — key name đã fix (Bug 1/F1/F29). Validate **giá trị** (numeric range cho `mqtt_port`/`osc_port`/`confirm`, `min<=max` cho sensor, `/` prefix cho OSC address) đã thêm ở `web.cpp::handleSave()` (F16/F15/4.9). Vẫn còn phần dùng `strncpy` cho các field text (`mqtt_ip`, `mqtt_topic`, `osc_ip`, `osc_address_*`, ...) mà **chưa validate độ dài trước khi copy** (mục 6 CLAUDE.md: "Dữ liệu từ Web form → validate độ dài **trước khi** copy vào buffer") — `strncpy` tự cắt bớt nên không tràn buffer, nhưng không cảnh báo user khi giá trị bị cắt. Chưa fix, out of scope cho các cluster đã làm.
- **HTML escaping** (spec doc §9 Web Server) — F7 đã fix: `htmlEscape()` trong `html.cpp` áp dụng cho toàn bộ giá trị config user nhập được render lại vào `value='...'`.
- **Auth trên endpoint thay đổi state** (không có trong spec doc gốc, phát hiện lúc audit — F6) — đã fix: `/save`, `/test_mqtt`, `/test_osc` giờ yêu cầu HTTP Basic Auth (`authUser`/`authPass`, mặc định `admin`/`admin`, đổi được qua panel Admin Auth).
- **Ethernet không có IP vĩnh viễn nếu không có DHCP server** (F19, biến thể mới của Bug 2 — Bug 2 gốc là block vô hạn, đã fix; nhưng "chờ có timeout rồi bỏ cuộc" cũng để thiết bị không có IP mãi mãi nếu mạng không có DHCP) — đã fix bằng static-IP fallback (`192.168.99.199`), xem `cantim_mqtt_new.cpp::setup()` ở trên. IP/gateway/netmask fallback này giờ sửa được qua panel Web UI (tab "Mạng (Ethernet)", `html.cpp`/`web.cpp`) thay vì chỉ qua NVS trực tiếp.
