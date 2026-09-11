[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Bootstrap,
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

    foreach ($line in $environmentLines) {
        if ($line -match "^([^=]+)=(.*)$") {
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
}
finally {
    Pop-Location
}

Write-Host "Built VulkanRenderer ($Configuration) in out/build/$preset"