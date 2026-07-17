param(
    [Parameter(Mandatory = $true)][string]$StageDirectory,
    [Parameter(Mandatory = $true)][string]$OutputFile,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$ProductName,
    [Parameter(Mandatory = $true)][string]$ExecutableName,
    [Parameter(Mandatory = $true)][string]$ProductId
)

$ErrorActionPreference = "Stop"
$ProjectDirectory = (Resolve-Path "$PSScriptRoot\..\..").Path
$StageDirectory = (Resolve-Path $StageDirectory).Path
$VersionParts = $Version.Split('.')
if ($VersionParts.Count -ne 3 -or ($VersionParts | Where-Object { $_ -notmatch '^\d+$' })) {
    throw "Version '$Version' is not a three-part numeric CMake project version."
}
if ($VersionParts | Where-Object { [int64]$_ -gt 65535 }) {
    throw "Each MSIX version component must be between 0 and 65535."
}
if ($ProductId -notmatch '^[A-Za-z][A-Za-z0-9.]{0,63}$') {
    throw "ProductId must begin with a letter and contain only letters, numbers, and periods."
}
if (-not (Test-Path (Join-Path $StageDirectory "bin\$ExecutableName.exe"))) {
    throw "The staged executable bin\$ExecutableName.exe does not exist."
}
$PackageVersion = "$Version.0"
$IdentityName = if ($env:BREEZEDESK_MSIX_IDENTITY_NAME) { $env:BREEZEDESK_MSIX_IDENTITY_NAME } else { "VictorFu.$ProductId" }
$Publisher = if ($env:BREEZEDESK_MSIX_PUBLISHER) { $env:BREEZEDESK_MSIX_PUBLISHER } else { "CN=$ProductName Development" }
$PublisherDisplayName = if ($env:BREEZEDESK_MSIX_PUBLISHER_DISPLAY_NAME) { $env:BREEZEDESK_MSIX_PUBLISHER_DISPLAY_NAME } else { $ProductName }

function Escape-XmlAttribute([string]$Value) {
    return [Security.SecurityElement]::Escape($Value)
}

function Find-WindowsSdkTool([string]$ToolName) {
    $Command = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($Command) { return $Command.Source }
    $Candidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter $ToolName -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending
    if ($Candidates.Count -eq 0) { throw "$ToolName was not found in the Windows SDK." }
    return $Candidates[0].FullName
}

$MakeAppx = Find-WindowsSdkTool "makeappx.exe"
$Magick = (Get-Command "magick.exe" -ErrorAction SilentlyContinue).Source
if (-not $Magick) {
    throw "ImageMagick (magick.exe) is required to rasterize the repository SVG for MSIX assets."
}

$WorkDirectory = Join-Path $ProjectDirectory "build\package-msix"
$LayoutDirectory = Join-Path $WorkDirectory "layout"
if (Test-Path $WorkDirectory) { Remove-Item -Recurse -Force $WorkDirectory }
New-Item -ItemType Directory -Force $LayoutDirectory, (Join-Path $LayoutDirectory "Assets") | Out-Null
Copy-Item "$StageDirectory\*" $LayoutDirectory -Recurse -Force

$Manifest = Get-Content "$PSScriptRoot\AppxManifest.xml.in" -Raw
$Manifest = $Manifest.Replace('@IDENTITY_NAME@', (Escape-XmlAttribute $IdentityName))
$Manifest = $Manifest.Replace('@PUBLISHER@', (Escape-XmlAttribute $Publisher))
$Manifest = $Manifest.Replace('@PUBLISHER_DISPLAY_NAME@', (Escape-XmlAttribute $PublisherDisplayName))
$Manifest = $Manifest.Replace('@VERSION@', $PackageVersion)
$Manifest = $Manifest.Replace('@PRODUCT_NAME@', (Escape-XmlAttribute $ProductName))
$Manifest = $Manifest.Replace('@EXECUTABLE_NAME@', (Escape-XmlAttribute $ExecutableName))
$Manifest = $Manifest.Replace('@PRODUCT_ID@', (Escape-XmlAttribute $ProductId))
$Manifest | Set-Content (Join-Path $LayoutDirectory "AppxManifest.xml") -Encoding utf8

$Icon = Join-Path $ProjectDirectory "resources\icons\breezedesk.png"
$Assets = Join-Path $LayoutDirectory "Assets"
& $Magick -background transparent $Icon -resize 44x44 (Join-Path $Assets "Square44x44Logo.png")
if ($LASTEXITCODE -ne 0) { throw "ImageMagick could not create Square44x44Logo.png." }
& $Magick -background transparent $Icon -resize 150x150 (Join-Path $Assets "Square150x150Logo.png")
if ($LASTEXITCODE -ne 0) { throw "ImageMagick could not create Square150x150Logo.png." }
& $Magick -background transparent $Icon -resize 50x50 (Join-Path $Assets "StoreLogo.png")
if ($LASTEXITCODE -ne 0) { throw "ImageMagick could not create StoreLogo.png." }
& $Magick -background transparent -size 310x150 -gravity center $Icon -resize 140x140 -extent 310x150 (Join-Path $Assets "Wide310x150Logo.png")
if ($LASTEXITCODE -ne 0) { throw "ImageMagick could not create Wide310x150Logo.png." }

$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$OutputParent = Split-Path -Parent $OutputFile
New-Item -ItemType Directory -Force $OutputParent | Out-Null
if (Test-Path $OutputFile) { Remove-Item -Force $OutputFile }
& $MakeAppx pack /d $LayoutDirectory /p $OutputFile /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed to create $OutputFile." }
Write-Output (Resolve-Path $OutputFile).Path
