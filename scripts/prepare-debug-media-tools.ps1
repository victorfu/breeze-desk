[CmdletBinding()]
param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\build\debug')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Destination = [IO.Path]::GetFullPath($Destination)
$SourceBuildScript = Join-Path $ProjectDir 'packaging\windows\build-ffmpeg-lgpl.ps1'
$CachedSourceBuild = Join-Path $ProjectDir 'build\ffmpeg-8.1.2-windows-x64\install\bin'

function Get-MediaToolPair {
    param([string]$Directory)

    if ([string]::IsNullOrWhiteSpace($Directory)) {
        return $null
    }
    $FullDirectory = [IO.Path]::GetFullPath($Directory)
    $Ffmpeg = Join-Path $FullDirectory 'ffmpeg.exe'
    $Ffprobe = Join-Path $FullDirectory 'ffprobe.exe'
    if (-not (Test-Path -LiteralPath $Ffmpeg -PathType Leaf) -or
        -not (Test-Path -LiteralPath $Ffprobe -PathType Leaf)) {
        return $null
    }
    return [pscustomobject]@{
        Directory = $FullDirectory
        Ffmpeg = $Ffmpeg
        Ffprobe = $Ffprobe
    }
}

function Assert-PackagedMediaToolPolicy {
    param($Pair)

    $Version = (& $Pair.Ffmpeg -hide_banner -version 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $Version -notmatch '(?m)^ffmpeg version 8\.1\.2(?:\s|$)') {
        throw "ffmpeg is not the pinned 8.1.2 release: $($Pair.Ffmpeg)"
    }
    & $Pair.Ffprobe -hide_banner -version *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "ffprobe failed its version check: $($Pair.Ffprobe)"
    }

    $BuildConfiguration = (& $Pair.Ffmpeg -hide_banner -buildconf 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg could not report its build configuration: $($Pair.Ffmpeg)"
    }
    foreach ($RequiredFlag in @('--disable-gpl', '--disable-nonfree', '--disable-network',
                                '--disable-autodetect', '--disable-shared', '--enable-static')) {
        if ($BuildConfiguration -notmatch [regex]::Escape($RequiredFlag)) {
            throw "ffmpeg is missing required build flag $RequiredFlag."
        }
    }
    if ($BuildConfiguration -match '--enable-(gpl|nonfree)') {
        throw 'ffmpeg enables a forbidden GPL or nonfree component.'
    }
}

function Copy-MediaToolPair {
    param(
        $Pair,
        [string]$TargetDirectory
    )

    [void](New-Item -ItemType Directory -Force -Path $TargetDirectory)
    if (-not [string]::Equals($Pair.Directory, $TargetDirectory,
                              [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $Pair.Ffmpeg -Destination (Join-Path $TargetDirectory 'ffmpeg.exe') -Force
        Copy-Item -LiteralPath $Pair.Ffprobe -Destination (Join-Path $TargetDirectory 'ffprobe.exe') -Force
    }
    $DeployedPair = Get-MediaToolPair $TargetDirectory
    if ($null -eq $DeployedPair) {
        throw "Media tools were not deployed to '$TargetDirectory'."
    }
    Assert-PackagedMediaToolPolicy $DeployedPair
}

function Copy-MediaToolNotices {
    param(
        [string]$BinDirectory,
        [string]$TargetDirectory
    )

    $InstallDirectory = Split-Path -Parent $BinDirectory
    $NoticeDirectory = Join-Path $TargetDirectory 'media-tool-notices'
    [void](New-Item -ItemType Directory -Force -Path $NoticeDirectory)
    foreach ($Name in @('BUILD_CONFIGURATION.txt', 'SOURCE.txt')) {
        $Source = Join-Path $InstallDirectory $Name
        if (Test-Path -LiteralPath $Source -PathType Leaf) {
            Copy-Item -LiteralPath $Source -Destination (Join-Path $NoticeDirectory $Name) -Force
        }
    }
    $LicenseDirectory = Join-Path $InstallDirectory 'LICENSES'
    if (Test-Path -LiteralPath $LicenseDirectory -PathType Container) {
        Copy-Item -LiteralPath $LicenseDirectory -Destination $NoticeDirectory -Recurse -Force
    }
}

$ExistingPair = Get-MediaToolPair $Destination
if ($null -ne $ExistingPair) {
    try {
        Assert-PackagedMediaToolPolicy $ExistingPair
        Write-Host "Pinned offline media tools are ready in '$Destination'."
        exit 0
    }
    catch {
        Write-Warning "Replacing media tools that do not match the release policy: $($_.Exception.Message)"
    }
}

$CandidateDirectories = @()
if (-not [string]::IsNullOrWhiteSpace($env:BREEZEDESK_FFMPEG_DIR)) {
    $CandidateDirectories += $env:BREEZEDESK_FFMPEG_DIR
}
$CandidateDirectories += $CachedSourceBuild

foreach ($CandidateDirectory in $CandidateDirectories) {
    $CandidatePair = Get-MediaToolPair $CandidateDirectory
    if ($null -eq $CandidatePair) {
        continue
    }
    try {
        Assert-PackagedMediaToolPolicy $CandidatePair
        Copy-MediaToolPair -Pair $CandidatePair -TargetDirectory $Destination
        Copy-MediaToolNotices -BinDirectory $CandidatePair.Directory -TargetDirectory $Destination
        Write-Host "Preloaded pinned offline media tools from '$($CandidatePair.Directory)'."
        exit 0
    }
    catch {
        Write-Warning "Skipping incompatible media tools in '$CandidateDirectory': $($_.Exception.Message)"
    }
}

if (-not (Test-Path -LiteralPath 'C:\msys64\usr\bin\bash.exe' -PathType Leaf)) {
    throw @'
Pinned FFmpeg must be built once, but MSYS2 was not found at C:\msys64.
Install MSYS2 and the documented build packages, then rerun scripts\build-and-run.bat:
  winget install --exact --id MSYS2.MSYS2
  C:\msys64\usr\bin\bash.exe -lc "pacman -Sy --needed --noconfirm make nasm diffutils mingw-w64-x86_64-gcc"
'@
}

Write-Host 'Preparing checksum-pinned FFmpeg 8.1.2 from official source. The first build can take several minutes...'
$BuildOutput = @()
& $SourceBuildScript | Tee-Object -Variable BuildOutput
$BuiltDirectory = [string]($BuildOutput | Select-Object -Last 1)
$BuiltPair = Get-MediaToolPair $BuiltDirectory
if ($null -eq $BuiltPair) {
    throw "The FFmpeg source build did not produce ffmpeg.exe and ffprobe.exe in '$BuiltDirectory'."
}
Assert-PackagedMediaToolPolicy $BuiltPair
Copy-MediaToolPair -Pair $BuiltPair -TargetDirectory $Destination
Copy-MediaToolNotices -BinDirectory $BuiltPair.Directory -TargetDirectory $Destination
Write-Host "Built, verified, and preloaded pinned offline media tools into '$Destination'."
