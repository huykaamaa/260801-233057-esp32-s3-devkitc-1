# Fix mạng / web / bảo mật mang về từ audit gia_sach

Ghi ngày 2026-08-03. Nguồn: audit project `gia_sach` (commit `1451d3e`, `babee5e`).

Code mạng của `gia_sach` tách ra từ project này, nên phần lớn lỗi là chung. Danh sách dưới đây
**đã đối chiếu thực tế với source của can_tim** tại commit `445f7c2` — không phải chép nguyên
list của gia_sach sang. Số dòng là của commit đó, kiểm lại nếu file đã đổi.

Các mục **chưa được gán mã F** — cố ý không tự đặt số để khỏi đụng hệ thống đánh số hiện có
(F6, F19, F25, F31...). Gán khi đưa vào lịch làm.

---

## A. CẦN SỬA — đã xác nhận còn lỗi

### A1. Lộ mật khẩu MQTT qua trang `/` không cần đăng nhập  🔴

`src/html.cpp:156-157`

```cpp
html += "<input type='password' name='mqtt_pass' value='";
html += htmlEscape(mqttPass);
```

`src/cantim_mqtt_new.cpp:245` đăng ký `server.on("/", HTTP_GET, handleRoot)` **không qua
`requireAuth()`** (cố ý, để dashboard tự refresh được). Nên bất kỳ ai vào được LAN đều đọc
password broker bằng View Source hoặc `curl http://<ip>/`. `type='password'` chỉ che trên màn
hình, không che trong HTML.

**Sửa:** bỏ `value=`, thay bằng `placeholder='(giữ nguyên nếu để trống)'`, rồi ở `handleSave`
chỉ ghi đè khi arg khác rỗng. Project này đã làm đúng pattern đó cho `auth_pass`
(`web.cpp:244`) — copy y hệt.

Lưu ý phát sinh: khi Pass không đọc lại được thì **không xóa được password** qua ô đó nữa.
Cách xử lý ở `gia_sach`: cho `mqtt_user` xóa trắng được, và trong `mqttInit()` chỉ gửi
password khi username khác rỗng → xóa User = chuyển hẳn sang anonymous.

### A2. `mqtt_ip` rỗng làm chết MQTT  🟡

`src/web.cpp:117-118` — `strncpy` thẳng, không guard rỗng.

Lưu chuỗi rỗng → URI thành `mqtt://:1883` → `esp_mqtt_client_init()` fail → MQTT chết hẳn cho
tới khi sửa lại qua web.

**Sửa:** guard `.length() > 0` (để trống = giữ nguyên). Ở `gia_sach` gom thành helper
`saveStringArg()` dùng lại cho nhiều trường.

### A3. `ETH.config()` không set DNS  🟡

`src/cantim_mqtt_new.cpp:229`

```cpp
if (ETH.config(fallbackIp, fallbackGw, fallbackMask)) {
```

Gọi 3 tham số → DNS để trống. `mqttServer` được nhét vào `mqtt://%s:%u` nên **cho phép nhập
hostname**, nhưng ở nhánh static fallback thì hostname không resolve được.

**Sửa:** truyền gateway làm DNS1 — `ETH.config(ip, gw, mask, gw)`.

### A4. `upload_port = auto` trong platformio.ini  🟡

`platformio.ini:19`

PlatformIO tự dò port khi **bỏ hẳn** dòng đó. Đặt chuỗi `"auto"` thì esptool v5.1.0 hiểu là
tên port thật và fail ngay:

```
A fatal error occurred: Could not open auto, the port is busy or doesn't exist.
```

Đây là lỗi thật đã chặn một lần nạp ở `gia_sach` ngày 2026-08-03.

**Sửa:** xóa cả `upload_port` lẫn `monitor_port`. Giữ `monitor_speed`.

### A5. Ô tick bị kéo dãn full chiều rộng  🟢

`src/html.cpp:49-50`

```cpp
html += ".field input,.single input{width:100%;padding:10px;...}";
html += ".field input[type=checkbox]{width:auto;...}";   // thiếu .single
```

Dòng trên set `width:100%` cho mọi input kể cả checkbox, dòng override chỉ nhắm `.field`.
Checkbox nào nằm trong `div.single` bị dãn hết chiều rộng, đẩy chữ nhãn ra xa bên phải.

**Sửa:** `.field input[type=checkbox],.single input[type=checkbox]{...}`

---

## B. KHÔNG cần làm — project này đã có sẵn

Đã đọc logic để xác nhận, không chỉ đọc comment. Đừng "sửa" lại mấy chỗ này.

| Mục | Bằng chứng |
|---|---|
| Không hardcode password trong source | `cantim_mqtt_new.cpp:28` — `mqttPass[32] = ""` |
| Validate IP/gateway/netmask trước khi lưu | `web.cpp:253-284` — `fromString()` cho **cả ba** trường, cờ `ethAddressInvalid` |
| Báo lỗi IP lên UI | `web.cpp:330-332` — nối vào `alertMsg` |
| Basic Auth trên endpoint đổi trạng thái | `web.cpp:12` `requireAuth()`, gọi ở 91, 358, 374 |
| `mqttConnected = false` sau `destroy()` | `web.cpp:298` — comment "4.8" |

Phần validate IP ở đây còn đầy đủ hơn bản `gia_sach` trước khi sửa. Khác biệt duy nhất:
can_tim coi chuỗi rỗng là **lỗi** (`fromString("")` trả false), `gia_sach` coi rỗng là "giữ
nguyên". Form luôn post đủ trường nên thực tế không khác nhau.

---

## C. KHÔNG áp dụng — chỉ có ở gia_sach

- **Relay test pulse kẹt ON sau 24.9 ngày** — phần sensor/relay của gia_sach lấy từ `dat_the`,
  project này không có. **Nhưng** nếu chỗ nào ở đây dùng sentinel `0` cùng phép trừ chống tràn
  `(X - millis()) > 0` thì dính đúng kiểu lỗi đó — đáng grep một lượt. Cơ chế: khi
  `millis() > 2^31` (~24.9 ngày uptime), `(0 - millis())` ép về `long` ra số **dương**, nên
  điều kiện "đang trong thời gian chờ" bật lại vĩnh viễn suốt 24.9 ngày kế tiếp.
- **Heartbeat gửi lại trạng thái định kỳ** — project này không có tính năng đó.
- **Resync sau chuỗi Test** — cấu trúc test khác, chưa kiểm.
- **`/data` hiện giá trị thô thay vì đã debounce** — bài toán distance, khác hẳn.

---

## D. Chưa kiểm — xem khi bắt tay vào

- Bật/tắt "Enable MQTT" có ngắt hẳn client không hay chỉ chặn publish. `mqtt.cpp:88,113` có
  guard `mqttEnabled` nhưng chưa rõ có restart client. Ở `gia_sach` đã đổi thành ngắt hẳn, vì
  trước đó dashboard vẫn báo CONNECTED sau khi bỏ tick.
- `oscUdp.begin(9000)` hardcode (`cantim_mqtt_new.cpp:251`) — cosmetic, local port khác với
  `oscPort` đích, dễ đọc nhầm.
- CSRF trên `/save`: Basic Auth được trình duyệt tự gắn nên trang bất kỳ có thể POST khi admin
  đang mở tab. Rủi ro thấp với thiết bị LAN — ở `gia_sach` đã cân nhắc và cố ý bỏ qua.
