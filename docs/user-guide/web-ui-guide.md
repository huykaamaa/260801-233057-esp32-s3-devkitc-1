# Hướng dẫn sử dụng — Web UI Phòng Cân Tim

> Dành cho người vận hành thiết bị (không cần biết code). Mô tả toàn bộ tính năng hiện có trên trang cấu hình web của thiết bị.

Cập nhật lần cuối: 2026-08-03 (ô MQTT Password không hiện lại giá trị cũ nữa; thêm tick "Ưu tiên IP tĩnh" trên tab Mạng).

---

## 1. Truy cập trang cấu hình

- Thiết bị dùng **Ethernet (dây LAN)**, không dùng WiFi.
- Cắm dây mạng vào cổng W5500 trước khi cấp nguồn.
- Lấy địa chỉ IP thiết bị từ router/DHCP server (hoặc theo dõi Serial log lúc boot nếu có kết nối USB debug — dòng `IP: ...`).
- **Nếu mạng KHÔNG có DHCP server** (hoặc thiết bị không nhận được IP trong ~10 giây đầu sau khi cắm dây): thiết bị sẽ tự chuyển sang dùng **địa chỉ IP tĩnh cố định `192.168.99.199`** (gateway `192.168.99.1`, subnet mask `255.255.255.0`) thay vì treo không có mạng vô thời hạn. Muốn truy cập bằng IP tĩnh này, máy tính của bạn phải nằm cùng dải mạng `192.168.99.x` (ví dụ đặt IP máy tính là `192.168.99.10`, gateway/mask giống trên) rồi mở `http://192.168.99.199/`. Đây là **IP cứng dự phòng**, khác với IP router cấp bình thường — chỉ xuất hiện khi mạng thật sự không có DHCP hoạt động.
- **Không có Serial/USB cũng tìm được IP:** trong **5 phút đầu sau khi có IP**, thiết bị tự phát 1 mạng WiFi chứa IP hiện tại, không cần mật khẩu — mở danh sách WiFi trên điện thoại/laptop, đọc tên mạng đó là biết IP ngay, không cần bắt được vào mạng đó. Tên mạng có 2 dạng, phân biệt luôn IP đó là thật hay fallback:
  - **`CANTIM-DHCP-<ip>`** (vd `CANTIM-DHCP-192.168.8.102`) — IP thật do router cấp qua DHCP.
  - **`CANTIM-STATIC-<ip>`** (vd `CANTIM-STATIC-192.168.99.199`) — đã rơi vào IP tĩnh fallback (mạng không có DHCP hoặc DHCP quá chậm, xem mục 5c) — máy tính phải đặt IP tĩnh cùng dải `192.168.99.x` mới truy cập được, không phải bắt được WiFi này là vào thẳng được.
  
  Sau 5 phút mạng WiFi này tự tắt để đỡ tốn điện/nhiễu sóng; muốn thấy lại thì cắm điện lại board (reset).
- Mở trình duyệt, truy cập `http://<ip-thiết-bị>/`.
- Trang **`/` (trang chủ) và ô trạng thái realtime luôn xem được, không cần đăng nhập.** Chỉ khi bấm **Save**, **Test MQTT**, hoặc **Test OSC**, trình duyệt mới hỏi username/password (xem mục 0 bên dưới).

Trang chủ hiển thị:
- **Ô trạng thái realtime** (đầu trang) — tự cập nhật mỗi 100ms: trạng thái kết nối MQTT, trạng thái bật/tắt OSC, trạng thái **FULL / MISSING / [!] NO SENSORS ENABLED** hiện tại (xem mục 2 để phân biệt), và khoảng cách đo được của từng sensor (hoặc `OFFLINE` nếu sensor không gửi dữ liệu quá 5 giây).
- **2 tab** ngay dưới ô trạng thái: **"Cấu hình"** (Sensor, MQTT, OSC, Confirm Time, Admin Auth — mục 2-5b) và **"Mạng (Ethernet)"** (mục 5c). Bấm tab để chuyển qua lại, không tải lại trang. Nút **SAVE SETTINGS** lưu **cả 2 tab cùng lúc** dù đang đứng ở tab nào (xem mục 6).

---

## 0. Đăng nhập (Basic Auth) — mặc định `admin` / `admin`

