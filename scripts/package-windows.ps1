<#
.SYNOPSIS
Packages the Microsoft Store MSIX in one step.

.DESCRIPTION
Prepares or reuses the pinned LGPL FFmpeg build, then invokes the canonical
Windows MSIX packaging pipeline. The public Partner Center identity comes from
packaging/windows/msix-identity.ps1 unless all three BREEZEDESK_MSIX_*
development overrides are set.

The output is an unsigned Store submission package. Use
scripts/package-windows-dev.ps1 when a separately signed local-installation
test package is required.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'scripts/package-windows.ps1 must be run on Windows.'
}

$ProjectDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$PreviousLocation = Get-Location
$PreviousFfmpegDirectory = $env:BREEZEDESK_FFMPEG_DIR
$PreviousPath = $env:Path
$PreviousCmakePrefixPath = $env:CMAKE_PREFIX_PATH
$PreviousVulkanSdk = $env:VULKAN_SDK

function Find-QtRoot {
    $Candidates = @(
        $env:BREEZEDESK_QT_ROOT
        $env:QT_PATH
        $env:Qt6_DIR
    )
    if (-not [string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
        $Candidates += $env:CMAKE_PREFIX_PATH -split ';'
    }

    $WindeployQt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($WindeployQt) {
        $Candidates += Split-Path -Parent (Split-Path -Parent $WindeployQt.Source)
    }
    foreach ($Root in 'C:\Qt', 'D:\Qt') {
        if (Test-Path -LiteralPath $Root -PathType Container) {
            $Candidates += Get-ChildItem -Path "$Root\*\msvc*_64\bin\windeployqt.exe" -File `
                -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending |
                ForEach-Object { Split-Path -Parent (Split-Path -Parent $_.FullName) }
        }
    }

    foreach ($Candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($Candidate)) {
            continue
        }
        $Candidate = [IO.Path]::GetFullPath($Candidate)
        if (Test-Path -LiteralPath (Join-Path $Candidate 'bin\windeployqt.exe') -PathType Leaf) {
            return $Candidate
        }
        if (Test-Path -LiteralPath (Join-Path $Candidate 'windeployqt.exe') -PathType Leaf) {
            return Split-Path -Parent $Candidate
        }
        if (Test-Path -LiteralPath (Join-Path $Candidate 'Qt6Config.cmake') -PathType Leaf) {
            $RootFromConfig = [IO.Path]::GetFullPath((Join-Path $Candidate '..\..\..'))
            if (Test-Path -LiteralPath (Join-Path $RootFromConfig 'bin\windeployqt.exe') -PathType Leaf) {
                return $RootFromConfig
            }
        }
    }
    throw 'Qt with windeployqt.exe was not found. Set BREEZEDESK_QT_ROOT to the Qt MSVC installation.'
}

function Find-ImageMagick {
    $Magick = Get-Command magick.exe -ErrorAction SilentlyContinue
    if ($Magick) {
        return $Magick.Source
    }

    $Candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($env:BREEZEDESK_MAGICK)) {
        $Candidates += $env:BREEZEDESK_MAGICK
    }
    $Candidates += Get-ChildItem -Path 'C:\Program Files\ImageMagick-*\magick.exe' -File `
        -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -ExpandProperty FullName
    $Candidates += Get-ChildItem -Path 'C:\Program Files (x86)\ImageMagick-*\magick.exe' -File `
        -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -ExpandProperty FullName

    foreach ($Candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($Candidate) -and
            (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
            return [IO.Path]::GetFullPath($Candidate)
        }
    }
    throw 'ImageMagick was not found. Install its prebuilt package with: winget install --id ImageMagick.ImageMagick -e'
}

function Find-VulkanSdk {
    $Candidates = @($env:VULKAN_SDK, $env:VK_SDK_PATH)
    if (Test-Path -LiteralPath 'C:\VulkanSDK' -PathType Container) {
        $Candidates += Get-ChildItem -LiteralPath 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -ExpandProperty FullName
    }

    foreach ($Candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($Candidate)) {
            continue
        }
        $Candidate = [IO.Path]::GetFullPath($Candidate)
        if ((Test-Path -LiteralPath (Join-Path $Candidate 'Include\vulkan\vulkan.h') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $Candidate 'Lib\vulkan-1.lib') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $Candidate 'Bin\glslc.exe') -PathType Leaf)) {
            return $Candidate
        }
    }
    throw 'The Vulkan SDK was not found. Install it with: winget install --id KhronosGroup.VulkanSDK -e'
}

try {
    Set-Location $ProjectDirectory

    $QtRoot = Find-QtRoot
    $QtBin = Join-Path $QtRoot 'bin'
    $MagickExecutable = Find-ImageMagick
    $MagickBin = Split-Path -Parent $MagickExecutable
    $VulkanSdk = Find-VulkanSdk
    $env:VULKAN_SDK = $VulkanSdk
    $env:Path = "$QtBin;$MagickBin;$(Join-Path $VulkanSdk 'Bin');$PreviousPath"
    $env:CMAKE_PREFIX_PATH = if ([string]::IsNullOrWhiteSpace($PreviousCmakePrefixPath)) {
        $QtRoot
    }
    else {
        "$QtRoot;$PreviousCmakePrefixPath"
    }
    Write-Host "Using Qt from $QtRoot"
    Write-Host "Using ImageMagick from $MagickExecutable"
    Write-Host "Using Vulkan SDK from $VulkanSdk"

    $MissingCommands = @(
        foreach ($Command in 'cmake.exe', 'ninja.exe', 'magick.exe') {
            if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) {
                $Command
            }
        }
    )
    if ($MissingCommands.Count -gt 0) {
        $Message = "Required packaging commands are not on PATH: $($MissingCommands -join ', ')."
        throw $Message
    }

    if ([string]::IsNullOrWhiteSpace($env:BREEZEDESK_FFMPEG_DIR)) {
        Write-Host 'Preparing pinned LGPL FFmpeg...'
        $FfmpegOutput = @(& "$ProjectDirectory\packaging\windows\build-ffmpeg-lgpl.ps1")
        if ($FfmpegOutput.Count -eq 0) {
            throw 'The FFmpeg preparation script did not return its bin directory.'
        }
        $env:BREEZEDESK_FFMPEG_DIR = [string]$FfmpegOutput[-1]
    }
    else {
        Write-Host "Using FFmpeg from $env:BREEZEDESK_FFMPEG_DIR"
    }

    $FfmpegDirectory = [IO.Path]::GetFullPath($env:BREEZEDESK_FFMPEG_DIR)
    foreach ($Executable in 'ffmpeg.exe', 'ffprobe.exe') {
        if (-not (Test-Path -LiteralPath (Join-Path $FfmpegDirectory $Executable) -PathType Leaf)) {
            throw "BREEZEDESK_FFMPEG_DIR does not contain $Executable`: $FfmpegDirectory"
        }
    }
    $env:BREEZEDESK_FFMPEG_DIR = $FfmpegDirectory

    Write-Host 'Building the Microsoft Store MSIX...'
    & cmd.exe /d /c 'packaging\windows\package.bat'
    if ($LASTEXITCODE -ne 0) {
        throw "Windows MSIX packaging failed with exit code $LASTEXITCODE."
    }

    $VersionFile = Join-Path $ProjectDirectory 'build\package-windows-version.txt'
    $ProductNameFile = Join-Path $ProjectDirectory 'build\package-identity\product-name.txt'
    if (-not (Test-Path -LiteralPath $VersionFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $ProductNameFile -PathType Leaf)) {
        throw 'The packaging pipeline did not write its version or product identity output.'
    }

    $Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
    $ProductName = if ([string]::IsNullOrWhiteSpace($env:BREEZEDESK_PRODUCT_NAME)) {
        (Get-Content -LiteralPath $ProductNameFile -Raw).Trim()
    }
    else {
        $env:BREEZEDESK_PRODUCT_NAME
    }
    $Msix = Join-Path $ProjectDirectory "dist\$ProductName-$Version-Windows-x64.msix"
    if (-not (Test-Path -LiteralPath $Msix -PathType Leaf)) {
        throw "The expected MSIX was not created: $Msix"
    }

    Write-Host ''
    Write-Host "Packaged: $Msix"
    if (Test-Path -LiteralPath "$Msix.sha256" -PathType Leaf) {
        Write-Host "Checksum: $Msix.sha256"
    }
}
finally {
    Set-Location $PreviousLocation
    $env:Path = $PreviousPath
    if ($null -eq $PreviousCmakePrefixPath) {
        Remove-Item Env:\CMAKE_PREFIX_PATH -ErrorAction SilentlyContinue
    }
    else {
        $env:CMAKE_PREFIX_PATH = $PreviousCmakePrefixPath
    }
    if ($null -eq $PreviousVulkanSdk) {
        Remove-Item Env:\VULKAN_SDK -ErrorAction SilentlyContinue
    }
    else {
        $env:VULKAN_SDK = $PreviousVulkanSdk
    }
    if ($null -eq $PreviousFfmpegDirectory) {
        Remove-Item Env:\BREEZEDESK_FFMPEG_DIR -ErrorAction SilentlyContinue
    }
    else {
        $env:BREEZEDESK_FFMPEG_DIR = $PreviousFfmpegDirectory
    }
}
