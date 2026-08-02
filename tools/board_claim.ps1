# board_claim.ps1  -  Đặc cách (reserve) 1 board vật lý (theo COM port) cho 1 session
#
# VẤN ĐỀ: nếu có >1 board Cân Tim cắm vào cùng 1 máy (hoặc nhiều máy chia sẻ share
# này), 2 session Claude cùng flash/monitor 1 COM port sẽ đè lên nhau (esptool
# collision, hoặc 2 session đọc/ghi serial cùng lúc). Project này chỉ có 1
# PlatformIO env (`esp32-s3-devkitc-1`) nên đơn vị claim là COM PORT, không phải
# tên env như FI-AUDIO-DMX-003 (project đó có 5 env cố định ứng với 5 board khác
# nhau  -  không áp dụng ở đây).
#
# GIẢI PHÁP: claim file per COM port tại thư mục CHUNG ngoài repo:
#     C:\pio_build\cantim_board_claims\<COMx>.claim   (JSON: ai giữ, từ lúc nào, để làm gì)
# Mọi session (mọi checkout/worktree) thấy cùng một trạng thái. Claim KHÔNG tự hết
# hạn  -  release là bắt buộc.
#
# KHÔNG port phần "board-notify-on-release" (POST HTTP báo board tắt đèn hiệu) của
# DMX003  -  firmware project này chưa có endpoint tương ứng, không fabricate.
#
# Usage:
#   ./tools/board_claim.ps1 claim   COM6 -Note "flash test threshold moi"
#   ./tools/board_claim.ps1 check   COM6              # exit 0 = free/của mình, 3 = session khac giu
#   ./tools/board_claim.ps1 wait    COM6 -TimeoutSec 1800
#   ./tools/board_claim.ps1 release COM6              # chỉ chủ claim (-Force để cướp)
#   ./tools/board_claim.ps1 release-mine              # nhả MỌI claim của session này
#   ./tools/board_claim.ps1 status                    # liệt kê claim đang có + ghi STATUS.md
#
# Identity của "session" = đường dẫn thư mục làm việc (git rev-parse --show-toplevel,
# hoặc CWD nếu không phải git repo). Hai session trong CÙNG một thư mục được coi là
# một chủ.
#
# Exit codes: 0 = OK/free/mine · 1 = lỗi tham số · 3 = board bị session khác giữ
#             4 = wait timeout

