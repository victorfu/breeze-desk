param(
    [Parameter(Mandatory = $true)][string]$Artifact
)

$ErrorActionPreference = "Stop"
$Artifact = (Resolve-Path $Artifact).Path
$Sha256 = [Security.Cryptography.SHA256]::Create()
$Stream = [IO.File]::OpenRead($Artifact)
try {
    $Digest = ([BitConverter]::ToString($Sha256.ComputeHash($Stream))).Replace('-', '').ToLowerInvariant()
}
finally {
    $Stream.Dispose()
    $Sha256.Dispose()
}
$Line = "$Digest  $([IO.Path]::GetFileName($Artifact))"
[IO.File]::WriteAllText("$Artifact.sha256", $Line + [Environment]::NewLine, [Text.Encoding]::ASCII)
Write-Output "$Artifact.sha256"
