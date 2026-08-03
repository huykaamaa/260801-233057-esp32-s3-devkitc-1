# Console codepage cp1252 làm crash tiến trình `pio upload` giữa chừng

## Triệu chứng

`pio run -e esp32-s3-devkitc-1 -t upload` chạy trong bash session của agent (Windows) bị kill
với exit code 137 (SIGKILL) mà không có lỗi biên dịch/link nào. Log cho thấy tiến trình đã
qua bước build, connect ESP32-S3, erase flash, compress image — rồi treo im lặng, không tiến
triển thêm, cho tới khi bị timeout kill.

## Root cause

Windows console mặc định dùng codepage `cp1252`. esptool (chạy dưới lớp wrap của PlatformIO)
in progress bar bằng ký tự Unicode block (`█`, `░`). Khi PlatformIO đọc từng dòng output của
tiến trình con qua 1 thread riêng (`platform/_run.py::_on_stdout_line` → `_echo_line` →
`click.secho`) rồi ghi ra stdout của chính nó bằng encoding `cp1252`, gặp ký tự block này thì
`UnicodeEncodeError` — traceback:

```
File ".../click/utils.py", line 318, in echo
    file.write(out)
File ".../encodings/cp1252.py", line 19, in encode
    return codecs.charmap_encode(...)
UnicodeEncodeError: 'charmap' codec can't encode characters in position 23-52
```

Exception này xảy ra **trong 1 thread phụ** (`Thread-15`, thread đọc/echo output), không phải
thread chính chạy esptool. Thread đọc chết → không ai rút dữ liệu ra khỏi pipe stdout của
tiến trình con esptool nữa → pipe buffer đầy → esptool block ở `write()` khi cố in dòng
progress kế tiếp → cả tiến trình "treo" (không lỗi, không thoát, không tiến triển) → agent
sandbox/bash timeout kill nó sau vài phút (exit 137).

**Nguy hiểm thật sự:** vụ crash này xảy ra **giữa lúc đang ghi flash** (sau bước erase, đang
compress/ghi image) — nếu bị kill đúng lúc write() flash thật sự đang chạy dở, board có thể
kẹt ở trạng thái flash một phần (bootloader cũ + app mới ghi dở, hoặc ngược lại). May mắn lần
này log dừng lại ở bước "Compressed ... bytes" (trước khi bắt đầu ghi thật), nên chưa gây hỏng
board — nhưng đây là may, không phải đảm bảo.

## Fix

Set encoding UTF-8 cho tiến trình Python trước khi chạy MỌI lệnh `pio` có tương tác nhiều
output (upload, monitor) — không riêng gì build suông:

```bash
export PYTHONIOENCODING=utf-8
export PYTHONUTF8=1
```

Đã thêm vào skill `pio-dev` (mục "PATH" ngay đầu file) để áp dụng mặc định mọi lần gọi `pio`
trong session, không cần user nhắc lại. Xem [pio-dev/SKILL.md](../.claude/skills/pio-dev/SKILL.md).

**Đáng nhớ vì:** đây KHÔNG phải lỗi code C++ hay lỗi firmware — hoàn toàn là vấn đề môi trường
Windows console encoding, dễ tái phát ở bất kỳ project PlatformIO/esptool nào chạy trong agent
session trên Windows. Bug thể hiện ra ngoài giống hệt "lệnh bị treo/timeout không rõ lý do",
dễ nhầm là do board/driver/USB nếu không đọc traceback trong log thật.
