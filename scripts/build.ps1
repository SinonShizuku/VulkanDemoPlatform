[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Bootstrap,
    [switch]$Run,
    [string]$ExternalDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "External")
)

$ErrorActionPreference = "Stop"

function Get-VisualStudioRoot {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installationPath) {
            return $installationPath.Trim()
        }
    }

    $candidates = @(
        $env:VSINSTALLDIR,
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Community"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Professional"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\Enterprise"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\BuildTools"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools"),
        "D:\Program Files\VisualStudio"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "VC\Auxiliary\Build\vcvars64.bat")) {
            return $candidate
        }
    }

    throw "Visual Studio with the MSVC x64 toolchain was not found."
}

function Import-VisualStudioEnvironment {
    param([Parameter(Mandatory = $true)][string]$VisualStudioRoot)

    $vcvars = Join-Path $VisualStudioRoot "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars)) {
        throw "vcvars64.bat was not found under $VisualStudioRoot"
    }

    $environmentLines = & cmd.exe /d /s /c "`"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize the Visual Studio environment."
    }

    # `set` 会同时列出仅大小写不同的重复变量（例如 PATH 与 Path）。PowerShell 的
    # 环境变量名不区分大小写，若全部写入则后者会覆盖前者，PATH 里的 CMake 等
    # 条目会凭空消失。这里按首次出现的名字去重，与 Win32 的查询语义一致。
    $importedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $environmentLines) {
        if ($line -match "^([^=]+)=(.*)$") {
            if (-not $importedNames.Add($matches[1])) {
                continue
            }
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }

    return $VisualStudioRoot
}

function Add-NinjaToPath {
    param([Parameter(Mandatory = $true)][string]$VisualStudioRoot)

    if (Get-Command ninja.exe -ErrorAction SilentlyContinue) {
        return
    }

    $candidates = @(
        (Join-Path $VisualStudioRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"),
        "D:\Program Files\Jetbrain\CLion\bin\ninja\win\x64"
    )

    foreach ($candidate in $candidates) {
        $ninja = Join-Path $candidate "ninja.exe"
        if (Test-Path -LiteralPath $ninja) {
            $env:PATH = "$candidate;$env:PATH"
            return
        }
    }

    throw "Ninja was not found. Install Ninja or add it to PATH."
}

function Add-CMakeToPath {
    param([Parameter(Mandatory = $true)][string]$VisualStudioRoot)

    if (Get-Command cmake.exe -ErrorAction SilentlyContinue) {
        return
    }

    $candidates = @(
        (Join-Path $VisualStudioRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"),
        $(if ($env:VULKAN_SDK) { Join-Path $env:VULKAN_SDK "cmake\bin" })
    )

    foreach ($candidate in $candidates) {
        if (-not $candidate) {
            continue
        }
        $cmake = Join-Path $candidate "cmake.exe"
        if (Test-Path -LiteralPath $cmake) {
            $env:PATH = "$candidate;$env:PATH"
            return
        }
    }

    throw "CMake was not found. Install CMake or add it to PATH."
}

function Get-VulkanSdkRoot {
    if ($env:VULKAN_SDK -and
        (Test-Path -LiteralPath (Join-Path $env:VULKAN_SDK "Include\vulkan\vulkan.h"))) {
        return $env:VULKAN_SDK
    }

    $searchRoots = @("C:\VulkanSDK", "D:\VulkanSDK") | Where-Object { Test-Path -LiteralPath $_ }
    $candidate = Get-ChildItem -LiteralPath $searchRoots -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "Include\vulkan\vulkan.h") } |
        Sort-Object -Property { try { [version]$_.Name } catch { [version]"0.0.0" } } -Descending |
        Select-Object -First 1

    if (-not $candidate) {
        throw "Vulkan SDK was not found. Install it from https://vulkan.lunarg.com/sdk/home or set VULKAN_SDK."
    }

    return $candidate.FullName
}

# MSVC 依赖扫描依赖英文的 "Note: including file:" 前缀；本地化（如中文代码页）会让 CMake
# 的 dyndep 收不到头文件依赖，从而漏掉 "只改头文件" 的增量重编译。这里强制英文诊断。
$env:VSLANG = "1033"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($ExternalDir)) {
    $ExternalDir = Join-Path $repositoryRoot $ExternalDir
}
$ExternalDir = [System.IO.Path]::GetFullPath($ExternalDir)

if ($Bootstrap) {
    & (Join-Path $PSScriptRoot "bootstrap-dependencies.ps1") -ExternalDir $ExternalDir
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $ExternalDir "glfw\include\GLFW\glfw3.h"))) {
    throw "Dependencies are missing. Rerun with -Bootstrap or run scripts/bootstrap-dependencies.ps1."
}

$visualStudioRoot = Import-VisualStudioEnvironment -VisualStudioRoot (Get-VisualStudioRoot)
Add-NinjaToPath -VisualStudioRoot $visualStudioRoot
Add-CMakeToPath -VisualStudioRoot $visualStudioRoot

$vulkanSdkRoot = Get-VulkanSdkRoot
$env:VULKAN_SDK = $vulkanSdkRoot
# Debug 链接 shaderc_sharedd.lib，运行时 DLL 由 CMake 拷贝到可执行文件旁；
# 这里把 Bin 目录加入 PATH，方便构建过程中的工具以及手动运行。
$env:PATH = "$(Join-Path $vulkanSdkRoot 'Bin');$env:PATH"
Write-Host "[vulkan] $vulkanSdkRoot"

$preset = "windows-ninja-$($Configuration.ToLowerInvariant())"
Push-Location $repositoryRoot
try {
    Write-Host "[configure] $preset"
    & cmake --preset $preset "-DVULKAN_RENDERER_EXTERNAL_DIR=$ExternalDir"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    Write-Host "[build] $preset"
    & cmake --build --preset $preset --parallel
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($Run) {
        $executable = Join-Path $repositoryRoot "out\build\$preset\VulkanRenderer.exe"
        if (-not (Test-Path -LiteralPath $executable)) {
            throw "Build output was not found: $executable"
        }
        Write-Host "[run] $executable"
        & $executable
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}
finally {
    Pop-Location
}

Write-Host "Built VulkanRenderer ($Configuration) in out/build/$preset"