Từ bản cập nhật này, các thao tác **thay đổi cấu hình/trạng thái thiết bị** — bấm **SAVE SETTINGS**, **Test MQTT**, **Test OSC** — yêu cầu đăng nhập bằng cửa sổ popup của trình duyệt (HTTP Basic Auth), để tránh người lạ trên cùng mạng LAN đổi cấu hình hoặc kích test mà không được phép. Xem trang trạng thái (`/`) thì **không** cần đăng nhập.

- **Username / Password mặc định: `admin` / `admin`.** Thiết bị mới (hoặc chưa từng đổi) sẽ log rõ dòng cảnh báo này ra Serial mỗi lần khởi động, để không ai quên đổi.
- **Đổi mật khẩu:** cuộn xuống panel **Admin Auth** ở cuối form cấu hình, nhập username/password mới rồi bấm **SAVE SETTINGS**. Có thể để trống 1 trong 2 ô (username hoặc password) nếu chỉ muốn đổi ô còn lại — ô để trống giữ nguyên giá trị cũ, không bị xoá.
- Ô password trên form **luôn hiển thị rỗng** kể cả sau khi đã đặt mật khẩu (không hiện lại mật khẩu hiện tại vì lý do bảo mật) — đây là bình thường, không phải lỗi mất dữ liệu.
- **Đổi mật khẩu ngay khi lắp đặt thiết bị mới** nếu thiết bị nằm trên mạng có nhiều người truy cập được — mật khẩu mặc định `admin`/`admin` là công khai (ghi trong tài liệu này).

---

## 2. Sensor Configuration

Mỗi sensor (hiện có 3 sensor) có:

| Trường | Ý nghĩa |
|---|---|
| **Enable this sensor for publish** | Bật/tắt — chỉ sensor được bật mới tính vào điều kiện "đủ người" (FULL). |
| **MIN Distance (mm)** | Khoảng cách tối thiểu (mm) để coi là "có người" ở vị trí sensor này. |
| **MAX Distance (mm)** | Khoảng cách tối đa (mm) để coi là "có người" ở vị trí sensor này. |

**Cách hoạt động:** thiết bị chỉ báo **FULL** khi **tất cả** sensor đang bật (enable) đều đo được khoảng cách nằm trong khoảng `[MIN, MAX]` của chính nó. Chỉ cần 1 sensor (đang bật) ra ngoài khoảng, hoặc bị `OFFLINE` (mất tín hiệu quá 5 giây), trạng thái sẽ chuyển về **MISSING**.

Nếu chỉ dùng ít hơn 3 sensor thật, hãy **tắt (uncheck)** các sensor không dùng — sensor bị tắt sẽ không ảnh hưởng đến điều kiện FULL/MISSING dù không có dữ liệu.

Giá trị mặc định (khi chưa từng Save lần nào): MIN = 200mm, MAX = 800mm, cả 3 sensor đều bật.

**Lưu ý MIN/MAX:** khi Save, thiết bị kiểm tra MIN phải ≤ MAX cho từng sensor — nếu nhập MIN lớn hơn MAX, cặp giá trị đó **bị từ chối, không lưu** (báo lỗi trong thông báo sau khi Save), các trường khác vẫn lưu bình thường.

**Nếu tắt (uncheck) HẾT cả 3 sensor:** ô trạng thái sẽ hiện badge màu **cam "[!] NO SENSORS ENABLED"** thay vì badge đỏ MISSING thông thường. Đây là dấu hiệu **thiết bị chưa cấu hình sensor nào**, không phải "phòng đang trống" — kiểm tra lại panel Sensor Configuration nếu thấy badge này ngoài ý muốn.

---

## 3. MQTT Settings

| Trường | Ý nghĩa |
|---|---|
| **Enable MQTT** | Bật/tắt gửi dữ liệu qua MQTT. Tắt thì các trường bên dưới vẫn lưu nhưng không publish. |
| **MQTT IP** | Địa chỉ broker MQTT. |
| **MQTT Port** | Cổng broker (mặc định 1883). |
| **Username / Password** | Thông tin đăng nhập broker (bỏ trống nếu broker không yêu cầu). Ô **Password luôn hiện rỗng trên form** (không hiện lại mật khẩu đã lưu) — để trống khi Save nghĩa là giữ nguyên mật khẩu cũ, giống ô Password ở mục 5b. Muốn xóa hẳn mật khẩu: xóa trắng ô **Username** rồi Save — thiết bị chuyển sang kết nối anonymous (không gửi username lẫn password) dù mật khẩu cũ vẫn còn nằm trong bộ nhớ. |
| **MQTT Topic** | Topic sẽ publish trạng thái vào. |
| **FULL Message** | Nội dung payload gửi khi trạng thái chuyển sang FULL (mặc định `FULL`). |
| **MISSING Message** | Nội dung payload gửi khi trạng thái chuyển sang MISSING (mặc định `MISSING`). |

