<#
.SYNOPSIS
Packages the Microsoft Store MSIX in one step.

.DESCRIPTION
Prepares or reuses the pinned LGPL FFmpeg build, then invokes the canonical
Windows MSIX packaging pipeline. The public Partner Center identity comes from
packaging/windows/msix-identity.psd1 unless all three BREEZEDESK_MSIX_*
development overrides are set.

The output is an unsigned Store submission package. Use
packaging/windows/create-dev-certificate.ps1 separately only when a signed
local-installation test package is required.
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

try {
    Set-Location $ProjectDirectory

    $MissingCommands = @(
        foreach ($Command in 'cmake.exe', 'ninja.exe', 'windeployqt.exe', 'magick.exe') {
            if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) {
                $Command
            }
        }
    )
    if ($MissingCommands.Count -gt 0) {
        throw "Required packaging commands are not on PATH: $($MissingCommands -join ', ')."
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
    if ($null -eq $PreviousFfmpegDirectory) {
        Remove-Item Env:\BREEZEDESK_FFMPEG_DIR -ErrorAction SilentlyContinue
    }
    else {
        $env:BREEZEDESK_FFMPEG_DIR = $PreviousFfmpegDirectory
    }
}
