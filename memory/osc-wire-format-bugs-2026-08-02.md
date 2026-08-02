# OSC wire-format bugs (F9 int32 byte order, F10 string padding) — 2026-08-02

## Hiện tượng

Không có bug report từ operator — phát hiện qua audit đọc code + đối chiếu spec OSC 1.0,
không phải từ triệu chứng thực địa. Đây chính là điều đáng nhớ: 2 bug này **sai theo chuẩn**
nhưng **im lặng với default đang ship**, nên không ai từng thấy nó "hỏng" trên thiết bị thật.

## Bug 1 (F9) — int32 value ghi little-endian thay vì big-endian

`sendOscValue()` cũ ghi giá trị `int` bằng `memcpy` thẳng từ bộ nhớ native (ESP32 =
little-endian) vào gói UDP. OSC 1.0 quy định mọi giá trị số (int32, float32...) phải ở
**big-endian ("network byte order")**. Một receiver OSC chuẩn (Resolume, TouchDesigner,
`liblo`...) decode gói little-endian này SAI: gửi `value=1` bị đọc thành `16777216`
(`0x01000000` thay vì `0x00000001`).

**Vì sao im lặng với default:** `oscValueFull` mặc định là `1` — 1 con số nhỏ, byte thấp.
`1` little-endian trên wire = bytes `01 00 00 00`. Một số phần mềm nhận OSC lỏng lẻo hoặc
chỉ quan tâm "giá trị khác 0 = true" vẫn hoạt động "đúng" một cách tình cờ (giá trị đọc sai
`16777216` vẫn là non-zero, vẫn trigger cue trong 1 số hệ thống ánh sáng chỉ check truthy).
Bug chỉ lộ rõ khi phần mềm nhận thật sự dùng giá trị số (map độ sáng theo số, so sánh bằng,
log giá trị ra màn hình) — lúc đó thấy số sai hẳn nhưng không ai nghi byte order vì
"trạng thái vẫn chuyển đúng lúc".

**Fix:** byte-swap thủ công (`>>24`, `>>16`, `>>8`, giữ nguyên byte thấp) trước khi
`udp.write()`, xem `sendOscValue()` trong `src/mqtt.cpp`.

## Bug 2 (F10) — padding formula làm mất NUL terminator khi len chia hết 4

OSC string phải NUL-terminate rồi pad thêm 0 tới bội số 4 byte tiếp theo (**luôn** ít nhất 1
byte pad, kể cả khi string đã tự nhiên là bội số 4 — phải có NUL). Code cũ dùng:

```
padding = (4 - (len % 4)) % 4
```

Khi `len % 4 == 0` (string dài đúng bội số 4, vd 4/8/12/16/... ký tự), công thức trên ra
`padding = 0` — **không ghi byte pad nào**, tức là **không có NUL terminator**, đứt chuẩn OSC
hoàn toàn cho đúng những address có độ dài "đẹp".

**Vì sao im lặng với default:** default `oscAddressFull`/`oscAddressMissing` trong
`src/cantim_mqtt_new.cpp` là `"/composition/layers/1/clips/1/connect"` — **37 ký tự**,
`37 % 4 == 1`, không phải bội số 4 nên padding cũ vẫn ra `3` byte (khác 0, vô tình đúng).
Chỉ khi operator tự đổi sang 1 address có độ dài đúng bội số 4 (vd đổi `connect` 7 ký tự
thành `select` 6 ký tự → tổng 36 ký tự, `36 % 4 == 0`) mới trúng ca lỗi — dùng đúng default
ship sẵn sẽ KHÔNG BAO GIỜ thấy bug này, càng dễ khiến nó bị bỏ sót ở QA thông thường (test
bằng default rồi coi là "đã test OSC"). Địa chỉ OSC nói chung có cấu trúc
`/section/n/field` lặp lại đều nên trúng đúng bội số 4 không hiếm khi operator tự đặt tên.

**Fix:** đổi công thức thành `padding = 4 - (len % 4)` (không có `% 4` bọc ngoài) — luôn ra
`1..4` byte, không bao giờ ra `0`. Xem `writeOscString()` trong `src/mqtt.cpp`.

## Bài học chung (đáng nhớ hơn 2 con số cụ thể)

**"Chạy được với default hiện tại" không chứng minh code đúng chuẩn.** Cả 2 bug này tồn tại
từ trước audit, code build sạch, thiết bị "trông như hoạt động" (trạng thái đổi đúng lúc
trên UI), nhưng dữ liệu gửi ra ngoài (OSC packet) sai theo chuẩn OSC 1.0 và sẽ hỏng khi
operator đổi sang 1 address/value khác rơi vào đúng edge case. Đối chiếu với spec giao thức
gốc (không chỉ test bằng mắt "thấy hoạt động") là cách duy nhất bắt được lớp bug này —
tương tự bài học ở [[nvs-key-length-conflation-2026-08-02]] (giới hạn NVS cũng "im lặng" cho
tới khi đúng field vượt ngưỡng).

## Liên quan
- `docs/modules.md` — `src/mqtt.cpp` section đã cập nhật.
- Fix commit: `97906fa` — "fix: OSC wire-format bugs - big-endian int32, string padding, address validation".
