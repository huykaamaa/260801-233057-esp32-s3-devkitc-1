---
name: git-commit-discipline
description: Quy tắc dùng git để quản lý commit trong project Phòng Cân Tim — commit NHỎ và AN TOÀN, cân bằng với chi phí build. Dùng khi sắp `git add`/`git commit`, khi 1 task chạm nhiều file/nhiều thay đổi không liên quan và cần quyết định gộp hay tách commit, khi build ESP32 tốn thời gian và cần quyết định build bao nhiêu lần, trước khi `git push`, hoặc khi phát hiện file/nội dung lạ (secret, config không phải của mình) sắp bị stage. KHÔNG dùng cho Q&A thuần đọc code (xem skill `pio-dev` cho chi tiết build/flash/test).
---

# Git Commit Discipline — Phòng Cân Tim

## Nguyên tắc cốt lõi: commit NHỎ + AN TOÀN

- **1 commit = 1 thay đổi logic hoàn chỉnh** (1 bug fix, 1 field mới, 1 refactor nhỏ, 1 doc update). KHÔNG gộp nhiều task không liên quan vào 1 commit — khó revert/bisect khi có lỗi sau này.
- **Commit sớm và thường xuyên** hơn là dồn cả task thành 1 commit khổng lồ cuối cùng — mỗi commit nên là 1 checkpoint có thể quay lại được.
- Code thay đổi (không phải doc-only) → build là gate trước khi commit, nhưng **build bao nhiêu lần** thì cân bằng theo chi phí — xem mục dưới, đừng máy móc build sau MỌI commit nếu tốn tài nguyên vô ích.

## Build trước khi commit — cân bằng với chi phí build ⚠️

Mặc định: code thay đổi → build PASS trước khi commit là checkpoint tốt. Nhưng nếu đang làm
**nhiều commit nhỏ liên tiếp trong cùng 1 task** (vd tách theo lớp ở skill khác: logic → backend
→ frontend) và build ESP32 tốn thời gian đáng kể so với việc đang làm, **không bắt buộc build sau
MỖI commit** — cân nhắc:

1. **Giãn cách build (batch verify).** Gộp nhiều commit nhỏ liên quan thành 1 điểm verify: build 1
   lần SAU KHI xong cả nhóm (vd sau khi xong đủ 2-3 commit theo lớp), miễn commit trung gian không
   phải điểm cần chạy được ngay lập tức (không phải bàn giao/checkpoint cho user giữa chừng).
2. **Dùng cách rẻ hơn khi có thể.** Chỉ đổi logic thuần (`sensor_logic.*`, không đụng
   Arduino/ETH/WiFi/WebServer/Preferences) → `pio test -e native` (vài giây, chạy trên PC) thay vì
   build ESP32 đầy đủ (chậm hơn) cho từng commit nhỏ; chỉ build ESP32 đầy đủ 1 lần cuối để xác nhận
   link/flash trước khi coi cả nhóm là xong. Xem skill `pio-dev` §0/§3.
3. **Bỏ hẳn build cho thay đổi không đụng code chạy được.** Sửa `docs/*.md`, `CLAUDE.md`,
   comment-only, `.gitignore` → không build (Tier 0).
4. **Build chạy nền nếu cần tiếp tục việc khác.** Build ESP32 lâu → chạy `run_in_background`,
   tiếp tục soạn commit message/chuẩn bị thay đổi tiếp theo, quay lại đọc kết quả trước khi xác nhận
   "xong" — không đứng chờ không làm gì.

**Ranh giới không đổi:** dù giãn cách build, *nhóm commit cuối cùng trước khi báo "xong task"*
(theo Definition of Done trong `CLAUDE.md`) vẫn phải có 1 lần build PASS thật sự phủ hết thay đổi —
giãn cách là để tiết kiệm build LẶP LẠI giữa các bước trung gian, không phải để bỏ qua verify hẳn.

## Trước khi `git add` — checklist an toàn

1. `git status` — đúng những file mình định đổi, không có file lạ mình không tạo ra (xem thêm skill `parallel-session-coordination` nếu nghi có session khác).
2. `git diff` (hoặc `git diff --staged` sau khi add) — đọc lại đúng nội dung sắp commit, đặc biệt các file cấu hình/credential (`globals.h`, `*.ini`).
3. **`git add <path cụ thể>`** — KHÔNG dùng `git add -A` / `git add .` mù. Chỉ stage file liên quan tới thay đổi đang làm.
4. Sắp commit file có credential (`mqttPass`, `mqttUser`, API key, token...) → dừng lại kiểm tra đây có phải secret THẬT bị hardcode không (project hiện có `mqttPass`/`mqttUser` hardcode trong `cantim_mqtt_new.cpp` — nếu đây là secret thật của user, đó là **finding cần báo user**, không phải phần việc đang làm; **không tự ý xoá/đổi** giá trị đó khi chưa được hỏi).

## Message

Format: `feat: ...` / `fix: ...` / `docs: ...` / `refactor: ...` — mô tả NGẮN cái gì đổi.
Fix đúng 1 bug đã liệt kê trong `docs/user-take-note/project-spec-v1.md` §8 → tham chiếu số bug trong message, vd:

```
fix: Bug 2 - non-blocking Ethernet wait (timeout thay vì while chờ vô hạn)
```

## Chia nhỏ 1 task lớn thành nhiều commit

Task đụng nhiều lớp (logic → web handler → HTML/UI) → cân nhắc tách theo lớp, **mỗi commit tự build được** nếu khả thi:
1. Logic thuần (`sensor_logic.*`) trước — có thể verify bằng `pio test -e native`.
2. Network/backend (`mqtt.*`, `web.*`) sau.
3. Frontend/HTML (`html.*`) cuối.

Nếu 1 lớp không tự đứng được (build gãy khi thiếu lớp kia) → đừng ép tách, gộp tối thiểu cần thiết để mỗi commit vẫn build xanh. Xem thêm mục "Build trước khi commit" ở trên khi build tốn thời gian.

## KHÔNG được làm nếu chưa hỏi user

- `git push` — luôn hỏi trước khi đẩy lên remote.
- `git commit --amend`, `git reset --hard`, `git checkout -- <file>`, `git clean -f` — destructive, hỏi trước (theo nguyên tắc an toàn chung, không riêng git).
- **Opus/Fable tự `git commit` code** — cấm theo Rule #1 trong `CLAUDE.md`; ngoại lệ `git merge` khi Sonnet bế tắc đã ghi riêng ở đó, KHÔNG áp dụng cho `commit` thường.

## Quick check trước khi gõ `git commit`

```
git status đã xem, đúng file mình sửa?     → chưa thì xem trước
Build cần cho commit này (nhóm cuối/checkpoint)? → build trước; nếu là bước giữa chừng cùng nhóm, có thể giãn (xem mục Build ở trên)
1 commit = 1 thay đổi logic, không gộp việc lạ? → đang gộp thì tách lại
git add path cụ thể, không add -A/.?        → sửa lại nếu đang add mù
File có credential/secret bất ngờ?          → dừng, báo user, đừng tự sửa
Message rõ feat/fix/docs + ngắn gọn?        → viết lại nếu mơ hồ
```

## Related

- `CLAUDE.md` §Rule #1 — quyền `git commit`/`git merge` theo model.
- `CLAUDE.md` §8 Multi-Session Git Safety — self-revert khi 2 session cùng sửa 1 file.
- skill `parallel-session-coordination` — khi nghi có session khác chạy song song.
- skill `pio-dev` — build/test trước khi commit code, build ladder chi tiết.
