param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
    [string[]]$Paths
)

$ErrorActionPreference = "Stop"
if (-not $env:BREEZEDESK_SIGNTOOL_CERT -and -not $env:BREEZEDESK_SIGNTOOL_SHA1) {
    throw "Set BREEZEDESK_SIGNTOOL_CERT (PFX path) or BREEZEDESK_SIGNTOOL_SHA1 (certificate thumbprint)."
}

function Find-SignTool {
    $Command = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }
    $Candidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter "signtool.exe" -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending
    if ($Candidates.Count -eq 0) { throw "signtool.exe was not found in the Windows SDK." }
    return $Candidates[0].FullName
}

$SignTool = Find-SignTool
$TimestampUrl = if ($env:BREEZEDESK_TIMESTAMP_URL) { $env:BREEZEDESK_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }
if (-not $env:BREEZEDESK_RELEASE_EXECUTABLE_NAME -or
    -not $env:BREEZEDESK_CLI_EXECUTABLE_NAME -or
    -not $env:BREEZEDESK_WORKER_EXECUTABLE_NAME) {
    throw "Executable identity variables must be supplied by the CMake-driven packaging script."
}
$ProductExecutable = "$($env:BREEZEDESK_RELEASE_EXECUTABLE_NAME).exe"
$CliExecutable = "$($env:BREEZEDESK_CLI_EXECUTABLE_NAME).exe"
$WorkerExecutable = $env:BREEZEDESK_WORKER_EXECUTABLE_NAME
$CertificateArguments = if ($env:BREEZEDESK_SIGNTOOL_CERT) {
    $Arguments = @('/f', (Resolve-Path $env:BREEZEDESK_SIGNTOOL_CERT).Path)
    if ($null -ne $env:BREEZEDESK_SIGNTOOL_PASSWORD) { $Arguments += @('/p', $env:BREEZEDESK_SIGNTOOL_PASSWORD) }
    $Arguments
} else {
    @('/sha1', $env:BREEZEDESK_SIGNTOOL_SHA1)
}

$Files = foreach ($Path in $Paths) {
    $Resolved = Get-Item -LiteralPath $Path
    if ($Resolved.PSIsContainer) {
        Get-ChildItem -LiteralPath $Resolved.FullName -Recurse -File | Where-Object {
            $_.Name -in @($ProductExecutable, $CliExecutable, 'ffmpeg.exe', 'ffprobe.exe') -or
            $_.Name -like "$WorkerExecutable*.exe"
        }
    } else {
        $Resolved
    }
}
$Files = @($Files | Sort-Object FullName -Unique)
if ($Files.Count -eq 0) { throw "No signable artifacts were found." }

foreach ($File in $Files) {
    & $SignTool sign /fd SHA256 /td SHA256 /tr $TimestampUrl @CertificateArguments $File.FullName
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for $($File.FullName)." }
    & $SignTool verify /pa $File.FullName
    if ($LASTEXITCODE -ne 0) { throw "signature verification failed for $($File.FullName)." }
}
