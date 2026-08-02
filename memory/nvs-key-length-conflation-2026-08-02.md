# NVS key length limit + namespace conflation (Bug 1 / F1, F29, F2, F4) — 2026-08-02

## Hiện tượng gốc
OSC Full Value sống sót qua reboot, OSC Full Address thì mất — dù cả hai đi qua cùng
`saveDistanceConfig()`/`setup()`, cùng pattern code.

## Root cause thật (không phải "sai key" kiểu typo)
ESP32 NVS giới hạn tên key ở `NVS_KEY_NAME_MAX_SIZE = 16` byte **kể cả NUL** => 15 ký tự
dùng được. Code cũ dùng thẳng:

- `"osc_address_full"` — 16 ký tự → **vượt giới hạn**
- `"osc_address_missing"` — 19 ký tự → **vượt giới hạn**
- `"osc_value_missing"` — 17 ký tự → **vượt giới hạn**
- `"osc_value_full"` — 14 ký tự → vừa đủ, sống sót

`Preferences::putString()`/`getString()` KHÔNG throw, KHÔNG log lỗi khi key quá dài —
`nvs_set_str()` trả `ESP_ERR_NVS_KEY_TOO_LONG` nội bộ, `Preferences` bọc nó thành `putString()`
trả `0` (giống hệt "không có gì để ghi") và `getString()` âm thầm trả về `defaultValue`.
Nhìn từ bên ngoài, key quá dài và "field chưa từng được lưu" là **không thể phân biệt** —
đúng lý do bug này tồn tại lâu mà không ai nghi ngờ đúng chỗ.

## Root cause kiến trúc sâu hơn (F29)
String literal `"osc_address_full"` từng dùng CHUNG cho cả:
1. Tên field HTML form (`<input name='osc_address_full'>` trong `html.cpp`)
2. Tên arg khi đọc form (`server.hasArg("osc_address_full")` trong `web.cpp`)
3. Tên KEY khi ghi/đọc NVS (`prefs.putString("osc_address_full", ...)`)

HTTP arg name không có giới hạn độ dài — NVS key thì có. Việc dùng chung 1 literal cho cả
2 tầng (UI layer và storage layer) nghĩa là đổi tên field "cho rõ nghĩa hơn" ở tầng UI vô
tình phá tầng storage mà không ai nhận ra ngay — 2 tầng có ràng buộc khác nhau nhưng bị
coi là 1.

## Bằng chứng thực địa
`nvs_backup.bin` lấy từ board thật của operator lúc audit chứa **3+ generation key mồ côi**
trong namespace `"distance"` (từ các lần đổi tên key trước đó không có cơ chế dọn), cộng
thêm 1 generation nữa trong namespace `"player"` (namespace đó KHÔNG còn được code hiện tại
dùng tới — code hiện tại chỉ `prefs.begin("distance", ...)`). Firmware trước fix này KHÔNG
có cách nào tự phát hiện hay dọn các generation mồ côi đó.

## Fix đã áp dụng
1. Đổi 3 key vượt giới hạn thành `osc_addr_full` (13), `osc_addr_miss` (13),
   `osc_value_miss` (14) — TÁCH biệt khỏi tên field HTML/hasArg (tên field HTML giữ
   nguyên `osc_address_full`/`osc_address_missing`/`osc_value_missing`, không đổi).
2. Macro `NVS_KEY(s)` (template + `static_assert`, xem `src/globals.h`) bọc quanh MỌI
   literal key NVS hiện có — key >15 ký tự giờ fail ở **build time**, không còn âm thầm
   mất data ở field.
3. `cfg_ver` (kiểu `uint32_t`, key NVS "cfg_ver" = 7 ký tự) + `CFG_VERSION` macro trong
   `globals.h` — boot log cảnh báo nếu `cfg_ver` cũ/thiếu, **không tự xoá NVS** (board chạy
   không người trông, xoá nhầm config broker/OSC của operator còn tệ hơn không làm gì).
   Muốn xoá tay: `tools/full_erase.sh` (đã có sẵn từ trước, erase toàn bộ flash — không
   web-expose, không thêm nút factory-reset trên Web UI trong cluster này).
4. `prefs.begin()` (cả load lẫn save) giờ kiểm tra return value — load fail thì giữ default
   trong RAM (không load), save fail thì return `-1` sớm, không ghi gì.
5. `saveDistanceConfig()` đổi từ `void` sang `int`, đếm số `put*()` trả 0-byte-written coi
   là lỗi (trừ `putString()` trên field cho phép rỗng thật sự như `mqtt_user`/`mqtt_pass` —
   `putString("")` cũng trả 0 khi THÀNH CÔNG ghi chuỗi rỗng, nên chỉ tính là lỗi khi giá trị
   nguồn không rỗng).
6. `web.cpp::handleSave()` dùng số lỗi đó để trả lời trung thực — "Saved OK" chỉ khi 0 lỗi,
   ngược lại báo số lỗi hoặc "Save FAILED", thay vì luôn luôn báo "Saved OK".

## Ngoài phạm vi cluster này (chưa fix, note lại cho session sau)
- Dọn 3+ generation key mồ côi trong namespace `"distance"` + generation trong `"player"`
  trên flash thật — chưa có cơ chế enumerate/xoá NVS key theo chương trình trong firmware.
  `tools/full_erase.sh` xoá được (xoá SẠCH toàn bộ flash) nhưng đó là búa tạ, không phải
  dọn có chọn lọc.
- Không thêm nút factory-reset web-facing — cần thiết kế auth/confirmation riêng.

## Liên quan
- `docs/user-take-note/project-spec-v1.md` §8 Bug 1 — đã đánh dấu FIXED, có tóm tắt tương tự.
- `docs/modules.md` — `src/globals.h`, `src/cantim_mqtt_new.cpp`, `src/web.cpp` sections đã
  cập nhật cùng commit.
