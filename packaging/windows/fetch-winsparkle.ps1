$ErrorActionPreference = "Stop"
$Version = "0.9.3"
$Revision = "8ca58d903779b866eb9ed4628b0a36e4d488b623"
$Sha256 = "745985f41d2ab26b2d5a1cf87d76e4ed851039db19038e50610eb25ea0b73772"
$ProjectDirectory = (Resolve-Path "$PSScriptRoot\..\..").Path
$WorkDirectory = Join-Path $ProjectDirectory "build\winsparkle-$Version"
$Archive = Join-Path $WorkDirectory "WinSparkle-$Version.zip"
$InstallDirectory = Join-Path $WorkDirectory "install"

$CachedInstall = $false
if ((Test-Path "$InstallDirectory\WinSparkle.dll") -and
    (Test-Path "$InstallDirectory\winsparkle-tool.exe") -and
    (Test-Path "$InstallDirectory\COPYING") -and
    (Test-Path "$InstallDirectory\SOURCE.txt")) {
    $CachedInstall = $true
}
New-Item -ItemType Directory -Force $WorkDirectory | Out-Null
if (-not (Test-Path $Archive)) {
    $Url = "https://github.com/vslavik/winsparkle/releases/download/v$Version/WinSparkle-$Version.zip"
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
$ActualSha = (Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualSha -ne $Sha256) {
    throw "WinSparkle checksum mismatch: expected $Sha256, received $ActualSha"
}
if ($CachedInstall) {
    Write-Output $InstallDirectory
    exit 0
}
if (Test-Path $InstallDirectory) { Remove-Item -Recurse -Force $InstallDirectory }
New-Item -ItemType Directory -Force $InstallDirectory | Out-Null
$ExtractDirectory = Join-Path $WorkDirectory "extracted"
if (Test-Path $ExtractDirectory) { Remove-Item -Recurse -Force $ExtractDirectory }
Expand-Archive $Archive $ExtractDirectory
$ReleaseDirectory = Join-Path $ExtractDirectory "WinSparkle-$Version"
Copy-Item "$ReleaseDirectory\x64\Release\WinSparkle.dll" "$InstallDirectory\WinSparkle.dll"
Copy-Item "$ReleaseDirectory\bin\winsparkle-tool.exe" "$InstallDirectory\winsparkle-tool.exe"
Copy-Item "$ReleaseDirectory\COPYING" "$InstallDirectory\COPYING"
Copy-Item "$ReleaseDirectory\COPYING.expat" "$InstallDirectory\COPYING.expat"
@(
    "WinSparkle $Version",
    "Source revision: $Revision",
    "Release: https://github.com/vslavik/winsparkle/releases/tag/v$Version",
    "Archive SHA-256: $Sha256"
) | Set-Content "$InstallDirectory\SOURCE.txt" -Encoding utf8
Write-Output $InstallDirectory
