# Phòng Cân Tim — Project Guide
> Firmware ESP32-S3: đọc cảm biến khoảng cách VL53L1X → xác định có người/vắng người → publish MQTT + OSC, cấu hình qua Web UI, lưu Preferences (NVS). Ethernet W5500.

*Nguyên tắc dưới đây học từ project chị em `FI-AUDIO-DMX-003` (`C:\( DEV CAVE 2026 )\FI-AUDIO-DMX-003`), rút gọn cho quy mô project này.*

---

# 🛑 RULE #1 — OPUS / FABLE KHÔNG CODE. ĐỌC TRƯỚC MỌI THỨ KHÁC.

**Đang chạy Opus hoặc Fable → bạn là planner/auditor. KHÔNG dùng `Bash`, `Edit`, `Write`, KHÔNG commit.**

| Model | Được làm | `Bash`/`Edit`/`Write`/commit |
|---|---|---|
| **Opus**, **Fable** (ngang vai) | plan, root-cause, audit, đọc file (`Read`/`Grep`/`Glob`) | ❌ **KHÔNG** (trừ 3 ngoại lệ hẹp bên dưới: doc, ledger SDD, `git merge`) |
| **Sonnet** | code, build, flash, commit | ✅ |

- Cần code → **dispatch Sonnet**: `Agent` tool với `model: "sonnet"`. Sonnet xong → Opus/Fable review.
- **Các ngoại lệ được phép** (chỉ đúng phạm vi liệt kê, không suy rộng):
  1. Sửa tài liệu (`docs/*.md`, `CLAUDE.md`) khi user yêu cầu rõ.
  2. **Ghi/cập nhật file ledger SDD** (progress ledger của plan đang chạy, vd `.superpowers/sdd/progress.md` hoặc file ledger tương ứng plan) — Opus/Fable được tự ghi tiến độ task/kết quả review sau khi dispatch Sonnet, không cần dispatch riêng chỉ để ghi sổ.
  3. **`git merge`** khi sub-agent Sonnet gặp khó khăn/bế tắc và cần Opus/Fable can thiệp để hợp nhất nhánh đang dở — ngoại lệ RIÊNG cho `merge`, không mở rộng sang các lệnh git khác (`commit`, `push`, `reset`, `checkout -- <file>`...).
  - Các ngoại lệ trên **KHÔNG bao gồm** `git commit` code, `git push`, hay lệnh bash thăm dò khác — vẫn phải hỏi user.
- Sonnet bế tắc nhiều lần → mới được code trực tiếp.
- **⚠️ Rule này KHÔNG được cưỡng chế bởi hook** — chỉ có sự tự giác của model (payload `PreToolUse` không có field `model`). Nếu đang là Opus/Fable và vừa định gọi `Bash`/`Edit`/`Write` ngoài 3 ngoại lệ trên — đó chính là lúc rule này áp dụng.

**Output yêu cầu:** Opus/Fable → plan Markdown rõ từng bước. Sonnet → code chạy được + báo cáo kết quả.

---

## PLUGINS
Luôn dùng plugin **superpowers** cho bước lên plan (`superpowers:writing-plans` hoặc skill tương đương). Dùng superpowers tối đa có thể trước khi code — brainstorm/plan trước, execute sau.

**Brainstorm chỉ ở design/early discovery** — bug fix đơn giản / task đã có plan rõ thì **KHÔNG** brainstorm, đọc plan rồi execute thẳng. Không chắc → hỏi user 1 câu: "brainstorm approach hay implement luôn?"

---

## 📋 Đọc trước khi sửa code
- `docs/user-take-note/project-spec-v1.md` — kiến trúc, hiện trạng, danh sách bug đã biết + hạng mục cần audit (Preferences, Ethernet blocking, buffer overflow…). **Đọc đầu tiên.**
- `docs/modules.md` — code map (file/hàm/biến, xem mục Documentation Requirements bên dưới) — đọc nếu đã tồn tại, thay vì search mù.
- Source thật: `src/cantim_mqtt_new.cpp`, `src/mqtt.*`, `src/web.*`, `src/html.*`, `src/globals.h`.

