param(
    [Parameter(Mandatory = $true)][string]$Artifact
)

$ErrorActionPreference = "Stop"
$Artifact = (Resolve-Path $Artifact).Path
$Digest = (Get-FileHash -Algorithm SHA256 $Artifact).Hash.ToLowerInvariant()
$Line = "$Digest  $([IO.Path]::GetFileName($Artifact))"
[IO.File]::WriteAllText("$Artifact.sha256", $Line + [Environment]::NewLine, [Text.Encoding]::ASCII)
Write-Output "$Artifact.sha256"
