[CmdletBinding()]
param(
    [string]$ExternalDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "External")
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$ExternalDir = [System.IO.Path]::GetFullPath($ExternalDir)

$dependencies = @(
    [pscustomobject]@{
        Name        = "glfw"
        Uri         = "https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.WIN64.zip"
        ArchiveRoot = "glfw-3.4.bin.WIN64"
        Marker      = "include/GLFW/glfw3.h"
        CopyPaths   = @("include", "lib-vc2022", "LICENSE.md")
    },
    [pscustomobject]@{
        Name        = "glm"
        Uri         = "https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip"
        ArchiveRoot = "glm-1.0.1"
        SourceSubdir = "glm"
        Marker      = "glm.hpp"
        CopyPaths   = @(".")
    },
    [pscustomobject]@{
        Name        = "imgui"
        Uri         = "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.3.zip"
        ArchiveRoot = "imgui-1.92.3"
        Marker      = "imgui.h"
        CopyPaths   = @(".")
    },
    [pscustomobject]@{
        Name        = "tinygltf"
        Uri         = "https://github.com/syoyo/tinygltf/archive/refs/tags/v2.9.6.zip"
        ArchiveRoot = "tinygltf-2.9.6"
        Marker      = "tiny_gltf.h"
        CopyPaths   = @(".")
    },
    [pscustomobject]@{
        Name        = "ktx"
        Uri         = "https://github.com/KhronosGroup/KTX-Software/archive/refs/tags/v3.0.1.zip"
        ArchiveRoot = "KTX-Software-3.0.1"
        Marker      = "include/ktx.h"
        CopyPaths   = @("include", "other_include", "lib", "LICENSE.md", "NOTICE.md")
    },
    [pscustomobject]@{
        Name        = "assimp"
        Uri         = "https://github.com/assimp/assimp/archive/refs/tags/v6.0.5.zip"
        ArchiveRoot = "assimp-6.0.5"
        Marker      = "include/assimp/Importer.hpp"
        CopyPaths   = @(".")
    },
    [pscustomobject]@{
        Name        = "stb"
        Uri         = "https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.zip"
        ArchiveRoot = "stb-2c980bb59875b0d32144a71867fbdebb2f77cd20"
        Marker      = "stb_image.h"
        CopyPaths   = @("stb_image.h", "stb_image_write.h")
    }
)

function Copy-DependencyPayload {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string[]]$CopyPaths
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($relativePath in $CopyPaths) {
        if ($relativePath -eq ".") {
            Get-ChildItem -LiteralPath $SourceRoot -Force | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
            }
            continue
        }

        $source = Join-Path $SourceRoot $relativePath
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Expected dependency payload is missing: $source"
        }

        Copy-Item -LiteralPath $source -Destination $Destination -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $ExternalDir -Force | Out-Null
$downloadDir = Join-Path $ExternalDir ".downloads"
New-Item -ItemType Directory -Path $downloadDir -Force | Out-Null

foreach ($dependency in $dependencies) {
    $destination = Join-Path $ExternalDir $dependency.Name
    $marker = Join-Path $destination $dependency.Marker

    if (Test-Path -LiteralPath $marker) {
        Write-Host "[skip] $($dependency.Name) already restored"
        continue
    }

    if (Test-Path -LiteralPath $destination) {
        throw "Destination exists but is incomplete: $destination. Remove it manually and rerun this script."
    }

    $archivePath = Join-Path $downloadDir ("$($dependency.Name).zip")
    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "[download] $($dependency.Name)"
        Invoke-WebRequest -Uri $dependency.Uri -OutFile $archivePath
    }

    $extractRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("VulkanRenderer-$($dependency.Name)-$([guid]::NewGuid().ToString('N'))")
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    try {
        Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot
        $sourceRoot = Join-Path $extractRoot $dependency.ArchiveRoot
        if (-not (Test-Path -LiteralPath $sourceRoot)) {
            throw "Unexpected archive layout for $($dependency.Name): $sourceRoot"
        }

        $payloadRoot = $sourceRoot
        if ($dependency.SourceSubdir) {
            $payloadRoot = Join-Path $sourceRoot $dependency.SourceSubdir
            if (-not (Test-Path -LiteralPath $payloadRoot)) {
                throw "Expected dependency source subdirectory is missing: $payloadRoot"
            }
        }

        Write-Host "[restore] $($dependency.Name)"
        Copy-DependencyPayload -SourceRoot $payloadRoot -Destination $destination -CopyPaths $dependency.CopyPaths
    }
    finally {
        if (Test-Path -LiteralPath $extractRoot) {
            $tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
            $resolvedExtractRoot = [System.IO.Path]::GetFullPath($extractRoot)
            if (-not $resolvedExtractRoot.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove extraction directory outside the temp root: $resolvedExtractRoot"
            }
            Remove-Item -LiteralPath $resolvedExtractRoot -Recurse -Force
        }
    }
}

Write-Host "Pinned dependencies are ready under $ExternalDir"