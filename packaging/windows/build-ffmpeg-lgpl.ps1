$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Version = '8.1.2'
$Sha256 = '464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c'
$ToolchainVersion = '2.8.0'
$ToolchainSha256 = '6252bf34fe2231a55ac7f03d482b36d2c7c58697990551bba508102cfb3f342e'
$NasmVersion = '2.16.03'
$NasmSha256 = '3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6'
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$WorkDir = Join-Path $ProjectDir "build\ffmpeg-$Version-windows-x64"
$Archive = Join-Path $WorkDir "ffmpeg-$Version.tar.xz"
$SourceDir = Join-Path $WorkDir 'source'
$InstallDir = Join-Path $WorkDir 'install'
$ToolchainCache = Join-Path $ProjectDir 'build\toolchains'
$ToolchainArchive = Join-Path $ToolchainCache "w64devkit-x64-$ToolchainVersion.7z.exe"
$ToolchainExtract = Join-Path $ToolchainCache "w64devkit-$ToolchainVersion"
$NasmArchive = Join-Path $ToolchainCache "nasm-$NasmVersion-win64.zip"
$NasmExtract = Join-Path $ToolchainCache "nasm-$NasmVersion-win64"

function Invoke-PinnedDownload {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256
    )

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $ActualSha256 = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($ActualSha256 -ne $ExpectedSha256) {
            throw "Cached download checksum mismatch: $Destination"
        }
        return
    }

    $Partial = "$Destination.part"
    for ($Attempt = 1; $Attempt -le 3; $Attempt++) {
        try {
            Invoke-WebRequest -Uri $Url -OutFile $Partial
            break
        }
        catch {
            if ($Attempt -eq 3) {
                throw
            }
            Start-Sleep -Seconds ([Math]::Pow(2, $Attempt - 1))
        }
    }
    if ((Get-FileHash -LiteralPath $Partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne
        $ExpectedSha256) {
        throw "Downloaded file checksum mismatch: $Url"
    }
    Move-Item -LiteralPath $Partial -Destination $Destination -Force
}

function Test-CachedInstall {
    if (-not ((Test-Path -LiteralPath "$InstallDir\bin\ffmpeg.exe" -PathType Leaf) -and
              (Test-Path -LiteralPath "$InstallDir\bin\ffprobe.exe" -PathType Leaf) -and
              (Test-Path -LiteralPath "$InstallDir\BUILD_CONFIGURATION.txt" -PathType Leaf) -and
              (Test-Path -LiteralPath "$InstallDir\SOURCE.txt" -PathType Leaf) -and
              (Test-Path -LiteralPath "$InstallDir\LICENSES\FFmpeg-LGPL-2.1.txt" -PathType Leaf) -and
              (Test-Path -LiteralPath "$InstallDir\LICENSES\FFmpeg-LGPL-3.0.txt" -PathType Leaf) -and
              (Test-Path -LiteralPath "$InstallDir\LICENSES\MinGW-w64-runtime.txt" -PathType Leaf))) {
        return $false
    }

    try {
        $Configuration = Get-Content -LiteralPath "$InstallDir\BUILD_CONFIGURATION.txt" -Raw
        foreach ($RequiredFlag in @('--disable-gpl', '--disable-nonfree', '--disable-network',
                                    '--disable-autodetect', '--disable-shared', '--enable-static')) {
            if ($Configuration -notmatch [regex]::Escape($RequiredFlag)) {
                throw "Cached FFmpeg is missing required build flag $RequiredFlag"
            }
        }
        if ($Configuration -match '--enable-(gpl|nonfree)') {
            throw 'Cached FFmpeg build is not LGPL-compatible'
        }
        if ($Configuration -match '--disable-x86asm') {
            throw 'Cached FFmpeg build is missing NASM optimizations'
        }
        $VersionOutput = & "$InstallDir\bin\ffmpeg.exe" -hide_banner -version 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $VersionOutput -notmatch '(?m)^ffmpeg version 8\.1\.2(?:\s|$)') {
            throw 'Cached FFmpeg 8.1.2 executable is not runnable'
        }
        return $true
    }
    catch {
        Write-Warning "Discarding an incompatible FFmpeg install cache: $($_.Exception.Message)"
        return $false
    }
}

