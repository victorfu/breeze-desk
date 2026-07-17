param(
    [Parameter(Mandatory = $true)][string]$Worker,
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true)][string]$LicenseDirectory
)

$ErrorActionPreference = "Stop"
$Worker = (Resolve-Path $Worker).Path
if (-not $env:CUDA_PATH -or -not (Test-Path $env:CUDA_PATH)) {
    throw "CUDA_PATH must point to the CUDA Toolkit used to build the worker"
}
$Dumpbin = (Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue).Source
if (-not $Dumpbin) { throw "dumpbin.exe is required to inspect CUDA worker dependencies" }
New-Item -ItemType Directory -Force $Destination, $LicenseDirectory | Out-Null

$DependencyOutput = & $Dumpbin /nologo /dependents $Worker 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "dumpbin could not inspect $Worker`n$DependencyOutput" }
$CudaPattern = '^(cublas|cublasLt|cudart|nvrtc|nvrtc-builtins|nvJitLink|cusparse|cusolver|curand|cufft)[A-Za-z0-9_.-]*\.dll$'
$Dependencies = @([regex]::Matches($DependencyOutput, '[A-Za-z0-9_.-]+\.dll', 'IgnoreCase') |
    ForEach-Object { $_.Value } |
    Where-Object { $_ -match $CudaPattern } |
    Sort-Object -Unique)

$Manifest = @("CUDA Toolkit: $env:CUDA_PATH", "Worker: $Worker")
foreach ($Dependency in $Dependencies) {
    $Source = Get-ChildItem $env:CUDA_PATH -Filter $Dependency -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        Select-Object -First 1
    if (-not $Source) { throw "CUDA runtime dependency $Dependency was not found below CUDA_PATH" }
    $Target = Join-Path $Destination $Dependency
    Copy-Item $Source.FullName $Target -Force
    $Hash = (Get-FileHash $Target -Algorithm SHA256).Hash.ToLowerInvariant()
    $Manifest += "$Dependency  $Hash"
}

$Eula = @(
    (Join-Path $env:CUDA_PATH "EULA.txt"),
    (Join-Path $env:CUDA_PATH "CUDA_EULA.txt"),
    (Join-Path $env:CUDA_PATH "doc\EULA.txt")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($Dependencies.Count -gt 0 -and -not $Eula) {
    throw "CUDA runtime DLLs were deployed, but the Toolkit EULA was not found"
}
if ($Eula) { Copy-Item $Eula (Join-Path $LicenseDirectory "NVIDIA-CUDA-EULA.txt") -Force }
$Manifest | Set-Content (Join-Path $LicenseDirectory "NVIDIA-CUDA-RUNTIME-MANIFEST.txt") -Encoding utf8
