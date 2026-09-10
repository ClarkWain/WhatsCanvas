param([switch]$CoreOnly)
$ErrorActionPreference = 'Stop'
$buildDirectory = Join-Path $PSScriptRoot $(if ($CoreOnly) { 'build-core' } else { 'build' })
$desktop = if ($CoreOnly) { 'OFF' } else { 'ON' }
& cmake -S $PSScriptRoot -B $buildDirectory "-DCHESS_BUILD_DESKTOP=$desktop" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& ctest --test-dir $buildDirectory -C Release --output-on-failure -V
if ($LASTEXITCODE -ne 0) { throw 'Game validation failed.' }
if (-not $CoreOnly) {
    $executable = Join-Path $buildDirectory 'Release/Chess.exe'
    if (-not (Test-Path -LiteralPath $executable)) { $executable = Join-Path $buildDirectory 'Chess.exe' }
    $capture = Join-Path $buildDirectory 'verification.ppm'
    & $executable --capture $capture
    if ($LASTEXITCODE -ne 0) { throw 'Desktop capture failed.' }
    Write-Output "Verified. Desktop capture: $capture"
}
