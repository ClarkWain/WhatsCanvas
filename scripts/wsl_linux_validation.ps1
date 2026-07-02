param(
    [string]$Distribution = "",
    [string]$BuildDir = "build-wsl",
    [switch]$EnableOpenTypeShaping,
    [switch]$RunOpenGLESSmoke
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $scriptDir

function Convert-ToWslPath([string]$Path) {
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if ($resolved -match "^([A-Za-z]):\\(.*)$") {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2] -replace "\\", "/"
        return "/mnt/$drive/$rest"
    }
    throw "Only Windows drive paths are supported for WSL validation: $resolved"
}

$wslRoot = Convert-ToWslPath $root
$configureArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=Debug"
)

if ($EnableOpenTypeShaping) {
    $configureArgs += "-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON"
}

$commands = @(
    "set -euo pipefail",
    "cd '$wslRoot'",
    "cmake $($configureArgs -join ' ')",
    "cmake --build '$BuildDir' --config Debug",
    "ctest --test-dir '$BuildDir' -C Debug -L unit --output-on-failure"
)

if ($RunOpenGLESSmoke) {
    $commands += "chmod +x scripts/opengles_build_smoke.sh"
    $commands += "WHATSCANVAS_GLES_BUILD_DIR='$wslRoot/build-wsl-gles-check' ./scripts/opengles_build_smoke.sh"
}

$command = $commands -join "; "
if ([string]::IsNullOrWhiteSpace($Distribution)) {
    wsl bash -lc $command
} else {
    wsl -d $Distribution bash -lc $command
}

if ($LASTEXITCODE -ne 0) {
    throw "WSL Linux validation failed with exit code $LASTEXITCODE"
}

Write-Host "WSL_LINUX_VALIDATION_RESULT=PASS"