**Lưu ý:** đổi IP/Port/Username/Password và bấm **Save** sẽ khiến thiết bị **kết nối lại MQTT ngay lập tức** (mất kết nối cũ, tạo kết nối mới với thông tin vừa nhập). Đổi Topic/FULL Message/MISSING Message thì không cần kết nối lại, áp dụng ngay cho lần publish kế tiếp.

**MQTT Port** phải là số nguyên từ 1 đến 65535 — nhập sai định dạng hoặc ngoài khoảng này sẽ **bị từ chối, không lưu** (báo lỗi trong thông báo sau khi Save), port cũ vẫn giữ nguyên.

---

## 4. OSC Settings

OSC dùng để gửi tín hiệu tới phần mềm trình chiếu/ánh sáng (vd resolume, TouchDesigner...) qua UDP.

| Trường | Ý nghĩa |
|---|---|
| **Enable OSC output** | Bật/tắt gửi gói OSC. |
| **OSC IP** | Địa chỉ máy nhận OSC. |
| **OSC Port** | Cổng UDP nhận OSC (mặc định 9000). |
| **FULL Address** | OSC address gửi khi trạng thái là FULL. |
| **FULL Value (int)** | Giá trị số nguyên gửi kèm address FULL. |
| **MISSING Address** | OSC address gửi khi trạng thái là MISSING. |
| **MISSING Value (int)** | Giá trị số nguyên gửi kèm address MISSING. |

FULL và MISSING có thể dùng **cùng 1 address với giá trị khác nhau** (vd `1` = FULL, `0` = MISSING) hoặc **2 address khác nhau tùy nhu cầu** — cấu hình độc lập cho từng state.

**OSC Port** cùng quy tắc với MQTT Port ở trên: số nguyên 1-65535, sai thì bị từ chối không lưu.

**FULL Address / MISSING Address** phải **bắt đầu bằng dấu `/`** (chuẩn OSC, vd `/composition/layers/1/select`) — nhập thiếu dấu `/` đầu sẽ **bị từ chối, không lưu** (báo lỗi trong thông báo sau khi Save).

---

## 5. Confirm Settings

| Trường | Ý nghĩa |
|---|---|
| **Confirm Time - FULL (ms)** | Thời gian (mili-giây) trạng thái phải giữ ổn định liên tục trước khi thiết bị publish **FULL**. Mặc định 1000ms (1 giây). |
| **Confirm Time - MISSING (ms)** | Thời gian giữ ổn định riêng trước khi publish **MISSING** (2026-08-03, tách riêng khỏi FULL). Mặc định 2000ms (2 giây) — cố ý lâu hơn FULL để giảm khả năng bắn nhầm MISSING khi người chỉ tạm rời khỏi sensor trong chốc lát. |

Cả 2 giá trị nhập vào tự động giới hạn trong khoảng **50 - 60000ms** (nhập ngoài khoảng này sẽ tự kéo về giá trị gần nhất trong khoảng, không bị từ chối).

Dùng để chống nhiễu (debounce): nếu người đứng gần ranh giới ngưỡng khiến trạng thái nhấp nháy FULL/MISSING liên tục, tăng giá trị tương ứng để chờ ổn định lâu hơn rồi mới publish, tránh spam MQTT/OSC. Muốn thiết bị "nhạy" báo FULL nhưng "chậm rãi" báo MISSING (vd tránh mất cue khi người đứng lên tạm thời) — giữ FULL thấp, tăng MISSING lên.

---

## 5b. Admin Auth

| Trường | Ý nghĩa |
|---|---|
| **Username** | Tên đăng nhập cho popup Basic Auth khi bấm Save/Test (mặc định `admin`). |
| **Password** | Mật khẩu tương ứng (mặc định `admin`). Ô này luôn hiện rỗng trên form — để trống khi Save nghĩa là giữ nguyên mật khẩu cũ. |

