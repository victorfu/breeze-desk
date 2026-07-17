param(
    [Parameter(Mandatory = $true)][string]$Artifact
)

$ErrorActionPreference = "Stop"
$Artifact = (Resolve-Path $Artifact).Path
if (-not $env:BREEZEDESK_WINSPARKLE_TOOL) {
    throw "BREEZEDESK_WINSPARKLE_TOOL must name the pinned winsparkle-tool.exe"
}
$Tool = (Resolve-Path $env:BREEZEDESK_WINSPARKLE_TOOL).Path
if (-not $env:BREEZEDESK_WINSPARKLE_PRIVATE_KEY) {
    throw "BREEZEDESK_WINSPARKLE_PRIVATE_KEY is required"
}

$PrivateKeyFile = [IO.Path]::GetTempFileName()
try {
    [IO.File]::WriteAllText($PrivateKeyFile, $env:BREEZEDESK_WINSPARKLE_PRIVATE_KEY, [Text.Encoding]::ASCII)
    $Signature = & $Tool sign --private-key-file $PrivateKeyFile $Artifact 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "winsparkle-tool failed: $Signature" }
    $Match = [regex]::Match($Signature, 'sparkle:edSignature="[^"]+"(?:\s+length="\d+")?')
    if (-not $Match.Success) { throw "winsparkle-tool did not return an EdDSA signature fragment: $Signature" }
    $SignatureFile = "$Artifact.edSignature"
    [IO.File]::WriteAllText($SignatureFile, $Match.Value + [Environment]::NewLine, [Text.Encoding]::ASCII)
    Write-Output $SignatureFile
}
finally {
    if (Test-Path $PrivateKeyFile) { Remove-Item -Force $PrivateKeyFile }
}
