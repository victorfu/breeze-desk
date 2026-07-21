$ErrorActionPreference = 'Stop'

foreach ($RequiredFile in @($env:GITHUB_ENV, $env:GITHUB_PATH)) {
    if ([string]::IsNullOrWhiteSpace($RequiredFile)) {
        throw 'This script must run inside GitHub Actions with GITHUB_ENV and GITHUB_PATH available.'
    }
}

$Before = @{}
Get-ChildItem Env: | ForEach-Object { $Before[$_.Name] = [string]$_.Value }
$BeforePath = [string]$env:Path

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $VsWhere)) {
    throw "vswhere.exe was not found at '$VsWhere'."
}

$VsWhereOutput = @(& $VsWhere -latest -products '*' -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath)
$VsWhereExitCode = $LASTEXITCODE
$InstallationPath = $VsWhereOutput | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Select-Object -First 1
if ($VsWhereExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($InstallationPath)) {
    throw 'Visual Studio 2022 with the x64 C++ toolset was not found.'
}

$InstallationPath = ([string]$InstallationPath).Trim()
$DevShell = Join-Path $InstallationPath 'Common7\Tools\Launch-VsDevShell.ps1'
if (-not (Test-Path -LiteralPath $DevShell)) {
    throw "Visual Studio Developer Shell was not found at '$DevShell'."
}

& $DevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if ([string]::IsNullOrWhiteSpace($env:VSCMD_VER) -or
    -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Visual Studio Developer Shell did not configure the x64 MSVC toolchain.'
}

$Utf8NoBom = [Text.UTF8Encoding]::new($false)
function Add-GitHubEnvironmentVariable {
    param(
        [string]$Name,
        [AllowEmptyString()][string]$Value
    )

    if ($Value.Contains("`r") -or $Value.Contains("`n")) {
        do {
            $Delimiter = "breezedesk_$([Guid]::NewGuid().ToString('N'))"
        } while (($Value -split '\r?\n') -contains $Delimiter)
        [IO.File]::AppendAllText($env:GITHUB_ENV,
                                 "$Name<<$Delimiter`n$Value`n$Delimiter`n", $Utf8NoBom)
        return
    }
    [IO.File]::AppendAllText($env:GITHUB_ENV, "$Name=$Value`n", $Utf8NoBom)
}

foreach ($Entry in (Get-ChildItem Env:)) {
    $Name = [string]$Entry.Name
    $Value = [string]$Entry.Value
    if ($Name -ieq 'Path' -or $Name -match '^(GITHUB_|RUNNER_)' -or $Name -ieq 'NODE_OPTIONS') {
        continue
    }
    if ($Before.ContainsKey($Name) -and
        [string]::Equals([string]$Before[$Name], $Value, [StringComparison]::Ordinal)) {
        continue
    }
    Add-GitHubEnvironmentVariable -Name $Name -Value $Value
}

function Get-PathKey {
    param([string]$Value)

    $Key = $Value.Trim()
    if ($Key.Length -gt 3) {
        $Key = $Key.TrimEnd([char[]]@('\', '/'))
    }
    return $Key
}

$KnownPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($Entry in ($BeforePath -split ';')) {
    $Key = Get-PathKey $Entry
    if ($Key) {
        [void]$KnownPaths.Add($Key)
    }
}

$AddedPaths = [Collections.Generic.List[string]]::new()
foreach ($Entry in ([string]$env:Path -split ';')) {
    $PathEntry = $Entry.Trim()
    if (-not $PathEntry) {
        continue
    }
    if ($KnownPaths.Add((Get-PathKey $PathEntry))) {
        $AddedPaths.Add($PathEntry)
    }
}

# GitHub prepends each GITHUB_PATH entry, so write in reverse to preserve DevShell's tool order.
for ($Index = $AddedPaths.Count - 1; $Index -ge 0; $Index--) {
    [IO.File]::AppendAllText($env:GITHUB_PATH, "$($AddedPaths[$Index])`n", $Utf8NoBom)
}

Write-Host "Exported the Visual Studio $env:VisualStudioVersion x64 developer environment."
