@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "BUILD_DIR=%ROOT_DIR%\build"
set "GENERATOR=Visual Studio 17 2022"
set "CONFIG=Debug"
set "TARGET=PrismCanvasDemo"
set "NO_RUN=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--no-run" (
    set "NO_RUN=1"
) else if /I "%~1"=="/no-run" (
    set "NO_RUN=1"
) else if /I "%~1"=="--release" (
    set "CONFIG=Release"
) else if /I "%~1"=="--debug" (
    set "CONFIG=Debug"
) else (
    echo Unknown argument: %~1
    echo Usage: build.bat [--no-run] [--debug^|--release]
    exit /b 1
)
shift
goto parse_args

:args_done
set "EXE_PATH=%BUILD_DIR%\%CONFIG%\%TARGET%.exe"

call :get_tick BUILD_START_MS

echo BUILD_TARGET=%TARGET%
echo BUILD_CONFIG=%CONFIG%

where cmake >nul 2>&1
if errorlevel 1 (
    echo CMake was not found in PATH.
    echo BUILD_RESULT=FAIL
    echo BUILD_FAILED_STAGE=PRECHECK
    exit /b 1
)

if exist "%ROOT_DIR%\.git" (
    echo [0/3] Updating submodules...
    git -C "%ROOT_DIR%" submodule update --init --recursive
    if errorlevel 1 (
        echo Submodule update failed.
        echo BUILD_RESULT=FAIL
        echo BUILD_FAILED_STAGE=SUBMODULES
        exit /b 1
    )
)

echo [1/3] Configuring...
call :get_tick STEP_START_MS
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" -T host=x64 -A x64 -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
set "STEP_EXIT=%ERRORLEVEL%"
call :elapsed_ms STEP_START_MS CONFIGURE_MS
echo BUILD_CONFIGURE_MS=!CONFIGURE_MS!
if not "!STEP_EXIT!"=="0" (
    echo Configure failed.
    echo BUILD_RESULT=FAIL
    echo BUILD_FAILED_STAGE=CONFIGURE
    exit /b 1
)

echo [2/3] Building...
call :get_tick STEP_START_MS
cmake --build "%BUILD_DIR%" --config %CONFIG% --target %TARGET%
set "STEP_EXIT=%ERRORLEVEL%"
call :elapsed_ms STEP_START_MS COMPILE_MS
echo BUILD_COMPILE_MS=!COMPILE_MS!
if not "!STEP_EXIT!"=="0" (
    echo Build failed.
    echo BUILD_RESULT=FAIL
    echo BUILD_FAILED_STAGE=BUILD
    exit /b 1
)

if not exist "%EXE_PATH%" (
    echo Executable not found: "%EXE_PATH%"
    echo BUILD_RESULT=FAIL
    echo BUILD_FAILED_STAGE=OUTPUT
    exit /b 1
)

if "%NO_RUN%"=="1" (
    echo [3/3] Skipping run.
    echo BUILD_RUN_MS=0
    call :elapsed_ms BUILD_START_MS TOTAL_MS
    echo BUILD_TOTAL_MS=!TOTAL_MS!
    echo BUILD_RESULT=PASS
    exit /b 0
)

echo [3/3] Running...
call :get_tick STEP_START_MS
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%EXE_PATH%'"
set "STEP_EXIT=%ERRORLEVEL%"
call :elapsed_ms STEP_START_MS RUN_MS
echo BUILD_RUN_MS=!RUN_MS!
if not "!STEP_EXIT!"=="0" (
    echo Run failed.
    echo BUILD_RESULT=FAIL
    echo BUILD_FAILED_STAGE=RUN
    exit /b 1
)

call :elapsed_ms BUILD_START_MS TOTAL_MS
echo BUILD_TOTAL_MS=!TOTAL_MS!
echo BUILD_RESULT=PASS
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