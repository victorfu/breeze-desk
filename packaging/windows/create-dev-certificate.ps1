param(
    [Parameter(Mandatory = $true)][string]$MsixPath,
    [string]$Publisher = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$ProjectDirectory = (Resolve-Path "$PSScriptRoot\..\..").Path
$Msix = Get-Item -LiteralPath $MsixPath
if ($Msix.Extension -ne ".msix") {
    throw "MsixPath must name an existing .msix package."
}
if ([string]::IsNullOrWhiteSpace($Publisher)) {
    $Publisher = $env:BREEZEDESK_MSIX_PUBLISHER
}
if ([string]::IsNullOrWhiteSpace($Publisher)) {
    $StoreIdentity = Import-PowerShellDataFile (Join-Path $PSScriptRoot "msix-identity.psd1")
    $Publisher = [string]$StoreIdentity.Publisher
}
if ([string]::IsNullOrWhiteSpace($Publisher)) {
    throw "The MSIX publisher is empty in msix-identity.psd1 and has no parameter or environment override."
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectDirectory "build\msix-dev-certificate"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null

function Find-WindowsSdkTool([string]$ToolName) {
    $Command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }
    $Candidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter $ToolName -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending
    if ($Candidates.Count -eq 0) { throw "$ToolName was not found in the Windows SDK." }
    return $Candidates[0].FullName
}

$FriendlyName = "BreezeDesk MSIX development signing"
$Certificate = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object {
        $_.Subject -eq $Publisher -and
        $_.FriendlyName -eq $FriendlyName -and
        $_.NotAfter -gt (Get-Date).AddDays(1)
    } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $Certificate) {
    $Certificate = New-SelfSignedCertificate `
        -Type Custom `
        -Subject $Publisher `
        -FriendlyName $FriendlyName `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -KeyUsage DigitalSignature `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -NotAfter (Get-Date).AddYears(2) `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
}

$CertificateFile = Join-Path $OutputDirectory "BreezeDesk-MSIX-Development.cer"
Export-Certificate -Cert $Certificate -FilePath $CertificateFile -Force | Out-Null

$SignTool = Find-WindowsSdkTool "signtool.exe"
& $SignTool sign /fd SHA256 /sha1 $Certificate.Thumbprint $Msix.FullName
if ($LASTEXITCODE -ne 0) {
    throw "signtool could not sign the MSIX. Confirm that its manifest Publisher exactly matches '$Publisher'."
}

Write-Output "Signed: $($Msix.FullName)"
Write-Output "Certificate: $CertificateFile"
Write-Output "Before installing the package, run this command from an elevated PowerShell window:"
Write-Output "Import-Certificate -FilePath '$CertificateFile' -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople'"
