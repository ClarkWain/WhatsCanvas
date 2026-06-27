@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT_DIR=%%~fI"
set "BUILD_DIR=%ROOT_DIR%\build"
set "EXE_PATH=%BUILD_DIR%\Debug\WhatsCanvasDemo.exe"
set "SCENES=text-heavy image-heavy gradient-effect clipping transform save-layer"

call :get_tick VALIDATION_SMOKE_START_MS

call "%ROOT_DIR%\build.bat" --no-run
set "BUILD_EXIT=%ERRORLEVEL%"
if not "%BUILD_EXIT%"=="0" (
    echo VALIDATION_SCENE_SMOKE_RESULT=FAIL
    echo VALIDATION_SCENE_SMOKE_FAILED_STAGE=BUILD
    exit /b 1
)

if not exist "%EXE_PATH%" (
    echo Executable not found: "%EXE_PATH%"
    echo VALIDATION_SCENE_SMOKE_RESULT=FAIL
    echo VALIDATION_SCENE_SMOKE_FAILED_STAGE=OUTPUT
    exit /b 1
)

for %%S in (%SCENES%) do (
    call :run_scene "%%S"
    if not "!ERRORLEVEL!"=="0" (
        call :elapsed_ms VALIDATION_SMOKE_START_MS TOTAL_MS
        echo VALIDATION_SCENE_SMOKE_TOTAL_MS=!TOTAL_MS!
        echo VALIDATION_SCENE_SMOKE_RESULT=FAIL
        exit /b !ERRORLEVEL!
    )
)

call :elapsed_ms VALIDATION_SMOKE_START_MS TOTAL_MS
echo VALIDATION_SCENE_SMOKE_TOTAL_MS=!TOTAL_MS!
echo VALIDATION_SCENE_SMOKE_TEST=PASS
echo VALIDATION_SCENE_SMOKE_RESULT=PASS
exit /b 0

:run_scene
set "SCENE=%~1"
set "LOG_PATH=%BUILD_DIR%\validation_scene_%SCENE%.log"
set "WHATSCANVAS_VALIDATION_SCENE=%SCENE%"
set "WHATSCANVAS_PRINT_PIXEL_HASH=1"
set "WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1"
set "WHATSCANVAS_FIXED_TIME_SECONDS=1.25"
set "WHATSCANVAS_DISABLE_MSAA=1"

echo VALIDATION_SCENE=%SCENE%
"%EXE_PATH%" > "%LOG_PATH%" 2>&1
set "RUN_EXIT=%ERRORLEVEL%"
type "%LOG_PATH%"
if not "%RUN_EXIT%"=="0" (
    echo VALIDATION_SCENE_SMOKE_FAILED_STAGE=RUN
    echo VALIDATION_SCENE_SMOKE_FAILED_SCENE=%SCENE%
    exit /b %RUN_EXIT%
)

findstr /C:"PIXEL_HASH_RGBA=" "%LOG_PATH%" >nul
if errorlevel 1 (
    echo Pixel hash output missing.
    echo VALIDATION_SCENE_SMOKE_FAILED_STAGE=HASH_OUTPUT
    echo VALIDATION_SCENE_SMOKE_FAILED_SCENE=%SCENE%
    exit /b 1
)

findstr /I /C:"Pixel hash mismatch" /C:"Pixel hash expectation invalid" /C:"Pixel readback failed" /C:"PPM capture failed" /C:"Fixed time invalid" /C:"SHADER::COMPILATION_FAILED" /C:"PROGRAM_LINKING_ERROR" "%LOG_PATH%" >nul
if not errorlevel 1 (
    echo Validation scene smoke found a rendering failure marker.
    echo VALIDATION_SCENE_SMOKE_FAILED_STAGE=MARKER_SCAN
    echo VALIDATION_SCENE_SMOKE_FAILED_SCENE=%SCENE%
    exit /b 1
)
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
