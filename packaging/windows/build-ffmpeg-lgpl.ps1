$ErrorActionPreference = "Stop"
$Version = "8.1.2"
$Sha256 = "464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c"
$ProjectDir = (Resolve-Path "$PSScriptRoot\..\..").Path
$WorkDir = Join-Path $ProjectDir "build\ffmpeg-$Version-windows-x64"
$Archive = Join-Path $WorkDir "ffmpeg-$Version.tar.xz"
$SourceDir = Join-Path $WorkDir "source"
$InstallDir = Join-Path $WorkDir "install"
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$CachedInstall = $false
if ((Test-Path "$InstallDir\bin\ffmpeg.exe") -and
    (Test-Path "$InstallDir\bin\ffprobe.exe") -and
    (Test-Path "$InstallDir\BUILD_CONFIGURATION.txt") -and
    (Test-Path "$InstallDir\SOURCE.txt") -and
    (Test-Path "$InstallDir\LICENSES\FFmpeg-LGPL-2.1.txt") -and
    (Test-Path "$InstallDir\LICENSES\FFmpeg-LGPL-3.0.txt")) {
    try {
        $Configuration = Get-Content "$InstallDir\BUILD_CONFIGURATION.txt" -Raw
        foreach ($RequiredFlag in @('--disable-gpl', '--disable-nonfree', '--disable-network',
                                    '--disable-autodetect', '--disable-shared', '--enable-static')) {
            if ($Configuration -notmatch [regex]::Escape($RequiredFlag)) {
                throw "Cached FFmpeg is missing required build flag $RequiredFlag"
            }
        }
        if ($Configuration -match '--enable-(gpl|nonfree)') {
            throw "Cached FFmpeg build is not LGPL-compatible"
        }
        $VersionOutput = & "$InstallDir\bin\ffmpeg.exe" -hide_banner -version 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or $VersionOutput -notmatch '(?m)^ffmpeg version 8\.1\.2(?:\s|$)') {
            throw "Cached FFmpeg 8.1.2 executable is not runnable"
        }
        $CachedInstall = $true
    }
    catch {
        Write-Warning "Discarding an incompatible FFmpeg install cache: $($_.Exception.Message)"
        $CachedInstall = $false
    }
}
if (-not (Test-Path $Archive)) {
    $Url = "https://ffmpeg.org/releases/ffmpeg-$Version.tar.xz"
    for ($Attempt = 1; $Attempt -le 3; $Attempt++) {
        try {
            Invoke-WebRequest $Url -OutFile "$Archive.part"
            break
        }
        catch {
            if ($Attempt -eq 3) { throw }
            Start-Sleep -Seconds ([Math]::Pow(2, $Attempt - 1))
        }
    }
    Move-Item "$Archive.part" $Archive
}
if ((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha256) {
    throw "FFmpeg source checksum mismatch"
}
if ($CachedInstall) {
    Write-Output "$InstallDir\bin"
    exit 0
}
if (-not (Test-Path "C:\msys64\usr\bin\bash.exe")) {
    throw "MSYS2 bash is required at C:\msys64\usr\bin\bash.exe"
}
& "C:\msys64\usr\bin\bash.exe" -lc "export PATH=/mingw64/bin:`$PATH; command -v tar >/dev/null && command -v xz >/dev/null && command -v make >/dev/null && command -v nasm >/dev/null && command -v diff >/dev/null && command -v x86_64-w64-mingw32-gcc >/dev/null && command -v ar >/dev/null && command -v windres >/dev/null"
if ($LASTEXITCODE -ne 0) {
    throw "MSYS2 make, nasm, diffutils, and the mingw-w64 x64 GCC toolchain are required to build FFmpeg"
}
if (Test-Path $InstallDir) { Remove-Item -Recurse -Force $InstallDir }
New-Item -ItemType Directory -Force $InstallDir | Out-Null
$MsysArchive = ($Archive -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
$MsysSource = ($SourceDir -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
$MsysInstall = ($InstallDir -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
$SourceStamp = Join-Path $SourceDir '.breezedesk-source-sha256'
$CanReuseSource = (Test-Path (Join-Path $SourceDir 'configure')) -and
    (Test-Path $SourceStamp) -and
    ((Get-Content $SourceStamp -Raw).Trim() -eq $Sha256)
if (-not $CanReuseSource) {
    if (Test-Path $SourceDir) { Remove-Item -Recurse -Force $SourceDir }
    New-Item -ItemType Directory -Force $SourceDir | Out-Null
    & "C:\msys64\usr\bin\bash.exe" -lc "tar -xf '$MsysArchive' --strip-components=1 -C '$MsysSource'"
    if ($LASTEXITCODE -ne 0) { throw "FFmpeg source extraction failed" }
    Set-Content -LiteralPath $SourceStamp -Value $Sha256 -Encoding ascii -NoNewline
} else {
    Write-Host "Reusing the verified FFmpeg source tree and compiled objects from an earlier attempt."
}
$Command = "export PATH=/mingw64/bin:`$PATH; cd '$MsysSource' && ./configure --prefix='$MsysInstall' --arch=x86_64 --target-os=mingw32 --cc=x86_64-w64-mingw32-gcc --ar=ar --windres=windres --extra-ldflags='-static -static-libgcc' --disable-gpl --disable-nonfree --disable-network --disable-autodetect --disable-doc --disable-debug --disable-shared --disable-pthreads --enable-w32threads --enable-static --enable-small && make -j2 && make install"
& "C:\msys64\usr\bin\bash.exe" -lc $Command
if ($LASTEXITCODE -ne 0) { throw "FFmpeg build failed" }
New-Item -ItemType Directory -Force "$InstallDir\LICENSES" | Out-Null
Copy-Item "$SourceDir\COPYING.LGPLv2.1" "$InstallDir\LICENSES\FFmpeg-LGPL-2.1.txt"
Copy-Item "$SourceDir\COPYING.LGPLv3" "$InstallDir\LICENSES\FFmpeg-LGPL-3.0.txt"
$BuildConfiguration = & "$InstallDir\bin\ffmpeg.exe" -hide_banner -buildconf 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "The packaged ffmpeg executable could not report its build configuration" }
$BuildConfiguration | Set-Content "$InstallDir\BUILD_CONFIGURATION.txt" -Encoding utf8
@("FFmpeg $Version", "Source: https://ffmpeg.org/releases/ffmpeg-$Version.tar.xz", "SHA-256: $Sha256") |
    Set-Content "$InstallDir\SOURCE.txt" -Encoding utf8
Write-Output "$InstallDir\bin"