param(
    [Parameter(Mandatory=$true, Position=0)]
    [ValidateSet('claim','release','release-mine','check','wait','status')]
    [string]$Action,
    [Parameter(Position=1)][string]$ComPort,
    [string]$Note = "",
    [int]$TimeoutSec = 3600,
    [int]$PollSec = 15,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ClaimDir = 'C:\pio_build\cantim_board_claims'
if (-not (Test-Path $ClaimDir)) { New-Item -ItemType Directory -Force $ClaimDir | Out-Null }

function Get-SessionId {
    try {
        $top = (git rev-parse --show-toplevel 2>$null)
        if ($top) { return $top.Trim() }
    } catch {}
    return (Get-Location).Path
}
function Get-BranchName {
    try { (git rev-parse --abbrev-ref HEAD 2>$null).Trim() } catch { '?' }
}
function Get-ClaimPath([string]$port) { Join-Path $ClaimDir "$port.claim" }
function Read-Claim([string]$port) {
    $p = Get-ClaimPath $port
    if (-not (Test-Path $p)) { return $null }
    try { Get-Content $p -Raw | ConvertFrom-Json } catch { $null }
}
function Show-Claim($c) {
    $age = [int]((Get-Date) - [datetime]$c.claimedAt).TotalMinutes
    "  port     : $($c.port)`n  giu boi  : $($c.session)`n  branch   : $($c.branch)`n  tu luc   : $($c.claimedAt)  (~$age phut truoc)`n  ly do    : $($c.note)"
}

function Write-StatusFile {
    # Best-effort  -  KHÔNG BAO GIỜ được làm claim/release thất bại vì lỗi ghi file này.
    try {
        $path = Join-Path $ClaimDir 'STATUS.md'
        $lines = @(
            "# Cantim Board Claim Status"
            ""
            '> File TU SINH boi `tools/board_claim.ps1`  -  khong sua tay, se bi ghi de.'
            ""
            "_Cap nhat luc: $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))_"
            ""
            "| COM port | Trang thai | Giu boi | Branch | Tu luc (~phut truoc) | Ly do |"
            "|---|---|---|---|---|---|"
        )
        $files = Get-ChildItem $ClaimDir -Filter '*.claim' -ErrorAction SilentlyContinue
        if (-not $files) {
            $lines += "| _(chua co claim nao)_ | | | | | |"
        }
        foreach ($f in $files) {
            $c = Read-Claim ([IO.Path]::GetFileNameWithoutExtension($f.Name))
            if (-not $c) { continue }
            $age = [int]((Get-Date) - [datetime]$c.claimedAt).TotalMinutes
            $mine = if ($c.session -eq $me) { ' *(session nay)*' } else { '' }
            $noteEsc = ($c.note -replace '\|', '\|')
            $lines += "| ``$($c.port)`` | BUSY$mine | $($c.session) | $($c.branch) | $($c.claimedAt) (~${age}p) | $noteEsc |"
        }
        $lines -join "`r`n" | Set-Content $path -Encoding UTF8 -NoNewline
    } catch {
        Write-Host "  [STATUS] Khong ghi duoc STATUS.md ($($_.Exception.Message))  -  khong anh huong claim/release." -ForegroundColor DarkYellow
    }
}

if ($Action -notin @('status','release-mine') -and [string]::IsNullOrWhiteSpace($ComPort)) {
    Write-Host "[ERROR] Thieu ten COM port (vd: COM6). Xem tools/board_claim.ps1 header." -ForegroundColor Red
    exit 1
}

$me = Get-SessionId

switch ($Action) {

    'status' {
        Write-StatusFile
        $files = Get-ChildItem $ClaimDir -Filter '*.claim' -ErrorAction SilentlyContinue
        if (-not $files) { Write-Host "[CLAIM] Khong co board nao dang bi giu  -  tat ca tu do." -ForegroundColor Green; exit 0 }
        foreach ($f in $files) {
            $c = Read-Claim ([IO.Path]::GetFileNameWithoutExtension($f.Name))
            if ($c) {
                $mine = if ($c.session -eq $me) { ' (CUA SESSION NAY)' } else { '' }
                Write-Host "[CLAIM]$mine" -ForegroundColor Yellow
                Write-Host (Show-Claim $c)
            }
        }
        exit 0
    }

    'claim' {
        $existing = Read-Claim $ComPort
        if ($existing -and $existing.session -ne $me -and -not $Force) {
            Write-Host "[BUSY] '$ComPort' dang bi session khac giu:" -ForegroundColor Red
            Write-Host (Show-Claim $existing)
            Write-Host "-> Chon board/COM khac (status), hoac 'wait $ComPort', hoac -Force (CHI khi user duyet)." -ForegroundColor Yellow
            exit 3
        }
        $claim = [ordered]@{
            port      = $ComPort
            session   = $me
            branch    = Get-BranchName
            note      = $Note
            claimedAt = (Get-Date).ToString('o')
        }
        $claim | ConvertTo-Json | Set-Content (Get-ClaimPath $ComPort) -NoNewline
        $verb = if ($existing -and $existing.session -eq $me) { 'GIA HAN' } elseif ($existing) { 'CUOP (-Force)' } else { 'GIU' }
        Write-Host "[CLAIM] $verb '$ComPort' cho session:`n  $me" -ForegroundColor Green
        if ($Note) { Write-Host "  ly do: $Note" }
        Write-Host "  Nho RELEASE khi xong: ./tools/board_claim.ps1 release $ComPort" -ForegroundColor Cyan
        Write-StatusFile
        exit 0
    }

    'check' {
        $c = Read-Claim $ComPort
        if (-not $c) { Write-Host "[FREE] '$ComPort' tu do." -ForegroundColor Green; exit 0 }
        if ($c.session -eq $me) { Write-Host "[MINE] '$ComPort' do chinh session nay giu." -ForegroundColor Green; exit 0 }
        Write-Host "[BUSY] '$ComPort' dang bi session khac giu:" -ForegroundColor Red
        Write-Host (Show-Claim $c)
        exit 3
    }

    'wait' {
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ($true) {
            $c = Read-Claim $ComPort
            if (-not $c -or $c.session -eq $me) {
                Write-Host "[FREE] '$ComPort' da tu do  -  tiep tuc duoc." -ForegroundColor Green
                exit 0
            }
            if ((Get-Date) -gt $deadline) {
                Write-Host "[TIMEOUT] Cho $TimeoutSec s nhung '$ComPort' van bi giu:" -ForegroundColor Red
                Write-Host (Show-Claim $c)
                exit 4
            }
            $left = [int]($deadline - (Get-Date)).TotalSeconds
            Write-Host "[WAIT] '$ComPort' bi giu boi $($c.branch) ($($c.note))  -  thu lai sau ${PollSec}s (con ${left}s)..."
            Start-Sleep -Seconds $PollSec
        }
    }

    'release-mine' {
        $files = Get-ChildItem $ClaimDir -Filter '*.claim' -ErrorAction SilentlyContinue
        $released = 0
        foreach ($f in $files) {
            $c = Read-Claim ([IO.Path]::GetFileNameWithoutExtension($f.Name))
            if ($c -and $c.session -eq $me) {
                Remove-Item $f.FullName -Force -Confirm:$false
                Write-Host "[RELEASE] '$($c.port)' da duoc nha." -ForegroundColor Green
                $released++
            }
        }
        if ($released -eq 0) { Write-Host "[CLAIM] Session nay khong giu board/COM nao." -ForegroundColor Yellow }
        Write-StatusFile
        exit 0
    }

    'release' {
        $c = Read-Claim $ComPort
        if (-not $c) { Write-Host "[CLAIM] '$ComPort' von khong bi giu  -  khong co gi de nha." -ForegroundColor Yellow; exit 0 }
        if ($c.session -ne $me -and -not $Force) {
            Write-Host "[DENIED] Claim '$ComPort' thuoc session khac  -  chi chu claim duoc nha (hoac -Force khi user duyet):" -ForegroundColor Red
            Write-Host (Show-Claim $c)
            exit 3
        }
        Remove-Item (Get-ClaimPath $ComPort) -Force -Confirm:$false
        Write-Host "[RELEASE] '$ComPort' da duoc nha  -  session khac flash/monitor duoc." -ForegroundColor Green
        Write-StatusFile
        exit 0
    }
}
