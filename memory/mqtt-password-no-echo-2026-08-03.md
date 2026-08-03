# MQTT password không round-trip qua form nữa — mất khả năng "xóa trắng" (F32)

## Root cause

Trang `/` (`handleRoot()` trong `html.cpp`) không qua `requireAuth()` — cố ý, để dashboard
tự-refresh (`fetch('/data')` mỗi 100ms) hoạt động không cần đăng nhập lại. Trước fix, ô
Password của panel MQTT Settings render `value='<mqttPass đã escape>'`, nghĩa là **bất kỳ ai
vào được LAN cũng đọc được password broker MQTT bằng View Source hoặc `curl http://<ip>/`**,
không cần đăng nhập Basic Auth. `type='password'` chỉ che trên màn hình trình duyệt, không che
trong HTML source.

Phát hiện qua audit project `gia_sach` (code mạng tách từ can_tim, cùng lỗi — xem
`docs/todo/audit-tu-gia-sach-2026-08-03.md` mục A1).

## Fix và hệ quả phát sinh

`html.cpp`: bỏ hẳn `value=`, chỉ còn `placeholder`. `web.cpp::handleSave()`: submit rỗng =
giữ nguyên (cùng pattern đã có sẵn cho `auth_pass`).

**Hệ quả:** một khi ô Password không hiển thị giá trị thật nữa, **không còn cách nào submit
"chuỗi rỗng" một cách cố ý** để xóa trắng password đã lưu — submit rỗng luôn bị hiểu là "để
trống = giữ nguyên", không phải "user cố tình muốn xóa".

## Quyết định kiến trúc: escape hatch qua Username

`mqtt.cpp::mqttInit()` đổi điều kiện gửi password từ độc lập:
```cpp
if (strlen(mqttUser) > 0) config.credentials.username = mqttUser;
if (strlen(mqttPass) > 0) config.credentials.authentication.password = mqttPass;
```
thành lồng nhau — password chỉ gửi khi username khác rỗng:
```cpp
if (strlen(mqttUser) > 0) {
  config.credentials.username = mqttUser;
  if (strlen(mqttPass) > 0) config.credentials.authentication.password = mqttPass;
}
```

Kết quả: **xóa trắng ô Username rồi Save = chuyển hẳn sang kết nối anonymous** (không gửi
username lẫn password lên broker), dù `mqttPass` vẫn còn nằm nguyên trong NVS. Đây là đường
thoát duy nhất còn lại để "vô hiệu hóa" 1 password đã lưu mà không cần đọc lại giá trị cũ.

**Đáng nhớ vì:** đây là pattern chung cho MỌI field "write-only" kiểu password trên Web UI
project này (đã áp dụng y hệt cho `auth_pass` trước đó, F6) — hễ 1 field không round-trip giá
trị được nữa, phải chủ động thiết kế 1 đường thoát khác để user "reset" nó, không thì field
đó coi như chỉ ghi được, không xóa được. Xem thêm
[nvs-key-length-conflation-2026-08-02](nvs-key-length-conflation-2026-08-02.md) cho 1 dạng bug
khác cũng liên quan tới field NVS write-only-nhưng-âm-thầm.
