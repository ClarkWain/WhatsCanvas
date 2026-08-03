@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "BUILD_DIR=%ROOT_DIR%\build"
set "GENERATOR=Visual Studio 17 2022"
set "GENERATOR_IS_MULTI_CONFIG=1"
set "GENERATOR_PLATFORM=x64"
set "GENERATOR_TOOLSET=host=x64"
set "CONFIG=Debug"
set "TARGET=WhatsCanvasDemo"
set "NO_RUN=0"
set "PACKAGE=0"
set "BUILD_SHARED=0"
set "BUILD_OPENGLES=0"
set "BUILD_VULKAN=0"

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
) else if /I "%~1"=="--package" (
    set "PACKAGE=1"
) else if /I "%~1"=="--shared" (
    set "BUILD_SHARED=1"
) else if /I "%~1"=="--opengles" (
    set "BUILD_OPENGLES=1"
) else if /I "%~1"=="--vulkan" (
    set "BUILD_VULKAN=1"
) else (
    echo Unknown argument: %~1
    echo Usage: build.bat [--no-run] [--debug^|--release] [--package] [--shared] [--opengles] [--vulkan]
    exit /b 1
)
shift
goto parse_args

:args_done
if defined WHATSCANVAS_BUILD_DIR set "BUILD_DIR=%WHATSCANVAS_BUILD_DIR%"
if defined WHATSCANVAS_CMAKE_GENERATOR set "GENERATOR=%WHATSCANVAS_CMAKE_GENERATOR%"
if defined WHATSCANVAS_CMAKE_MULTI_CONFIG set "GENERATOR_IS_MULTI_CONFIG=%WHATSCANVAS_CMAKE_MULTI_CONFIG%"
if defined WHATSCANVAS_CMAKE_PLATFORM set "GENERATOR_PLATFORM=%WHATSCANVAS_CMAKE_PLATFORM%"
if defined WHATSCANVAS_CMAKE_TOOLSET set "GENERATOR_TOOLSET=%WHATSCANVAS_CMAKE_TOOLSET%"

if /I "%GENERATOR%"=="Visual Studio 17 2022" set "GENERATOR_IS_MULTI_CONFIG=1"
if /I "%GENERATOR%"=="Ninja Multi-Config" set "GENERATOR_IS_MULTI_CONFIG=1"
if /I "%GENERATOR%"=="Ninja" set "GENERATOR_IS_MULTI_CONFIG=0"
if /I "%GENERATOR%"=="NMake Makefiles" set "GENERATOR_IS_MULTI_CONFIG=0"

set "EXE_PATH=%BUILD_DIR%\%CONFIG%\%TARGET%.exe"
if "%GENERATOR_IS_MULTI_CONFIG%"=="0" set "EXE_PATH=%BUILD_DIR%\%TARGET%.exe"
set "PACKAGE_DIR=%ROOT_DIR%\out\package\%CONFIG%"
set "CONFIGURE_ARGS=-S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE"
if "%PACKAGE%"=="1" (
    if /I "%WHATSCANVAS_PACKAGE_ENABLE_FREETYPE%"=="0" (
        set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=OFF"
    ) else (
        set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON"
    )
)
rem Packaging installs the renderer libraries, not the demo, so skip optional
rem executable targets to keep the package build lean and avoid linking
rem internal-only helpers against shared-library exports.
if "%PACKAGE%"=="1" set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DBUILD_TESTING=OFF -DWHATSCANVAS_BUILD_DEMO=OFF -DWHATSCANVAS_BUILD_BENCHMARKS=OFF"
if "%BUILD_SHARED%"=="1" set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DBUILD_SHARED_LIBS=ON"
if "%BUILD_OPENGLES%"=="1" set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DWHATSCANVAS_BUILD_OPENGLES=ON"
if "%BUILD_VULKAN%"=="1" set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DWHATSCANVAS_ENABLE_VULKAN=ON"
if defined WHATSCANVAS_CMAKE_EXTRA_ARGS set "CONFIGURE_ARGS=%CONFIGURE_ARGS% %WHATSCANVAS_CMAKE_EXTRA_ARGS%"
if "%GENERATOR_IS_MULTI_CONFIG%"=="1" (
    if defined GENERATOR_TOOLSET set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -T %GENERATOR_TOOLSET%"
    if defined GENERATOR_PLATFORM set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -A %GENERATOR_PLATFORM%"
) else (
    set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DCMAKE_BUILD_TYPE=%CONFIG%"
)

call :get_tick BUILD_START_MS

echo BUILD_TARGET=%TARGET%
echo BUILD_CONFIG=%CONFIG%
echo BUILD_GENERATOR=%GENERATOR%
echo BUILD_DIR=%BUILD_DIR%

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
call cmake %CONFIGURE_ARGS%
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
if "%PACKAGE%"=="1" (
    rem cmake --install exports every enabled renderer library (OpenGL and the
    rem dependency-free Software target). The demo only links OpenGL, so building
    rem just %TARGET% would leave WhatsCanvasSoftware unbuilt and the install step
    rem would fail. Build the default target set so all installed libraries exist.
    cmake --build "%BUILD_DIR%" --config %CONFIG%
) else (
    cmake --build "%BUILD_DIR%" --config %CONFIG% --target %TARGET%
)
set "STEP_EXIT=%ERRORLEVEL%"
call :elapsed_ms STEP_START_MS COMPILE_MS
echo BUILD_COMPILE_MS=!COMPILE_MS!
if not "!STEP_EXIT!"=="0" (
    echo Build failed.
    echo BUILD_RESULT=FAIL
    echo BUILD_FAILED_STAGE=BUILD
    exit /b 1
)

if not "%PACKAGE%"=="1" (
    if not exist "%EXE_PATH%" if exist "%BUILD_DIR%\%CONFIG%\%TARGET%.exe" set "EXE_PATH=%BUILD_DIR%\%CONFIG%\%TARGET%.exe"

    if not exist "%EXE_PATH%" (
        echo Executable not found: "%EXE_PATH%"
        echo BUILD_RESULT=FAIL
        echo BUILD_FAILED_STAGE=OUTPUT
        exit /b 1
    )
)

if "%PACKAGE%"=="1" (
    echo [3/4] Packaging...
    if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
    call :get_tick STEP_START_MS
    cmake --install "%BUILD_DIR%" --config %CONFIG% --prefix "%PACKAGE_DIR%"
    set "STEP_EXIT=%ERRORLEVEL%"
    call :elapsed_ms STEP_START_MS INSTALL_MS
    echo BUILD_INSTALL_MS=!INSTALL_MS!
    echo BUILD_PACKAGE_DIR=%PACKAGE_DIR%
    if not "!STEP_EXIT!"=="0" (
        echo Package install failed.
        echo BUILD_RESULT=FAIL
        echo BUILD_FAILED_STAGE=INSTALL
        exit /b 1
    )
) else (
    echo BUILD_INSTALL_MS=0
)

if "%NO_RUN%"=="1" (
    if "%PACKAGE%"=="1" (
        echo [4/4] Skipping run.
    ) else (
        echo [3/3] Skipping run.
    )
    echo BUILD_RUN_MS=0
    call :elapsed_ms BUILD_START_MS TOTAL_MS
    echo BUILD_TOTAL_MS=!TOTAL_MS!
    echo BUILD_RESULT=PASS
    exit /b 0
)

if "%PACKAGE%"=="1" (
    echo [4/4] Running...
) else (
    echo [3/3] Running...
)
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
