# Memory Index — Phòng Cân Tim

Index duy nhất — mỗi entry 1 dòng, link tới file trong cùng thư mục `memory/`.

## Active bugs / fixes

- 🩹 [NVS key length limit + namespace conflation (2026-08-02)](nvs-key-length-conflation-2026-08-02.md) — Bug 1/F1/F29/F2/F4: 3 key NVS vượt 15 ký tự (`osc_address_full/missing`, `osc_value_missing`) khiến `putString/getString` âm thầm fail; root cause sâu hơn là dùng chung literal cho tên field HTML và key NVS (2 tầng khác giới hạn độ dài).
