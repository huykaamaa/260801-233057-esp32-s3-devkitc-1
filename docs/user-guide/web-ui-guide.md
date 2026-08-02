# Hướng dẫn sử dụng — Web UI Phòng Cân Tim

> Dành cho người vận hành thiết bị (không cần biết code). Mô tả toàn bộ tính năng hiện có trên trang cấu hình web của thiết bị.

Cập nhật lần cuối: 2026-08-02.

---

## 1. Truy cập trang cấu hình

- Thiết bị dùng **Ethernet (dây LAN)**, không dùng WiFi.
- Cắm dây mạng vào cổng W5500 trước khi cấp nguồn (nếu chưa cắm, thiết bị vẫn khởi động sau ~10 giây chờ, nhưng sẽ không có địa chỉ IP nên không truy cập được web cho tới khi cắm dây và thiết bị lấy được IP).
- Lấy địa chỉ IP thiết bị từ router/DHCP server (hoặc theo dõi Serial log lúc boot nếu có kết nối USB debug — dòng `IP: ...`).
- Mở trình duyệt, truy cập `http://<ip-thiết-bị>/`.

Trang chủ hiển thị:
- **Ô trạng thái realtime** (đầu trang) — tự cập nhật mỗi 100ms: trạng thái kết nối MQTT, trạng thái bật/tắt OSC, trạng thái **FULL / MISSING** hiện tại, và khoảng cách đo được của từng sensor (hoặc `OFFLINE` nếu sensor không gửi dữ liệu quá 5 giây).
- **Form cấu hình** bên dưới, chia theo 4 nhóm: Sensor, MQTT, OSC, Confirm Time.

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

---

## 3. MQTT Settings

| Trường | Ý nghĩa |
|---|---|
| **Enable MQTT** | Bật/tắt gửi dữ liệu qua MQTT. Tắt thì các trường bên dưới vẫn lưu nhưng không publish. |
| **MQTT IP** | Địa chỉ broker MQTT. |
| **MQTT Port** | Cổng broker (mặc định 1883). |
| **Username / Password** | Thông tin đăng nhập broker (bỏ trống nếu broker không yêu cầu). |
| **MQTT Topic** | Topic sẽ publish trạng thái vào. |
| **FULL Message** | Nội dung payload gửi khi trạng thái chuyển sang FULL (mặc định `FULL`). |
| **MISSING Message** | Nội dung payload gửi khi trạng thái chuyển sang MISSING (mặc định `MISSING`). |

**Lưu ý:** đổi IP/Port/Username/Password và bấm **Save** sẽ khiến thiết bị **kết nối lại MQTT ngay lập tức** (mất kết nối cũ, tạo kết nối mới với thông tin vừa nhập). Đổi Topic/FULL Message/MISSING Message thì không cần kết nối lại, áp dụng ngay cho lần publish kế tiếp.

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

---

## 5. Confirm Settings

| Trường | Ý nghĩa |
|---|---|
| **Confirm Time (ms)** | Thời gian (mili-giây) trạng thái phải giữ ổn định liên tục trước khi thiết bị mới publish MQTT/OSC. Mặc định 1000ms (1 giây). |

Dùng để chống nhiễu (debounce): nếu người đứng gần ranh giới ngưỡng khiến trạng thái nhấp nháy FULL/MISSING liên tục, tăng giá trị này để chờ ổn định lâu hơn rồi mới publish, tránh spam MQTT/OSC.

---

## 6. Lưu cấu hình

Bấm **SAVE SETTINGS** ở cuối form để lưu **toàn bộ** các trường trên (Sensor + MQTT + OSC + Confirm Time) vào bộ nhớ trong (Preferences/NVS) — giữ nguyên sau khi mất điện/reboot. Sau khi Save, trang sẽ hiện thông báo "Saved OK" rồi tự tải lại.

⚠️ Save áp dụng cho **tất cả** trường cùng lúc, không lưu riêng từng nhóm.

---

## 7. Test Settings

- **Test MQTT (FULL)** — kích hoạt thủ công hành vi "FULL" (publish MQTT + gửi OSC như khi thật sự đủ người) để kiểm tra kết nối/cấu hình mà không cần đợi người đứng vào vị trí sensor.
- **Test OSC (FULL)** — hiện tại có cùng hành vi với nút Test MQTT ở trên (đều kích hoạt trạng thái FULL). Nếu cần test riêng trạng thái MISSING, phải đợi sensor thật báo MISSING hoặc yêu cầu bổ sung nút test riêng.

---

## 8. Xử lý sự cố nhanh

| Hiện tượng | Kiểm tra |
|---|---|
| Trang web không load | Đúng IP? Đã cắm dây mạng và đợi thiết bị lấy IP (tối đa ~10s sau boot) chưa? |
| Sensor báo `OFFLINE` | Kiểm tra dây RS485 tới sensor đó, hoặc sensor đã bị tắt trong cấu hình nhưng vẫn hiển thị offline (bình thường nếu tắt). |
| Trạng thái không đổi dù có người | Kiểm tra sensor đó có đang **bật (enable)** không, và khoảng cách đo được (ô trạng thái realtime) có nằm trong MIN/MAX đã cấu hình không. |
| MQTT hiện `DISCONNECTED` | Kiểm tra IP/Port/Username/Password broker, và broker có đang chạy/cho phép kết nối từ thiết bị không. |
| Đổi 1 field rồi Save, field khác bị mất giá trị | Không nên xảy ra (Save ghi toàn bộ form) — nếu gặp, báo lại kèm field cụ thể để kiểm tra code. |
