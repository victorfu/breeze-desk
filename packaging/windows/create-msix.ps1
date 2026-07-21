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
$MakePri = Find-WindowsSdkTool "makepri.exe"
$Magick = (Get-Command "magick.exe" -ErrorAction SilentlyContinue).Source
if (-not $Magick) {
    throw "ImageMagick (magick.exe) is required to resize the repository PNG icons for MSIX assets."
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

$AppIcon = Join-Path $ProjectDirectory "resources\icons\breezedesk.png"
$SmallIcon = Join-Path $ProjectDirectory "resources\icons\breezedesk-tray.png"
$UnplatedIcon = Join-Path $ProjectDirectory "resources\icons\breezedesk-unplated.png"
$LightUnplatedIcon = Join-Path $ProjectDirectory "resources\icons\breezedesk-light-unplated.png"
$Assets = Join-Path $LayoutDirectory "Assets"

function New-SquareAsset([string]$Source, [int]$Size, [string]$Name) {
    & $Magick -background transparent $Source -filter Lanczos -resize "${Size}x${Size}" (Join-Path $Assets $Name)
    if ($LASTEXITCODE -ne 0) { throw "ImageMagick could not create $Name." }
}

function New-WideAsset([int]$Width, [int]$Height, [int]$IconSize, [string]$Name) {
    & $Magick $AppIcon -background transparent -filter Lanczos -resize "${IconSize}x${IconSize}" `
        -gravity center -extent "${Width}x${Height}" (Join-Path $Assets $Name)
    if ($LASTEXITCODE -ne 0) { throw "ImageMagick could not create $Name." }
}

New-SquareAsset $AppIcon 44 "Square44x44Logo.png"
foreach ($ScaleAsset in @(
    @(125, 55), @(150, 66), @(200, 88), @(400, 176)
)) {
    New-SquareAsset $AppIcon $ScaleAsset[1] "Square44x44Logo.scale-$($ScaleAsset[0]).png"
}

foreach ($TargetSize in 16, 20, 24, 28, 30, 32, 36, 40, 44, 48, 56, 60, 64, 72, 80, 96, 256) {
    $TargetSource = if ($TargetSize -le 48) { $SmallIcon } else { $AppIcon }
    foreach ($Variant in @(
        @("", $TargetSource),
        @("_altform-unplated", $UnplatedIcon),
        @("_altform-lightunplated", $LightUnplatedIcon)
    )) {
        New-SquareAsset $Variant[1] $TargetSize "Square44x44Logo.targetsize-$TargetSize$($Variant[0]).png"
    }
}

New-SquareAsset $AppIcon 50 "StoreLogo.png"
foreach ($ScaleAsset in @(
    @(125, 63), @(150, 75), @(200, 100), @(400, 200)
)) {
    New-SquareAsset $AppIcon $ScaleAsset[1] "StoreLogo.scale-$($ScaleAsset[0]).png"
}

New-SquareAsset $AppIcon 150 "Square150x150Logo.png"
foreach ($ScaleAsset in @(
    @(125, 188), @(150, 225), @(200, 300), @(400, 600)
)) {
    New-SquareAsset $AppIcon $ScaleAsset[1] "Square150x150Logo.scale-$($ScaleAsset[0]).png"
}

New-WideAsset 310 150 140 "Wide310x150Logo.png"
foreach ($ScaleAsset in @(
    @(125, 388, 188, 175),
    @(150, 465, 225, 210),
    @(200, 620, 300, 280),
    @(400, 1240, 600, 560)
)) {
    New-WideAsset $ScaleAsset[1] $ScaleAsset[2] $ScaleAsset[3] `
        "Wide310x150Logo.scale-$($ScaleAsset[0]).png"
}

$PriConfig = Join-Path $WorkDirectory "priconfig.xml"
& $MakePri createconfig /cf $PriConfig /dq en-US /o
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $PriConfig)) {
    throw "MakePri could not create the resource-index configuration."
}
$PriFile = Join-Path $LayoutDirectory "resources.pri"
& $MakePri new /pr $LayoutDirectory /cf $PriConfig `
    /mn (Join-Path $LayoutDirectory "AppxManifest.xml") /of $PriFile /o
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $PriFile)) {
    throw "MakePri could not index the qualified MSIX icon assets."
}

$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$OutputParent = Split-Path -Parent $OutputFile
New-Item -ItemType Directory -Force $OutputParent | Out-Null
if (Test-Path $OutputFile) { Remove-Item -Force $OutputFile }
& $MakeAppx pack /d $LayoutDirectory /p $OutputFile /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed to create $OutputFile." }
Write-Output (Resolve-Path $OutputFile).Path