---

## Critical Context

### Hardware
- **ESP32-S3-DevKitC-1**, framework Arduino, `ARDUINO_USB_CDC_ON_BOOT=1`.
- Ethernet **W5500** qua SPI (FSPI).
- Sensor khoảng cách **VL53L1X** (I2C) — hiện code dùng biến `rsDistance[]`/RS485, kiểm tra kỹ driver thật đang dùng trước khi sửa (spec doc ghi VL53L1X nhưng code có `HardwareSerial RS485` — đối chiếu code thật, đừng tin doc mù).
- Lưu cấu hình bằng `Preferences` (NVS) — MQTT server/port/user/pass/topic, ngưỡng distance, OSC address/value.

### Build Command
```powershell
pio run -e esp32-s3-devkitc-1
```
Flash: `pio run -e esp32-s3-devkitc-1 -t upload` (chỉ 1 board/1 env, không cần wrapper claim như DMX003).

---

# Development Rules

## 1. Modularization — reuse cross-project
- Core logic (đọc/lọc sensor, quyết định state có người/vắng người, publish MQTT/OSC) viết thành module **không phụ thuộc trực tiếp global đặc thù project** — dùng struct config hoặc function-pointer callback để inject behavior (pin, ngưỡng, topic...) thay vì hardcode.
- Câu hỏi kiểm tra: *đem module này sang project ESP32 khác (khác sensor/pin/protocol), bao nhiêu dòng phải sửa?* — **>10% là chưa đủ portable**.
- Giữ tách theo concern như hiện tại: sensor/logic (`sensor_logic.*`), network (`mqtt.*`), web/HTTP handler (`web.*`), HTML render (`html.*`), config/global state (`globals.h`) — **không gộp ngược** các concern này vào 1 file khi thêm tính năng.

## 2. Tách Logic / UI, Backend / Frontend, Input / Render
- **Business logic** (tính ngưỡng distance, quyết định state, điều kiện publish) **KHÔNG** được nhúng vào hàm sinh HTML hay HTTP handler — handler chỉ gọi logic đã có sẵn rồi render kết quả.
- **Backend** (firmware logic, HTTP route, MQTT) tách khỏi **frontend** (`html.cpp`/`html.h`) — template HTML không chứa quyết định nghiệp vụ, chỉ nhận data đã xử lý sẵn để render.
- Nếu sau này thêm màn hình vật lý/bàn phím: **xử lý phím (input handler)** và **vẽ màn hình (draw/render)** phải là 2 file/2 hàm tách biệt, không gộp chung — tránh 1 hàm vừa đọc input vừa vẽ vừa chứa business logic.

## 3. Giới hạn độ dài file
- Soft cap **≤400 dòng/file** — file nào vượt khi đang sửa liên quan → ưu tiên tách trước khi thêm code mới (không bắt buộc tách ngay các file hiện có nếu không đụng tới).
- Hard trigger: **file >600 dòng → PHẢI tách nhỏ TRƯỚC khi thêm code mới** vào file đó.
- Khi tách file: cập nhật code guide (`docs/modules.md` — xem mục Documentation bên dưới) **cùng commit**.

## 4. NVS / Preferences Writes ⚠️
- **KHÔNG** ghi `prefs.putXxx()` liên tục trong `loop()` hoặc hot path — mỗi lần ghi flash tốn thời gian, ghi dồn dập có thể gây trễ/mất ổn định.
- Chỉ ghi khi user bấm **Save** trên Web UI (đã đúng pattern hiện tại) — giữ nguyên, không thêm auto-save theo chu kỳ.
- Sau khi thêm field cấu hình mới: đảm bảo có **cả** `putXxx()` (save) **và** `getXxx()` (load ở boot) — bug đã biết (`docs/user-take-note/project-spec-v1.md` Bug 1) là thiếu 1 trong 2 khiến field mất sau reboot.

