---
name: parallel-session-coordination
description: Quy tắc phối hợp khi có NHIỀU session Claude cùng làm việc trên project Phòng Cân Tim (nhiều board vật lý, hoặc nhiều git worktree). Dùng khi phát hiện/được báo có session khác đang chạy song song, khi thấy file lạ trong `git status` mà mình không tạo, trước MỌI lệnh git stash/checkout/merge/reset, hoặc trước khi flash/monitor 1 board mà không chắc board đó có đang bị session khác dùng không. Bản rút gọn từ FI-AUDIO-DMX-003 — project này chỉ 1 PlatformIO env nên KHÔNG port phần multi-env/pio_wt.ps1/shared-build-dir của DMX003 (không áp dụng, xem §5).
---

# Parallel Session Coordination — Phòng Cân Tim

## Vì sao skill này tồn tại

Project có thể có **nhiều board Cân Tim vật lý** cắm cùng lúc (nhiều địa điểm cân,
test song song), và/hoặc nhiều session Claude cùng sửa code. Rủi ro thật:
1. Hai session cùng flash/monitor **cùng 1 COM port** → collision (esptool có thể
   brick board giữa chừng, hoặc 2 process cùng đọc serial phá dữ liệu của nhau).
2. Hai session cùng sửa file trong **cùng một thư mục làm việc** (không dùng
   worktree riêng) → tự đè lên nhau. Đây chính là Rule #5 trong `CLAUDE.md`.

## Quy tắc 1 dòng

**Mỗi session làm việc trong thư mục/worktree RIÊNG nếu code song song thật (không
chỉ 1 người gõ 2 lệnh nối tiếp).** Nếu chỉ có 1 thư mục làm việc, coi như KHÔNG có
"song song" thật — chỉ cần theo Rule #5 (git status trước khi sửa, dừng nếu thấy
file lạ).

```bash
git worktree add ../<feature-folder> feat/<branch>   # session nhận thư mục riêng
git worktree list                                     # xác nhận đã tách
```

## Trước MỌI lệnh git — checklist

1. `git branch --show-current` — đúng branch mình nghĩ chưa? (detached HEAD = STOP)
2. `git status` — có file mình KHÔNG tạo ra không?
   - **CÓ → STOP.** Đừng stash/checkout/merge/reset. Báo user file nào lạ, chờ chỉ
     đạo — đây là dấu hiệu session song song đang sửa cùng file (xem `CLAUDE.md` §5).
   - KHÔNG → tiếp tục.
3. Lệnh có phá hoại không (`reset --hard`, `push --force`, `clean -f`, `stash`+switch)?
   → Hỏi user trước. Ưu tiên cách không phá hoại.

## Commit hygiene (nhiều session)

- `git add <path-cụ-thể>` — KHÔNG `git add .` / `git add -A` khi biết có session
  khác đang chạy, dễ cuốn theo file của họ.
- Chỉ commit file session mình tạo/sửa.

## Build — project này KHÔNG cần lock

Khác với DMX003 (đã hard-code `build_dir` dùng chung ngoài repo → phải có
`pio_wt.ps1` cấp phát build dir riêng mỗi worktree để tránh clobber), project này
**không override `build_dir`** trong `platformio.ini` — PlatformIO mặc định đặt
build dir tại `.pio/build/` NGAY TRONG mỗi checkout/worktree. Hai worktree khác
thư mục = hai `.pio/` khác nhau = build song song tự nhiên AN TOÀN, không cần
wrapper. Chỉ 2 session cùng build TRONG CÙNG một thư mục mới cạnh tranh — tránh
bằng cách không chạy `pio run` chồng lệnh nhau trong cùng 1 checkout.

## Flash / Monitor cùng COM port — PHẢI claim

`-t upload` hoặc `pio device monitor` mở cùng 1 COM port từ 2 session → collision.
Dùng `tools/board_claim.ps1` (bản rút gọn từ DMX003, đơn vị claim là **COM port**
vì project chỉ có 1 PlatformIO env, không phải 5 env như DMX003):

```powershell
./tools/board_claim.ps1 claim   COM6 -Note "flash test threshold moi"
./tools/board_claim.ps1 check   COM6      # exit 0 free/mine, 3 = session khac giu
./tools/board_claim.ps1 wait    COM6      # cho toi khi duoc nha (poll 15s)
./tools/board_claim.ps1 status            # liet ke moi claim + ghi STATUS.md
./tools/board_claim.ps1 release COM6      # nha 1 claim
./tools/board_claim.ps1 release-mine      # nha MOI claim cua session nay
```

Claim file lưu ở `C:\pio_build\cantim_board_claims\` (chung cho mọi checkout/máy
chia sẻ đường dẫn đó) — **không tự hết hạn**, release là bắt buộc.

**KHÔNG port** phần "board-notify-on-release" (POST HTTP báo board tắt đèn hiệu)
của DMX003 — firmware Cân Tim chưa có endpoint tương ứng (`/api/fw/test-release`),
không fabricate tính năng không tồn tại.

**Thời điểm release (quan trọng):** claim "cho đến khi xong" nghĩa là đến CUỐI
chuỗi hành động của prompt hiện tại — chạy `release-mine` trước khi viết tóm tắt
cuối và dừng chờ user. ĐỪNG giữ claim khi đang idle chờ user trả lời — claim không
tự hết hạn nên một claim bị quên sẽ chặn session khác vô thời hạn.

## Thẻ quyết định nhanh

```
Thấy file lạ trong git status?        → STOP, hỏi user, đừng đụng vào
Sắp stash + đổi branch?               → Chỉ khi cây làm việc chỉ có file của mình
Chuẩn bị flash/monitor 1 COM port?    → board_claim.ps1 check trước, claim nếu free
Board bị session khác claim?          → chọn board khác, hoặc board_claim.ps1 wait
Build 2 worktree khác thư mục cùng lúc? → OK, build_dir mặc định tự tách, không cần lock
Build 2 session CÙNG 1 thư mục?       → không làm — xếp hàng, hoặc tách worktree
Commit biến mất?                      → git reflog → checkout -b restore
Merge conflict?                       → STOP, báo user
```

## Recovery khi commit "biến mất"

```bash
git reflog                                  # tìm hash commit của mình
git checkout <hash> -b feat/<name>-restore  # cứu vào branch mới
```

Đừng `reset --hard` để "sửa" — nó phá luôn. Tách branch từ reflog hash thay vào đó.
