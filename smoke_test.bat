@echo off
setlocal

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "BUILD_DIR=%ROOT_DIR%\build"
set "LOG_PATH=%BUILD_DIR%\smoke_test.log"
set "EXE_PATH=%BUILD_DIR%\Debug\PrismCanvasDemo.exe"
set "EXPECTED_HASH=%~1"

call "%ROOT_DIR%\build.bat" --no-run
if errorlevel 1 exit /b 1

if not exist "%EXE_PATH%" (
    echo Executable not found: "%EXE_PATH%"
    exit /b 1
)

set "CPPDEMO_PRINT_PIXEL_HASH=1"
set "CPPDEMO_EXIT_AFTER_FIRST_FRAME=1"
set "CPPDEMO_FIXED_TIME_SECONDS=1.25"
set "CPPDEMO_DISABLE_MSAA=1"
if not "%EXPECTED_HASH%"=="" set "CPPDEMO_EXPECT_PIXEL_HASH=%EXPECTED_HASH%"
"%EXE_PATH%" > "%LOG_PATH%" 2>&1
set "RUN_EXIT=%ERRORLEVEL%"
type "%LOG_PATH%"
if not "%RUN_EXIT%"=="0" exit /b %RUN_EXIT%

findstr /C:"PIXEL_HASH_RGBA=" "%LOG_PATH%" >nul
if errorlevel 1 (
    echo Pixel hash output missing.
    exit /b 1
)

findstr /I /C:"Pixel hash mismatch" /C:"Pixel hash expectation invalid" /C:"Pixel readback failed" /C:"PPM capture failed" /C:"Fixed time invalid" /C:"SHADER::COMPILATION_FAILED" /C:"PROGRAM_LINKING_ERROR" "%LOG_PATH%" >nul
if not errorlevel 1 (
    echo Smoke test found a rendering failure marker.
    exit /b 1
)

echo SMOKE_TEST=PASS
exit /b 0
