param(
    [string]$BuildDir = "build",
    [string]$OutputDir = "build/performance-results",
    [ValidateSet("quick", "standard", "thorough")]
    [string]$Profile = "standard",
    [string[]]$Backends = @("software"),
    [string[]]$Scenes = @("all")
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $root $BuildDir
$outputPath = Join-Path $root $OutputDir
$candidates = @(
    (Join-Path $buildPath "Release/WhatsCanvasPerformanceSuite.exe"),
    (Join-Path $buildPath "WhatsCanvasPerformanceSuite.exe")
)
$executable = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $executable) {
    throw "WhatsCanvasPerformanceSuite Release executable was not found in $buildPath"
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$env:WHATSCANVAS_PERF_COMMIT = (
    git -C $root rev-parse --verify HEAD 2>$null
)

if ($Scenes -contains "all") {
    $sceneList = @(
        & $executable --list-scenes |
            ForEach-Object { ($_ -split "`t")[0] }
    )
} else {
    $sceneList = $Scenes
}

$writtenResults = @()
foreach ($backend in $Backends) {
    foreach ($scene in $sceneList) {
        $result = Join-Path $outputPath "$backend-$scene.jsonl"
        Write-Host "Running $backend/$scene ($Profile)..."
        & $executable `
            --backend $backend `
            --profile $Profile `
            --scene $scene `
            --output $result
        if ($LASTEXITCODE -ne 0) {
            throw "Performance suite failed for '$backend/$scene'"
        }
        $writtenResults += $result
    }
}

python (Join-Path $PSScriptRoot "compare_performance.py") `
    --validate $writtenResults
if ($LASTEXITCODE -ne 0) {
    throw "Performance result validation failed"
}

Write-Host "Results: $outputPath"