## 5. Non-blocking Loop ⚠️
- **CẤM** `while(!condition)` chờ vô hạn trong `setup()`/`loop()` (bug đã biết: `while(!eth_connected)` treo board nếu chưa cắm dây Ethernet — Bug 2).
- Dùng timeout + non-blocking state check; log lỗi rồi tiếp tục loop thay vì block.
- MQTT reconnect / Ethernet reconnect: kiểm tra bằng `millis()` cooldown, không `delay()` dài trong `loop()`.

## 6. String / Buffer Safety ⚠️
- Cấu hình dùng `char buf[N]` cố định (đã đúng pattern: `char mqttServer[32]`...) — **giữ nguyên**, không đổi sang `String` cho các biến này (tránh heap fragmentation).
- Mọi `strcpy`/`sprintf` ghi vào các buffer này → dùng bản `n`-safe (`strncpy`, `snprintf`) + luôn null-terminate, đối chiếu đúng `sizeof(buf)`. Đây là hạng mục audit đã ghi nhận (buffer overflow, char array overflow).
- Dữ liệu từ Web form (`server.arg(...)`) → validate độ dài **trước khi** copy vào buffer cấu hình.

## 7. Web Server / Form Parsing
- HTML output cho input do user nhập (MQTT topic, OSC address...) → escape khi render lại vào HTML để tránh injection vào chính trang cấu hình.
- Parse form: kiểm tra `hasArg()` trước khi `arg()`, không giả định field luôn có mặt.

## 8. Multi-Session Git Safety ⚠️
**Hai session Claude sửa cùng file = self-revert**, không phải lỗi sync. Trước khi sửa: `git status`. File thay đổi "bí ẩn" giữa 2 tool call → **STOP**, kiểm tra có session song song không.

**Commit nhỏ + an toàn — canonical: skill `git-commit-discipline`.** 1 commit = 1 thay đổi logic, `git add <path cụ thể>` (không `-A`/`.`), build là gate trước nhóm commit cuối, dừng lại nếu sắp stage credential/secret bất ngờ. Build ESP32 tốn thời gian → được phép **giãn cách build** (verify gộp nhiều commit nhỏ 1 lần, hoặc dùng `pio test -e native` rẻ hơn cho logic thuần) thay vì build sau từng commit — chi tiết ở skill đó.

## 9. Debug Output
Log lỗi/trạng thái quan trọng (MQTT connect fail, Ethernet down, sensor timeout) giữ nguyên luôn bật (`LOG(...)`). Nếu thêm debug tần suất cao (mỗi vòng loop, mỗi lần đọc sensor) → gate bằng `#define DEBUG_XXX 0/1`, không để in tràn Serial production.

## 10. Tìm hàm/biến/tính năng — `docs/modules.md` trước, Grep sau ⚠️
- Cần tìm 1 hàm/biến/tính năng cụ thể → xem **`docs/modules.md` trước**. Có trong đó → dùng thẳng path + line number ghi sẵn, đọc ±50 dòng quanh đó để lấy context.
- **Chỉ khi không tìm thấy trong `docs/modules.md`** (chưa có mục đó, hoặc file chưa tồn tại) → mới tăng scope: `Grep` toàn bộ `src/`.
- **KHÔNG** đoán vị trí code, **KHÔNG** grep mù nhiều file trước khi xem modules.md — tốn token và dễ bám vào bản cũ/sai chỗ.
- Grep xong tìm ra thứ chưa có trong `docs/modules.md` → thêm mục đó vào luôn (giữ code guide theo kịp code, xem mục Documentation Requirements).

---

## 📚 Documentation Requirements

**A. User guide — bắt buộc khi thêm tính năng mới.**
Mỗi tính năng mới (feature user-facing: option Web UI mới, kênh output mới, luồng cấu hình mới...) phải có/viết/cập nhật file `docs/user-guide/<tính-năng>-guide.md`, nội dung hướng tới **người dùng cuối** (không phải dev): tính năng làm gì, cách bật/cấu hình qua Web UI, giá trị mặc định, lưu ý vận hành. Sửa tính năng có sẵn → cập nhật guide tương ứng trong cùng task, không để lệch với hành vi thật.