Xem mục 0 ở đầu tài liệu để biết chi tiết cách đăng nhập và lý do panel này tồn tại.

---

## 5c. Mạng (Ethernet) — IP tĩnh dự phòng

Nằm ở tab **"Mạng (Ethernet)"** riêng (xem mục 1), không chung tab với các panel còn lại.

| Trường | Ý nghĩa |
|---|---|
| **Ưu tiên IP tĩnh (bỏ qua DHCP)** | Tick bật: thiết bị dùng IP tĩnh bên dưới **ngay từ đầu lúc boot**, bỏ qua hoàn toàn 10 giây chờ DHCP. Mặc định **tắt** (giữ hành vi cũ: thử DHCP trước, chỉ dùng IP tĩnh khi DHCP thất bại). |
| **Static IP** | Địa chỉ IP tĩnh. Mặc định `192.168.99.199`. |
| **Gateway** | Gateway tương ứng. Mặc định `192.168.99.1`. |
| **Netmask** | Subnet mask tương ứng. Mặc định `255.255.255.0`. |

**Mặc định (tick tắt): đây KHÔNG phải IP chính của thiết bị.** Thiết bị luôn thử xin IP qua DHCP trước (tối đa ~10 giây mỗi lần boot). 3 giá trị IP/Gateway/Netmask **chỉ được dùng khi DHCP thất bại** (mạng không có DHCP server, hoặc router chưa cấp IP kịp trong 10 giây đó) — lúc đó thiết bị tự gán 3 giá trị này làm IP của chính nó, để Web UI vẫn còn truy cập được thay vì mất mạng hoàn toàn (xem thêm mục 1, mục 8).

**Bật tick "Ưu tiên IP tĩnh":** thiết bị dùng thẳng 3 giá trị IP/Gateway/Netmask làm IP của nó ngay khi boot, không chờ DHCP — boot nhanh hơn, phù hợp khi mạng không có DHCP server hoặc cần thiết bị luôn có IP cố định chắc chắn. Nếu IP/Gateway/Netmask nhập sai định dạng thì thiết bị **tự động lùi về thử DHCP như bình thường** (không bị kẹt cứng).

- Mỗi ô IP/Gateway/Netmask phải là **địa chỉ IPv4 hợp lệ** (dạng `a.b.c.d`, ví dụ `192.168.99.199`) — nhập sai định dạng sẽ **bị từ chối, không lưu** (báo lỗi trong thông báo sau khi Save), giá trị cũ vẫn giữ nguyên.
- **Đổi giá trị ở đây không có tác dụng ngay lập tức.** Thiết bị chỉ đọc lại các giá trị này lúc **boot** (cắm điện lại). Nếu tick "Ưu tiên IP tĩnh" đang **tắt** và mạng vẫn có DHCP hoạt động bình thường, đổi IP tĩnh ở đây sẽ **không thấy thay đổi gì** trên thiết bị đang chạy — đó là bình thường, không phải lỗi.
- Muốn xác nhận IP tĩnh mới đã áp dụng (khi tick tắt): rút dây mạng (hoặc tắt DHCP server) rồi cắm lại điện thiết bị, đợi ~10 giây, sau đó truy cập bằng IP tĩnh vừa đặt. Khi tick **bật**, chỉ cần cắm lại điện là thấy ngay, không cần rút dây mạng.

---

## 6. Lưu cấu hình

Bấm **SAVE SETTINGS** ở cuối form để lưu **toàn bộ** các trường trên **cả 2 tab** (Sensor + MQTT + OSC + Confirm Time + Admin Auth + Mạng/Ethernet) vào bộ nhớ trong (Preferences/NVS) — giữ nguyên sau khi mất điện/reboot. Trình duyệt sẽ hỏi username/password (xem mục 0) trước khi thực hiện. Sau khi Save, trang sẽ hiện thông báo:
- **"Saved OK"** — mọi trường lưu thành công.
- **"Saved with N error(s) - check Serial log"** hoặc **"Save FAILED - NVS not accessible, check Serial log"** — có trường không lưu được, cần xem log Serial (kỹ thuật viên) để biết nguyên nhân.
- Kèm thêm ghi chú field cụ thể bị từ chối nếu có, ví dụ `(OSC address rejected: must start with /)`, `(MQTT port rejected: must be 1-65535)`, `(sensor min/max rejected: min must be <= max)`, `(Ethernet static IP/gateway/netmask rejected: must be a valid IPv4 address)`.

