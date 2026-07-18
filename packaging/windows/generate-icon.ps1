param(
    [Parameter(Mandatory = $true)][string]$OutputFile
)

$ErrorActionPreference = "Stop"
$ProjectDirectory = (Resolve-Path "$PSScriptRoot\..\..").Path
$Magick = (Get-Command "magick.exe" -ErrorAction SilentlyContinue).Source
if (-not $Magick) {
    throw "ImageMagick (magick.exe) is required to generate the Windows icon."
}

$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$OutputParent = Split-Path -Parent $OutputFile
New-Item -ItemType Directory -Force $OutputParent | Out-Null

$TempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$FrameDirectory = Join-Path $TempRoot ("breezedesk-icon-" + [Guid]::NewGuid().ToString("N"))
$ResolvedFrameDirectory = [IO.Path]::GetFullPath($FrameDirectory)
if (-not $ResolvedFrameDirectory.StartsWith($TempRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create icon frames outside the temporary directory."
}

$Symbol = Join-Path $ProjectDirectory "resources\icons\breezedesk-symbol.svg"
$Tray = Join-Path $ProjectDirectory "resources\icons\breezedesk-tray.svg"
$Sizes = 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128, 256
$FramePaths = @()

try {
    New-Item -ItemType Directory -Force $ResolvedFrameDirectory | Out-Null
    foreach ($Size in $Sizes) {
        $Source = if ($Size -le 28) { $Tray } else { $Symbol }
        $FramePath = Join-Path $ResolvedFrameDirectory (("{0:D3}" -f $Size) + ".png")
        & $Magick -background transparent -density 384 $Source -resize "${Size}x${Size}" $FramePath
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $FramePath)) {
            throw "ImageMagick could not render the ${Size}px Windows icon frame."
        }
        $FramePaths += $FramePath
    }

    & $Magick @FramePaths $OutputFile
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $OutputFile)) {
        throw "ImageMagick could not create the multi-resolution Windows icon."
    }
} finally {
    if (Test-Path $ResolvedFrameDirectory) {
        Remove-Item -LiteralPath $ResolvedFrameDirectory -Recurse -Force
    }
}

Write-Output $OutputFile
