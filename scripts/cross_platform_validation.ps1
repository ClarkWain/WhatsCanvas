$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build-validation"

cmake -S $root -B $buildDir -DCMAKE_BUILD_TYPE=Debug
cmake --build $buildDir --config Debug
ctest --test-dir $buildDir -C Debug -L unit --output-on-failure

& (Join-Path $PSScriptRoot "opengles_build_smoke.bat")