Sau khi hiện thông báo, trang tự tải lại.

⚠️ Save áp dụng cho **tất cả** trường cùng lúc, không lưu riêng từng nhóm. Field bị từ chối (không hợp lệ) sẽ **không** ghi đè giá trị cũ, các field hợp lệ khác trong cùng lần Save vẫn được lưu bình thường.

---

## 7. Test Settings

Cả 2 nút Test đều yêu cầu đăng nhập (xem mục 0).

- **Test MQTT (FULL)** — kích hoạt thủ công hành vi "FULL" (publish MQTT + gửi OSC như khi thật sự đủ người) để kiểm tra kết nối/cấu hình mà không cần đợi người đứng vào vị trí sensor.
- **Test OSC (FULL)** — hiện tại có cùng hành vi với nút Test MQTT ở trên (đều kích hoạt trạng thái FULL). Nếu cần test riêng trạng thái MISSING, phải đợi sensor thật báo MISSING hoặc yêu cầu bổ sung nút test riêng.

Sau khi bấm Test, ô trạng thái realtime sẽ phản ánh đúng là đã "publish FULL" (không còn bị lệch/kẹt trạng thái do bấm Test) — nếu ngay sau đó sensor thật báo MISSING, thiết bị sẽ publish MISSING bình thường ở lần chuyển kế tiếp, không bị kẹt chờ một sự kiện không liên quan mới chịu publish.

---

## 8. Xử lý sự cố nhanh

| Hiện tượng | Kiểm tra |
|---|---|
| Trang web không load | Đúng IP? Đã cắm dây mạng và đợi thiết bị lấy IP (tối đa ~10s sau boot) chưa? Nếu mạng không có DHCP, thử IP tĩnh dự phòng `192.168.99.199` (xem mục 1). Không biết chính xác IP → trong 5 phút đầu sau boot, mở WiFi trên điện thoại, tìm mạng `CANTIM-DHCP-<ip>` hoặc `CANTIM-STATIC-<ip>` để đọc IP thật + biết luôn là IP DHCP hay fallback (xem mục 1). |
| Trình duyệt hỏi username/password khi bấm Save/Test | Bình thường (F6, xem mục 0) — mặc định `admin`/`admin` nếu chưa từng đổi. Đăng nhập sai lặp lại → kiểm tra panel Admin Auth đã lưu đúng chưa, hoặc hỏi người đã đổi mật khẩu gần nhất. |
| Sensor báo `OFFLINE` | Kiểm tra dây RS485 tới sensor đó, hoặc sensor đã bị tắt trong cấu hình nhưng vẫn hiển thị offline (bình thường nếu tắt). |
| Trạng thái không đổi dù có người | Kiểm tra sensor đó có đang **bật (enable)** không, và khoảng cách đo được (ô trạng thái realtime) có nằm trong MIN/MAX đã cấu hình không. |
| Thấy badge cam **"[!] NO SENSORS ENABLED"** | Không phải lỗi mạng/sensor — nghĩa là **tất cả** sensor đang tắt (uncheck) trong panel Sensor Configuration. Bật lại ít nhất 1 sensor. |
| MQTT hiện `DISCONNECTED` | Kiểm tra IP/Port/Username/Password broker, và broker có đang chạy/cho phép kết nối từ thiết bị không. |
| Save báo lỗi ("rejected"/"error(s)") thay vì "Saved OK" | Đọc kỹ ghi chú field bị từ chối trong thông báo (OSC address thiếu `/`, port ngoài khoảng 1-65535, MIN > MAX, IP/Gateway/Netmask sai định dạng) — sửa đúng field đó rồi Save lại; các field khác đã lưu vẫn giữ nguyên. |
| Đổi IP tĩnh dự phòng (tab Mạng/Ethernet) xong nhưng không thấy gì thay đổi | Bình thường nếu mạng vẫn có DHCP hoạt động — 3 giá trị này chỉ áp dụng khi DHCP thất bại lúc boot, xem mục 5c. |
| Đổi 1 field rồi Save, field khác bị mất giá trị | Không nên xảy ra (Save ghi toàn bộ form) — nếu gặp, báo lại kèm field cụ thể để kiểm tra code. |
