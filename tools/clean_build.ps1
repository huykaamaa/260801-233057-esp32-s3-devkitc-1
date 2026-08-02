<#
.SYNOPSIS
  Xoa cache build (.pio/build/, .pio/libdeps/) cua checkout/worktree hien tai.

.DESCRIPTION
  Du project khong override build_dir (moi worktree tu co .pio/ rieng, xem
  .claude/skills/parallel-session-coordination/SKILL.md ss5), .pio/build/ van
  tich luy object file qua nhieu lan build trong VONG DOI cua MOT worktree/checkout
  duy nhat - dac biet khi worktree do song lau qua nhieu commit fix khac nhau
  (vd: chuoi audit/fix nhieu buoc trong cung 1 worktree). Chay script nay sau
  moi commit lon hoac ngay truoc khi merge worktree ve main, de tranh object
  file cu lan ket qua build moi.

  KHONG xoa .pio/build_native (build native rieng, PC-side, khong dinh phan cung).

.PARAMETER Path
  Thu muc project can don (mac dinh: thu muc hien tai). Truyen duong dan worktree
  neu dang dung tu ben ngoai worktree do.

.EXAMPLE
  ./tools/clean_build.ps1
  ./tools/clean_build.ps1 -Path ../cantim-fix
#>
param(
  [string]$Path = "."
)

$ErrorActionPreference = "Stop"
$resolved = Resolve-Path $Path -ErrorAction Stop
$buildDir = Join-Path $resolved ".pio\build"
$libdepsDir = Join-Path $resolved ".pio\libdeps"

if (-not (Test-Path (Join-Path $resolved "platformio.ini"))) {
  Write-Error "$resolved khong co platformio.ini - sai thu muc project?"
  exit 1
}

$removed = @()
foreach ($d in @($buildDir, $libdepsDir)) {
  if (Test-Path $d) {
    Remove-Item -Recurse -Force -Confirm:$false $d
    $removed += $d
  }
}

if ($removed.Count -eq 0) {
  Write-Output "Khong co gi de don (.pio/build, .pio/libdeps da sach) trong $resolved"
} else {
  Write-Output "Da xoa: $($removed -join ', ')"
  Write-Output "Lan build ke tiep se compile lai tu dau (binh thuong, khong phai loi)."
}
