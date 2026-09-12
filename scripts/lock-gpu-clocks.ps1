[CmdletBinding()]
param(
    [ValidateSet("status", "lock", "restore")]
    [string]$Action = "status",
    [int]$Index = 0,
    # lock 时使用的最小/最大 SM 频率(MHz)；留空则取最大频率的 90%~100%
    [int]$MinMHz = 0,
    [int]$MaxMHz = 0,
    [string]$StateFile = ""
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command nvidia-smi -ErrorAction SilentlyContinue)) { throw "nvidia-smi not found" }

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($StateFile)) {
    $StateFile = Join-Path $repositoryRoot "out/benchmark/gpu-clock-state.txt"
}

function Get-GpuInfo {
    $csv = & nvidia-smi -i $Index --query-gpu=name,clocks.sm,clocks.max.sm,clocks.mem,clocks.max.mem --format=csv,noheader,nounits
    $parts = ($csv -split ",") | ForEach-Object { $_.Trim() }
    return [pscustomobject]@{ Name = $parts[0]; SmMHz = [int]$parts[1]; MaxSmMHz = [int]$parts[2]; MemMHz = [int]$parts[3]; MaxMemMHz = [int]$parts[4] }
}

$gpu = Get-GpuInfo
Write-Host ("[gpu] {0}: SM {1}/{2} MHz, MEM {3}/{4} MHz" -f $gpu.Name, $gpu.SmMHz, $gpu.MaxSmMHz, $gpu.MemMHz, $gpu.MaxMemMHz)

switch ($Action) {
    "status" {
        if (Test-Path -LiteralPath $StateFile) { Write-Host ("[state] " + (Get-Content -LiteralPath $StateFile -Raw).Trim()) }
        else { Write-Host "[state] 未记录（未锁频）" }
    }
    "lock" {
        if ($MaxMHz -le 0) { $MaxMHz = $gpu.MaxSmMHz }
        if ($MinMHz -le 0) { $MinMHz = [int]($MaxMHz * 0.9) }
        Write-Host ("[lock] nvidia-smi -i {0} -lgc {1},{2}" -f $Index, $MinMHz, $MaxMHz)
        & nvidia-smi -i $Index -lgc "$MinMHz,$MaxMHz" | Write-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[lock] 失败：锁定 SM 频率通常需要以管理员身份运行 PowerShell。" -ForegroundColor Yellow
            throw "nvidia-smi -lgc failed (run elevated)"
        }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $StateFile) | Out-Null
        "locked,gpu=$($gpu.Name),sm=$MinMHz-$MaxMHz,mem=$($gpu.MemMHz)" | Set-Content -LiteralPath $StateFile -Encoding UTF8
        Write-Host "[lock] 状态已写入 $StateFile（benchmark 时会被记录进 summary.csv）"
    }
    "restore" {
        & nvidia-smi -i $Index -rgc | Write-Host
        if ($LASTEXITCODE -ne 0) { Write-Host "[restore] 还原失败：可能需要管理员权限。" -ForegroundColor Yellow }
        if (Test-Path -LiteralPath $StateFile) { Remove-Item -LiteralPath $StateFile -Force }
        Write-Host "[restore] 已还原为驱动默认频率"
    }
}
