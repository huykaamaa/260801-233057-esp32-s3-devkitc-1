# `upload_port = auto` / `monitor_port = auto` chặn flash (F35)

`platformio.ini` từng có:

```ini
upload_port = auto
monitor_port = auto
```

Ý định là "để PlatformIO tự dò cổng USB". Thực tế **esptool v5.1.0 hiểu chuỗi `"auto"` là
tên port thật**, không phải từ khóa đặc biệt, nên fail ngay:

```
A fatal error occurred: Could not open auto, the port is busy or doesn't exist.
```

**Cách đúng để "tự dò port":** không viết dòng `upload_port`/`monitor_port` nào cả (bỏ hẳn cả
2 dòng). PlatformIO chỉ tự động dò cổng khi các key này **không tồn tại** trong config, không
phải khi giá trị là chuỗi `"auto"`.

Lỗi này từng chặn thật 1 lần nạp firmware ở project `gia_sach` ngày 2026-08-03 (nguồn phát
hiện: audit gia_sach mang về can_tim, xem `docs/todo/audit-tu-gia-sach-2026-08-03.md` mục A4).
Fix ở can_tim: xóa cả 2 dòng, giữ nguyên `monitor_speed = 115200`.

**Đáng nhớ vì:** cái bẫy này rất dễ tái phát ở bất kỳ project ESP32/PlatformIO nào khác — code
mẫu/tutorial hay viết `upload_port = auto` với ý tưởng sai này. Kiểm tra `platformio.ini` của
project mới trước khi báo "không nạp được, port bận" là do phần cứng.
