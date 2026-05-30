@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "BUILD_DIR=%ROOT_DIR%\build"
set "LOG_PATH=%BUILD_DIR%\smoke_test.log"
set "EXE_PATH=%BUILD_DIR%\Debug\PrismCanvasDemo.exe"
set "EXPECTED_HASH=%~1"

call :get_tick SMOKE_START_MS

call :get_tick STEP_START_MS
call "%ROOT_DIR%\build.bat" --no-run
set "BUILD_EXIT=%ERRORLEVEL%"
call :elapsed_ms STEP_START_MS BUILD_MS
echo SMOKE_BUILD_MS=!BUILD_MS!
if not "!BUILD_EXIT!"=="0" (
    echo SMOKE_RESULT=FAIL
    echo SMOKE_FAILED_STAGE=BUILD
    exit /b 1
)

if not exist "%EXE_PATH%" (
    echo Executable not found: "%EXE_PATH%"
    echo SMOKE_RESULT=FAIL
    echo SMOKE_FAILED_STAGE=OUTPUT
    exit /b 1
)

set "CPPDEMO_PRINT_PIXEL_HASH=1"
set "CPPDEMO_EXIT_AFTER_FIRST_FRAME=1"
set "CPPDEMO_FIXED_TIME_SECONDS=1.25"
set "CPPDEMO_DISABLE_MSAA=1"
if not "%EXPECTED_HASH%"=="" set "CPPDEMO_EXPECT_PIXEL_HASH=%EXPECTED_HASH%"
call :get_tick STEP_START_MS
"%EXE_PATH%" > "%LOG_PATH%" 2>&1
set "RUN_EXIT=%ERRORLEVEL%"
call :elapsed_ms STEP_START_MS RUN_MS
echo SMOKE_RUN_MS=!RUN_MS!
type "%LOG_PATH%"
if not "!RUN_EXIT!"=="0" (
    echo SMOKE_RESULT=FAIL
    echo SMOKE_FAILED_STAGE=RUN
    exit /b !RUN_EXIT!
)

findstr /C:"PIXEL_HASH_RGBA=" "%LOG_PATH%" >nul
if errorlevel 1 (
    echo Pixel hash output missing.
    echo SMOKE_RESULT=FAIL
    echo SMOKE_FAILED_STAGE=HASH_OUTPUT
    exit /b 1
)

findstr /I /C:"Pixel hash mismatch" /C:"Pixel hash expectation invalid" /C:"Pixel readback failed" /C:"PPM capture failed" /C:"Fixed time invalid" /C:"SHADER::COMPILATION_FAILED" /C:"PROGRAM_LINKING_ERROR" "%LOG_PATH%" >nul
if not errorlevel 1 (
    echo Smoke test found a rendering failure marker.
    echo SMOKE_RESULT=FAIL
    echo SMOKE_FAILED_STAGE=MARKER_SCAN
    exit /b 1
)

call :elapsed_ms SMOKE_START_MS TOTAL_MS
echo SMOKE_TOTAL_MS=!TOTAL_MS!
echo SMOKE_TEST=PASS
echo SMOKE_RESULT=PASS
exit /b 0

:get_tick
set "_time=!time: =0!"
set /a "_hours=1!_time:~0,2!-100"
set /a "_minutes=1!_time:~3,2!-100"
set /a "_seconds=1!_time:~6,2!-100"
set /a "_centis=1!_time:~9,2!-100"
set /a "%~1=(((_hours*60)+_minutes)*60+_seconds)*100+_centis"
exit /b 0

:elapsed_ms
set "_start_cs=!%~1!"
call :get_tick NOW_CS
set /a "_elapsed_cs=NOW_CS-_start_cs"
if !_elapsed_cs! lss 0 set /a "_elapsed_cs+=24*60*60*100"
set /a "%~2=_elapsed_cs*10"
exit /b 0
