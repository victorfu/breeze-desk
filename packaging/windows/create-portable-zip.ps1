param(
    [Parameter(Mandatory = $true)][string]$StageDirectory,
    [Parameter(Mandatory = $true)][string]$OutputFile,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$ProductName,
    [Parameter(Mandatory = $true)][string]$ExecutableName
)

$ErrorActionPreference = "Stop"
$ProjectDirectory = (Resolve-Path "$PSScriptRoot\..\..").Path

if (-not (Test-Path -LiteralPath $StageDirectory -PathType Container)) {
    throw "The stage directory does not exist: $StageDirectory"
}
$StageDirectory = (Resolve-Path -LiteralPath $StageDirectory).Path
if (-not (Test-Path -LiteralPath (Join-Path $StageDirectory "bin\$ExecutableName.exe") -PathType Leaf)) {
    throw "The staged executable bin\$ExecutableName.exe does not exist."
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version '$Version' is not a three-part numeric CMake project version."
}

$OutputFileName = [IO.Path]::GetFileName($OutputFile)
if (-not $OutputFileName.EndsWith('-Windows-x64-portable.zip', [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputFile must end in -Windows-x64-portable.zip."
}

$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$OutputParent = Split-Path -Parent $OutputFile
$ArchiveRootName = [IO.Path]::GetFileNameWithoutExtension($OutputFileName)
$WorkDirectory = Join-Path $ProjectDirectory "build\package-windows-portable"
$LayoutDirectory = Join-Path $WorkDirectory $ArchiveRootName

if (Test-Path -LiteralPath $WorkDirectory) {
    Remove-Item -LiteralPath $WorkDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force $LayoutDirectory, $OutputParent | Out-Null
Copy-Item -Path (Join-Path $StageDirectory '*') -Destination $LayoutDirectory -Recurse -Force

$Readme = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'portable-readme.txt.in') -Raw
$Readme = $Readme.Replace('@PRODUCT_NAME@', $ProductName)
$Readme = $Readme.Replace('@VERSION@', $Version)
$Readme = $Readme.Replace('@EXECUTABLE_NAME@', $ExecutableName)
$Readme | Set-Content -LiteralPath (Join-Path $LayoutDirectory 'README.txt') -Encoding ascii

if (Test-Path -LiteralPath $OutputFile) {
    Remove-Item -LiteralPath $OutputFile -Force
}
Push-Location $WorkDirectory
try {
    & cmake -E tar cf $OutputFile --format=zip $ArchiveRootName
    if ($LASTEXITCODE -ne 0) {
        throw "cmake failed to create $OutputFile."
    }
}
finally {
    Pop-Location
}
if (-not (Test-Path -LiteralPath $OutputFile -PathType Leaf)) {
    throw "The portable ZIP was not created: $OutputFile"
}
Write-Output (Resolve-Path -LiteralPath $OutputFile).Path