function Resolve-W64DevkitHome {
    if (-not [string]::IsNullOrWhiteSpace($env:BREEZEDESK_W64DEVKIT_DIR)) {
        $ToolchainHomePath = [IO.Path]::GetFullPath($env:BREEZEDESK_W64DEVKIT_DIR)
        if (-not (Test-Path -LiteralPath (Join-Path $ToolchainHomePath 'bin\sh.exe') -PathType Leaf)) {
            throw "BREEZEDESK_W64DEVKIT_DIR does not contain bin\sh.exe: $ToolchainHomePath"
        }
    }
    else {
        [void](New-Item -ItemType Directory -Force -Path $ToolchainCache)
        $DownloadArguments = @{
            Url = "https://github.com/skeeto/w64devkit/releases/download/v$ToolchainVersion/w64devkit-x64-$ToolchainVersion.7z.exe"
            Destination = $ToolchainArchive
            ExpectedSha256 = $ToolchainSha256
        }
        Invoke-PinnedDownload @DownloadArguments

        $ToolchainHomePath = Join-Path $ToolchainExtract 'w64devkit'
        if (-not (Test-Path -LiteralPath (Join-Path $ToolchainHomePath 'bin\sh.exe') -PathType Leaf)) {
            if (Test-Path -LiteralPath $ToolchainExtract) {
                Remove-Item -LiteralPath $ToolchainExtract -Recurse -Force
            }
            [void](New-Item -ItemType Directory -Force -Path $ToolchainExtract)
            Write-Host "Extracting checksum-pinned portable w64devkit $ToolchainVersion..."
            $ExtractorArguments = @{
                FilePath = $ToolchainArchive
                ArgumentList = @('-y', "-o`"$ToolchainExtract`"")
                WindowStyle = 'Hidden'
                PassThru = $true
                Wait = $true
            }
            $Extractor = Start-Process @ExtractorArguments
            if ($Extractor.ExitCode -ne 0 -or
                -not (Test-Path -LiteralPath (Join-Path $ToolchainHomePath 'bin\sh.exe') -PathType Leaf)) {
                throw "w64devkit extraction failed with exit code $($Extractor.ExitCode)"
            }
        }
    }

    $ActualVersion = (Get-Content -LiteralPath (Join-Path $ToolchainHomePath 'VERSION.txt') -Raw).Trim()
    if ($ActualVersion -ne $ToolchainVersion) {
        throw "w64devkit version mismatch: expected $ToolchainVersion, found $ActualVersion"
    }
    return $ToolchainHomePath
}

function Resolve-NasmHome {
    if (-not [string]::IsNullOrWhiteSpace($env:BREEZEDESK_NASM_DIR)) {
        $NasmHomePath = [IO.Path]::GetFullPath($env:BREEZEDESK_NASM_DIR)
        if (-not (Test-Path -LiteralPath (Join-Path $NasmHomePath 'nasm.exe') -PathType Leaf)) {
            throw "BREEZEDESK_NASM_DIR does not contain nasm.exe: $NasmHomePath"
        }
    }
    else {
        [void](New-Item -ItemType Directory -Force -Path $ToolchainCache)
        $DownloadArguments = @{
            Url = "https://www.nasm.us/pub/nasm/releasebuilds/$NasmVersion/win64/nasm-$NasmVersion-win64.zip"
            Destination = $NasmArchive
            ExpectedSha256 = $NasmSha256
        }
        Invoke-PinnedDownload @DownloadArguments

        $NasmHomePath = Join-Path $NasmExtract "nasm-$NasmVersion"
        if (-not (Test-Path -LiteralPath (Join-Path $NasmHomePath 'nasm.exe') -PathType Leaf)) {
            if (Test-Path -LiteralPath $NasmExtract) {
                Remove-Item -LiteralPath $NasmExtract -Recurse -Force
            }
            Write-Host "Extracting checksum-pinned NASM $NasmVersion..."
            Expand-Archive -LiteralPath $NasmArchive -DestinationPath $NasmExtract -Force
            if (-not (Test-Path -LiteralPath (Join-Path $NasmHomePath 'nasm.exe') -PathType Leaf)) {
                throw 'NASM extraction failed'
            }
        }
    }

    $ActualVersion = & (Join-Path $NasmHomePath 'nasm.exe') -v 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $ActualVersion -notmatch "NASM version $([regex]::Escape($NasmVersion))") {
        throw "NASM version mismatch: expected $NasmVersion"
    }
    return $NasmHomePath
}

[void](New-Item -ItemType Directory -Force -Path $WorkDir)
if (Test-CachedInstall) {
    Write-Output "$InstallDir\bin"
    exit 0
}

$DownloadArguments = @{
    Url = "https://ffmpeg.org/releases/ffmpeg-$Version.tar.xz"
    Destination = $Archive
    ExpectedSha256 = $Sha256
}
Invoke-PinnedDownload @DownloadArguments

$ToolchainHome = Resolve-W64DevkitHome
$ToolchainBin = Join-Path $ToolchainHome 'bin'
$NasmHome = Resolve-NasmHome
$Shell = Join-Path $ToolchainBin 'sh.exe'
$SourceStamp = Join-Path $SourceDir '.breezedesk-source-sha256'
$BuildStamp = Join-Path $SourceDir '.breezedesk-windows-build-id'
$BuildIdentity = "$Sha256|w64devkit-$ToolchainVersion|nasm-$NasmVersion|offline-static-v1"
$CanReuseSource = (Test-Path -LiteralPath (Join-Path $SourceDir 'configure') -PathType Leaf) -and
    (Test-Path -LiteralPath $SourceStamp -PathType Leaf) -and
    ((Get-Content -LiteralPath $SourceStamp -Raw).Trim() -eq $Sha256) -and
    (Test-Path -LiteralPath $BuildStamp -PathType Leaf) -and
    ((Get-Content -LiteralPath $BuildStamp -Raw).Trim() -eq $BuildIdentity)

$BuildJobs = [Math]::Max(1, [Math]::Min([Environment]::ProcessorCount, 4))
if (-not [string]::IsNullOrWhiteSpace($env:BREEZEDESK_FFMPEG_JOBS)) {
    $ParsedJobs = 0
    if (-not [int]::TryParse($env:BREEZEDESK_FFMPEG_JOBS, [ref]$ParsedJobs) -or $ParsedJobs -lt 1) {
        throw 'BREEZEDESK_FFMPEG_JOBS must be a positive integer'
    }
    $BuildJobs = $ParsedJobs
}

$PreviousPath = $env:Path
try {
    $env:Path = "$NasmHome;$ToolchainBin;$PreviousPath"
    $env:BREEZEDESK_FFMPEG_ARCHIVE_PATH = $Archive.Replace('\', '/')
    $env:BREEZEDESK_FFMPEG_SOURCE_PATH = $SourceDir.Replace('\', '/')
    $env:BREEZEDESK_FFMPEG_INSTALL_PATH = $InstallDir.Replace('\', '/')
    $env:BREEZEDESK_FFMPEG_BUILD_JOBS = [string]$BuildJobs

    & $Shell -lc 'for tool in make gcc ar windres diff tar xz nasm; do command -v "$tool" >/dev/null || exit 1; done'
    if ($LASTEXITCODE -ne 0) {
        throw 'The portable w64devkit is missing a tool required to build FFmpeg'
    }

    if (-not $CanReuseSource) {
        if (Test-Path -LiteralPath $SourceDir) {
            Remove-Item -LiteralPath $SourceDir -Recurse -Force
        }
        [void](New-Item -ItemType Directory -Force -Path $SourceDir)
        & $Shell -lc 'tar -xf "$BREEZEDESK_FFMPEG_ARCHIVE_PATH" --strip-components=1 -C "$BREEZEDESK_FFMPEG_SOURCE_PATH"'
        if ($LASTEXITCODE -ne 0) {
            throw 'FFmpeg source extraction failed'
        }
        Set-Content -LiteralPath $SourceStamp -Value $Sha256 -Encoding ascii -NoNewline
        Set-Content -LiteralPath $BuildStamp -Value $BuildIdentity -Encoding ascii -NoNewline
    }
    else {
        Write-Host 'Reusing the verified FFmpeg source tree and compiled objects from an earlier attempt.'
    }

    if (Test-Path -LiteralPath $InstallDir) {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
    }
    [void](New-Item -ItemType Directory -Force -Path $InstallDir)

    $BuildCommand = @'
cd "$BREEZEDESK_FFMPEG_SOURCE_PATH" &&
./configure --prefix="$BREEZEDESK_FFMPEG_INSTALL_PATH" --arch=x86_64 --target-os=mingw32 --cc=gcc --ar=ar --windres=windres --extra-ldflags='-static -static-libgcc' --disable-gpl --disable-nonfree --disable-network --disable-autodetect --disable-doc --disable-debug --disable-shared --disable-pthreads --enable-w32threads --enable-static --enable-small &&
make -s V=1 -j"$BREEZEDESK_FFMPEG_BUILD_JOBS" &&
make -s V=1 install
'@
    Write-Host "Building FFmpeg $Version with $BuildJobs parallel jobs..."
    & $Shell -lc $BuildCommand
    if ($LASTEXITCODE -ne 0) {
        throw 'FFmpeg build failed'
    }
}
finally {
    $env:Path = $PreviousPath
    Remove-Item Env:BREEZEDESK_FFMPEG_ARCHIVE_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:BREEZEDESK_FFMPEG_SOURCE_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:BREEZEDESK_FFMPEG_INSTALL_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:BREEZEDESK_FFMPEG_BUILD_JOBS -ErrorAction SilentlyContinue
}

[void](New-Item -ItemType Directory -Force -Path "$InstallDir\LICENSES")
Copy-Item -LiteralPath "$SourceDir\COPYING.LGPLv2.1" `
    -Destination "$InstallDir\LICENSES\FFmpeg-LGPL-2.1.txt"
Copy-Item -LiteralPath "$SourceDir\COPYING.LGPLv3" `
    -Destination "$InstallDir\LICENSES\FFmpeg-LGPL-3.0.txt"
Copy-Item -LiteralPath (Join-Path $ToolchainHome 'COPYING.MinGW-w64-runtime.txt') `
    -Destination "$InstallDir\LICENSES\MinGW-w64-runtime.txt"
$BuildConfiguration = & "$InstallDir\bin\ffmpeg.exe" -hide_banner -buildconf 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    throw 'The packaged ffmpeg executable could not report its build configuration'
}
$BuildConfiguration | Set-Content -LiteralPath "$InstallDir\BUILD_CONFIGURATION.txt" -Encoding utf8
@(
    "FFmpeg $Version"
    "Source: https://ffmpeg.org/releases/ffmpeg-$Version.tar.xz"
    "SHA-256: $Sha256"
    "Windows toolchain: w64devkit $ToolchainVersion"
    "Toolchain source: https://github.com/skeeto/w64devkit/releases/tag/v$ToolchainVersion"
    "Toolchain archive SHA-256: $ToolchainSha256"
    "Assembler: NASM $NasmVersion"
    "Assembler source: https://www.nasm.us/pub/nasm/releasebuilds/$NasmVersion/win64/"
    "Assembler archive SHA-256: $NasmSha256"
) | Set-Content -LiteralPath "$InstallDir\SOURCE.txt" -Encoding utf8
Write-Output "$InstallDir\bin"
