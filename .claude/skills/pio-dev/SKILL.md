---
name: pio-dev
description: Build/upload firmware, xem serial monitor, hoặc chạy native unit test nhanh cho project Phòng Cân Tim (ESP32-S3, PlatformIO, 1 board/1 env — không cần wrapper claim như FI-AUDIO-DMX-003). Dùng khi cần build hoặc flash firmware lên board, khi cần đọc log Serial (RS485/MQTT/Ethernet), khi vừa sửa logic thuần (parsing/threshold trong src/sensor_logic.cpp) và cần test nhanh trước khi build lên board, hoặc khi phân vân "có cần build lại không". KHÔNG dùng cho Q&A thuần hoặc đọc code không liên quan tới build/flash/test.
---

# PlatformIO Dev Workflow — Phòng Cân Tim (ESP32-S3)

Project này chỉ có **1 board, 1 env** (`esp32-s3-devkitc-1`) — không cần wrapper/board-claim
như DMX003. Gõ lệnh `pio` trực tiếp là an toàn.

`pio` không có sẵn trên PATH mặc định trong shell của agent — nếu `pio --version` báo
"not found", thêm PlatformIO penv vào PATH cho phiên bash hiện tại trước:

```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"
```

(PowerShell: `$env:Path = "$env:USERPROFILE\.platformio\penv\Scripts;$env:Path"`)

⚠️ **LUÔN set encoding UTF-8 trước MỌI lệnh `pio` có upload/monitor (không chỉ build suông)** —
console Windows mặc định dùng codepage `cp1252`, còn esptool/PlatformIO in progress bar bằng
ký tự Unicode (`█░`). Không set thì:
- `pio run` (build suông) chỉ mất bảng RAM/Flash đẹp, in dòng "Firmware metrics can not be
  shown" — vô hại, bỏ qua được.
- `pio run -t upload` / `pio device monitor` thì **crash thật giữa chừng khi đang ghi flash**:
  thread đọc output của PlatformIO chết vì `UnicodeEncodeError` lúc gặp ký tự progress bar,
  pipe bị nghẽn, tiến trình con esptool treo chờ (không thoát), rồi bị agent/timeout kill —
  board có thể đang ở trạng thái flash dở dang lúc đó. Đã xảy ra thật ngày 2026-08-03, xem
  `memory/esptool-utf8-console-crash-2026-08-03.md`.

Set 1 lần đầu session (áp dụng cho mọi lệnh `pio` sau đó trong cùng bash session):

```bash
export PYTHONIOENCODING=utf-8
export PYTHONUTF8=1
```

(PowerShell: `$env:PYTHONIOENCODING = "utf-8"; $env:PYTHONUTF8 = "1"` — hoặc `chcp 65001` đổi
codepage cả console.)

## 0. Trước khi build — build ÍT nhất có thể

- Sửa `docs/`, `CLAUDE.md`, comment-only → **không build**.
- Sửa `.cpp`/`.h` trong `src/` hoặc `include/`, hoặc `platformio.ini` → build lại env
  `esp32-s3-devkitc-1` (Tier "full") **một lần** sau khi sửa xong, không build sau mỗi edit nhỏ.
- Sửa logic thuần trong `src/sensor_logic.cpp` / `include/sensor_logic.h` (parsing RS485,
  ngưỡng khoảng cách) → chạy **native test trước** (§3, vài giây, rẻ hơn build ESP32 ~50s
  rất nhiều), rồi mới build ESP32 để xác nhận link/flash OK.

## 1. Build & Upload

```bash
pio run -e esp32-s3-devkitc-1
```

Build PASS in ra `RAM: xx.x%` / `Flash: xx.x%` — luôn đọc 2 số này (không cần báo lại cho
user trừ khi thay đổi đáng kể so với baseline ~14%/~35%).

Build lỗi → tìm dòng `error:`, báo tối đa 5 lỗi đầu kèm `file:line:col`, đừng dán nguyên log.

Upload lên board (USB CDC, `platformio.ini` không khai báo `upload_port`/`monitor_port` nên
PlatformIO tự dò cổng — xem `memory/esptool-upload-port-auto-2026-08-03.md` về lý do KHÔNG
gán `upload_port = auto` bằng tay, chuỗi đó bị hiểu là tên port thật chứ không phải auto-detect):

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

Nếu auto-detect fail, tìm COM port rồi truyền `--upload-port`:

```bash
pio device list
pio run -e esp32-s3-devkitc-1 -t upload --upload-port COM3
```

Upload là hành động ghi vào flash thật của board đang cắm — nếu không chắc board nào đang
cắm hoặc user chưa xác nhận muốn flash ngay, hỏi trước khi chạy `-t upload`.