**B. Code guide — bắt buộc để session sau hiểu cấu trúc code.**
Duy trì `docs/modules.md` — bản đồ code cho **developer/session sau**: liệt kê từng file (`src/*.cpp`/`*.h`), chức năng chính, các hàm/biến quan trọng kèm mô tả ngắn. Khi thêm file mới hoặc tách file (theo mục 1-3 ở trên) → thêm/section vào `docs/modules.md` **cùng commit**, không để tài liệu tụt hậu so với code (tránh session sau search mù).

**C. Memory library (`memory/`) — bắt buộc khi tìm ra root-cause/quyết định đáng nhớ.** *(học từ DMX003)*
- Mỗi phát hiện đáng nhớ (root-cause 1 bug khó, quyết định kiến trúc, lý do 1 dev rule tồn tại...) → 1 file riêng trong `memory/`, đặt tên `<mô-tả-ngắn-slug>-<YYYY-MM-DD>.md`. Không dồn nhiều chủ đề vào 1 file.
- `memory/MEMORY.md` là **index duy nhất** — mỗi entry 1 dòng bullet, link bằng **relative path tới file trong cùng thư mục `memory/`** (chỉ tên file, KHÔNG dùng absolute path kiểu `C:\...`):
  ```
  - [Tiêu đề ngắn (YYYY-MM-DD)](ten-file-slug-YYYY-MM-DD.md) — tóm tắt 1 câu root-cause/fix.
  ```
- **Phân loại** — chọn 1 trong 2 cách, dùng nhất quán:
  - Group theo section header (`## Active bugs`, `## Architecture decisions`, `## Operational notes`...) khi list dài, hoặc
  - Emoji prefix ngắn gọn đầu dòng (vd 🩹 bug fix, 📐 architecture, 🔌 hardware/wiring, 🔀 git/workflow) để lọc nhanh bằng mắt.
- Cross-link giữa các memory file bằng `[[ten-file-slug]]` khi 1 bug/quyết định liên quan tới cái đã ghi trước đó — tránh chép lại nội dung.
- **Phân định 3 tầng tài liệu** (nguyên tắc gốc từ DMX003, áp dụng nguyên văn cho project này):
  - `CLAUDE.md` = rule **thi hành được** (làm gì / cấm gì) — ngắn, nạp vào MỌI session.
  - `memory/*.md` = **vì sao** rule/fix tồn tại (root cause, forensics) — chỉ đọc khi cần tra cứu (đúng mục 10 ở trên: modules.md/memory trước, grep sau).
  - `docs/*.md` (`docs/modules.md`, `docs/user-guide/*`, `docs/user-take-note/*`) = hướng dẫn dài (code map, user-facing, spec).
  - **Đừng dán narrative điều tra dài vào `CLAUDE.md`** — nó nạp vào mọi session, tốn token vô ích; narrative thuộc về `memory/*.md`.

---

## ✅ Definition of Done
Khi user xác nhận xong task ("xong", "chạy ngon rồi", "duyệt"):
1. Build PASS (`pio run -e esp32-s3-devkitc-1`).
2. Nếu fix bug đã liệt kê trong `docs/user-take-note/project-spec-v1.md` §8 → cập nhật trạng thái bug đó trong file (đánh dấu đã fix + cách fix ngắn gọn).
3. Field/feature mới trong Preferences → note vào spec doc để không lệch giữa code và tài liệu.
4. Tính năng mới/thay đổi hành vi user-facing → viết/cập nhật `docs/user-guide/<tính-năng>-guide.md` (mục A).
5. File mới hoặc tách file → cập nhật `docs/modules.md` (mục B).
6. Root-cause bug khó / quyết định kiến trúc đáng nhớ → thêm file `memory/<slug>-<YYYY-MM-DD>.md` + 1 dòng vào `memory/MEMORY.md` (mục C).
