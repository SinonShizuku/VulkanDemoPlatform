[CmdletBinding()]
param(
    [ValidateSet("all", "sponza", "bistro")]
    [string]$Scene = "all",
    [string]$Destination = "",
    # Bistro 需要接受 NVIDIA ORCA 的许可后手动下载；把 zip 路径传进来即可自动解包。
    [string]$BistroArchive = ""
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $repositoryRoot "Assets/benchmark"
}
$Destination = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

function Get-Sponza {
    $target = Join-Path $Destination "Sponza"
    if (Test-Path -LiteralPath (Join-Path $target "glTF/Sponza.gltf")) {
        Write-Host "[sponza] already present: $target"
        return
    }
    # Khronos glTF-Sample-Assets 里的 Sponza（Intel New Sponza），CC-BY；
    # 用 blobless + sparse checkout 只取这一个模型目录，避免拉整个仓库。
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("sponza-" + [guid]::NewGuid().ToString("N"))
    Write-Host "[sponza] fetching into $temp"
    git clone --depth 1 --filter=blob:none --sparse https://github.com/KhronosGroup/glTF-Sample-Assets.git $temp
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
    git -C $temp sparse-checkout set Models/Sponza
    if ($LASTEXITCODE -ne 0) { throw "sparse-checkout failed" }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $temp "Models/Sponza/*") -Destination $target -Recurse -Force
    Remove-Item -LiteralPath $temp -Recurse -Force
    Write-Host "[sponza] -> $target"
}

function Get-Bistro {
    $target = Join-Path $Destination "Bistro"
    if (Test-Path -LiteralPath $target) {
        Write-Host "[bistro] already present: $target"
        return
    }
    if ([string]::IsNullOrWhiteSpace($BistroArchive) -or -not (Test-Path -LiteralPath $BistroArchive)) {
        Write-Host "[bistro] skipped: 需要先接受许可并从 https://developer.nvidia.com/orca/amazon-lumberyard-bistro 下载，"
        Write-Host "         然后运行： scripts/fetch-benchmark-scenes.ps1 -Scene bistro -BistroArchive <zip 路径>"
        return
    }
    Write-Host "[bistro] expanding $BistroArchive"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Expand-Archive -LiteralPath $BistroArchive -DestinationPath $target -Force
    Write-Host "[bistro] -> $target"
}

if ($Scene -in @("all", "sponza")) { Get-Sponza }
if ($Scene -in @("all", "bistro")) { Get-Bistro }

Write-Host ""
Write-Host "使用方式（--scene 指向下面任一 glTF）："
Get-ChildItem -LiteralPath $Destination -Recurse -Filter *.gltf -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host ("  VulkanRenderer --demo glTFLoading --scene ""{0}""" -f $_.FullName) }