Wrapper sẵn có (tương đương, thêm confirm prompt cho case phá hoại):

```bash
./tools/upload.sh              # = pio run -t upload, giữ nguyên NVS (dùng hàng ngày)
./tools/full_erase.sh          # erase toàn bộ NVS (MQTT/OSC/threshold) rồi upload lại — có prompt xác nhận
```

**Nhiều board vật lý cùng lúc / nhiều session:** claim COM port trước khi flash để tránh
2 session cùng ghi 1 board — xem [tools/board_claim.ps1](../../../tools/board_claim.ps1) và
skill [parallel-session-coordination](../parallel-session-coordination/SKILL.md).

```powershell
./tools/board_claim.ps1 claim COM6 -Note "flash test"
./tools/board_claim.ps1 release COM6   # nhớ nhả khi xong chuỗi việc, đừng giữ khi idle
```

## 2. Serial Monitor

`pio device monitor` chạy **vô hạn**, sẽ treo agent nếu chạy foreground và chờ output —
LUÔN chạy nền (`run_in_background: true`) rồi đọc log bằng BashOutput/Monitor, không đợi
lệnh tự kết thúc.

```bash
pio device monitor -e esp32-s3-devkitc-1 --baud 115200
```

Muốn build+upload+monitor liền một lệnh:

```bash
pio run -e esp32-s3-devkitc-1 -t upload -t monitor
```

Dừng monitor: kill tiến trình nền (đừng để nó treo phiên làm việc). Log quan trọng luôn bật
(MQTT connect fail, Ethernet down, sensor timeout — theo `CLAUDE.md` §6); nếu không thấy log
mong đợi, kiểm tra `monitor_speed` khớp `Serial.begin(115200)` trong
[cantim_mqtt_new.cpp](../../../src/cantim_mqtt_new.cpp) trước khi nghi ngờ code.

Thay thế: [tools/serial_capture.py](../../../tools/serial_capture.py) — mở thẳng COM port
bằng pyserial (không đụng `pio device monitor`), ghi log kèm timestamp ra file + in ra
màn hình các dòng khớp tag (`ETH`, `MQTT`, `FULL`, `MISSING`, `Loi:`, `khong hop le`,
`overflow`...). Dùng khi cần lưu log lại để đối chiếu, hoặc khi `pio device monitor` đang
bị process khác chiếm cổng:

```bash
~/.platformio/penv/Scripts/python.exe -m pip install pyserial   # 1 lần
python tools/serial_capture.py --duration 30                     # auto-thoát sau 30s
```

## 3. Native test nhanh

Dùng cho logic thuần, không đụng Arduino/ETH/WiFi/WebServer/Preferences — hiện có
`include/sensor_logic.h` + `src/sensor_logic.cpp` (parse dòng RS485 "id,distance", check
ngưỡng khoảng cách, validate device id), test tại
[test/test_sensor_logic/test_sensor_logic.cpp](../../../test/test_sensor_logic/test_sensor_logic.cpp).

```bash
pio test -e native
```

- Chạy trên PC (Unity framework), không cần board cắm, vài giây là xong.
- `env:native` trong `platformio.ini` dùng `build_src_filter` để loại các file kéo theo
  Arduino (`cantim_mqtt_new.cpp`, `html.cpp`, `mqtt.cpp`, `web.cpp`) — nếu thêm module logic
  thuần mới cần test native, viết nó tương tự `sensor_logic.*` (không `#include <Arduino.h>`,
  không dùng `String`) rồi thêm test tương ứng trong `test/`.
- **Yêu cầu môi trường:** cần host GCC/G++ (MinGW) trên PATH. Nếu `pio test -e native` báo
  `'gcc' is not recognized` / `'g++' is not recognized` → chưa cài toolchain, không phải lỗi
  code. Cài một lần:
  ```bash
  winget install --id BrechtSanders.WinLibs.POSIX.UCRT --source winget
  ```
  rồi mở terminal mới để PATH nhận thay đổi, kiểm tra `gcc --version`.
- Build PASS ESP32 chỉ chứng minh cú pháp/link, KHÔNG chứng minh logic đúng — với thay đổi
  parsing/threshold, chạy native test trước khi coi task xong.

## 4. Definition of Done

Theo `CLAUDE.md` §Definition of Done: báo "xong" chỉ sau khi `pio run -e esp32-s3-devkitc-1`
PASS; nếu đổi logic trong `sensor_logic.*`, `pio test -e native` cũng phải PASS. Nếu fix bug
đã liệt kê trong `docs/user-take-note/project-spec-v1.md` §8 → cập nhật trạng thái bug đó.
