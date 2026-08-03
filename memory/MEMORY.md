# Memory Index — Phòng Cân Tim

Index duy nhất — mỗi entry 1 dòng, link tới file trong cùng thư mục `memory/`.

## Active bugs / fixes

- 🩹 [NVS key length limit + namespace conflation (2026-08-02)](nvs-key-length-conflation-2026-08-02.md) — Bug 1/F1/F29/F2/F4: 3 key NVS vượt 15 ký tự (`osc_address_full/missing`, `osc_value_missing`) khiến `putString/getString` âm thầm fail; root cause sâu hơn là dùng chung literal cho tên field HTML và key NVS (2 tầng khác giới hạn độ dài).
- 🩹 [OSC wire-format bugs: byte order + string padding (2026-08-02)](osc-wire-format-bugs-2026-08-02.md) — F9/F10: int32 value ghi little-endian thay vì big-endian (chuẩn OSC 1.0), và công thức padding string `(4-(len%4))%4` mất NUL terminator khi độ dài address chia hết 4. Cả 2 bug **im lặng với default hiện tại** (giá trị/độ dài default tình cờ né được case lỗi) — chỉ lộ khi operator tự đổi value/address, nên audit code đối chiếu spec giao thức (không chỉ test bằng mắt) là cách duy nhất bắt được.
- 🔀 [esptool hiểu `upload_port = auto` là tên port thật, không phải auto-detect (2026-08-03)](esptool-upload-port-auto-2026-08-03.md) — F35: xóa hẳn dòng `upload_port`/`monitor_port` thay vì gán `"auto"` mới thực sự để PlatformIO tự dò cổng. Bẫy dễ tái phát ở project ESP32 khác.
- 🩹 [MQTT password không round-trip qua form nữa (2026-08-03)](mqtt-password-no-echo-2026-08-03.md) — F32: trang `/` không cần đăng nhập từng lộ password broker qua View Source; fix xong thì field Password thành write-only, escape hatch để "xóa" là xóa trắng Username → chuyển anonymous.
