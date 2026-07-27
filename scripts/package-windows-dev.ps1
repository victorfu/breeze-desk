<#
.SYNOPSIS
Creates a signed MSIX for local development without modifying the Store MSIX.

.DESCRIPTION
Builds the unsigned Microsoft Store package unless -ReuseStorePackage is set,
verifies its SHA-256 sidecar, copies it to a -Development.msix artifact, and
signs only that copy with the local BreezeDesk development certificate.

Use -Install from an elevated PowerShell window to trust the public development
certificate in LocalMachine\TrustedPeople and install the development package.

.EXAMPLE
.\scripts\package-windows-dev.ps1

.EXAMPLE
.\scripts\package-windows-dev.ps1 -ReuseStorePackage

.EXAMPLE
.\scripts\package-windows-dev.ps1 -ReuseStorePackage -Install
#>
[CmdletBinding()]
param(
    [switch]$ReuseStorePackage,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'scripts/package-windows-dev.ps1 must be run on Windows.'
}

$ProjectDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$StorePackagingScript = Join-Path $PSScriptRoot 'package-windows.ps1'
$SigningScript = Join-Path $ProjectDirectory 'packaging\windows\create-dev-certificate.ps1'
$ChecksumScript = Join-Path $ProjectDirectory 'packaging\windows\write-checksum.ps1'
$CertificateDirectory = Join-Path $ProjectDirectory 'build\msix-dev-certificate'
$CertificateFile = Join-Path $CertificateDirectory 'BreezeDesk-MSIX-Development.cer'

function Get-Sha256([string]$Path) {
    $Sha256 = [Security.Cryptography.SHA256]::Create()
    $Stream = [IO.File]::OpenRead($Path)
    try {
        return ([BitConverter]::ToString($Sha256.ComputeHash($Stream))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $Stream.Dispose()
        $Sha256.Dispose()
    }
}

function Test-Administrator {
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
    return $Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if ($Install -and -not (Test-Administrator)) {
    throw 'The -Install option requires an elevated PowerShell window. Prepare the package without -Install, then rerun with -ReuseStorePackage -Install as administrator.'
}

if (-not $ReuseStorePackage) {
    & $StorePackagingScript
}

$StoreMsix = Get-ChildItem -LiteralPath (Join-Path $ProjectDirectory 'dist') -File `
    -Filter '*-Windows-x64.msix' -ErrorAction SilentlyContinue |
    Where-Object { $_.BaseName -notlike '*-Development' } |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if (-not $StoreMsix) {
    throw 'No Store MSIX was found. Run without -ReuseStorePackage to build it first.'
}

$StoreChecksumFile = "$($StoreMsix.FullName).sha256"
if (-not (Test-Path -LiteralPath $StoreChecksumFile -PathType Leaf)) {
    throw "The Store MSIX checksum is missing: $StoreChecksumFile. Rebuild the Store package before creating a development copy."
}
$ExpectedStoreDigest = ((Get-Content -LiteralPath $StoreChecksumFile -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
$StoreDigestBefore = Get-Sha256 $StoreMsix.FullName
if ($ExpectedStoreDigest -ne $StoreDigestBefore) {
    throw 'The Store MSIX no longer matches its checksum. Rebuild it with scripts/package-windows.ps1 before creating a development copy.'
}

$DevelopmentMsix = Join-Path $StoreMsix.DirectoryName "$($StoreMsix.BaseName)-Development.msix"
Copy-Item -LiteralPath $StoreMsix.FullName -Destination $DevelopmentMsix -Force

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $SigningScript `
    -MsixPath $DevelopmentMsix -OutputDirectory $CertificateDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Development MSIX signing failed with exit code $LASTEXITCODE."
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ChecksumScript -Artifact $DevelopmentMsix
if ($LASTEXITCODE -ne 0) {
    throw "Development MSIX checksum generation failed with exit code $LASTEXITCODE."
}

$StoreDigestAfter = Get-Sha256 $StoreMsix.FullName
if ($StoreDigestAfter -ne $StoreDigestBefore) {
    throw 'The Store MSIX changed while preparing the development package.'
}

if ($Install) {
    $CertificateLiteral = $CertificateFile.Replace("'", "''")
    $MsixLiteral = $DevelopmentMsix.Replace("'", "''")
    $InstallCommand = @"
`$ErrorActionPreference = 'Stop'
Import-Certificate -FilePath '$CertificateLiteral' -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null
Add-AppxPackage -Path '$MsixLiteral' -ForceApplicationShutdown -ForceUpdateFromAnyVersion
"@
    $EncodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($InstallCommand))
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand $EncodedCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Development MSIX installation failed with exit code $LASTEXITCODE."
    }
}

Write-Host ''
Write-Host "Store MSIX unchanged: $($StoreMsix.FullName)"
Write-Host "Development MSIX: $DevelopmentMsix"
Write-Host "Development checksum: $DevelopmentMsix.sha256"
Write-Host "Development certificate: $CertificateFile"
if ($Install) {
    Write-Host 'Development MSIX installed for the current user.'
}
else {
    Write-Host 'To trust and install it, rerun this script from elevated PowerShell with -ReuseStorePackage -Install.'
}
