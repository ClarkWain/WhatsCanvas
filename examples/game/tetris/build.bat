@echo off
setlocal

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "BUILD_DIR=%ROOT_DIR%\build"
set "GENERATOR=Visual Studio 17 2022"
set "CONFIG=Release"
set "TARGET=Tetris"
set "EXE_PATH=%BUILD_DIR%\%CONFIG%\%TARGET%.exe"
set "NO_RUN=0"

if /I "%~1"=="--no-run" set "NO_RUN=1"
if /I "%~1"=="/no-run" set "NO_RUN=1"

where cmake >nul 2>&1
if errorlevel 1 (
    echo CMake was not found in PATH.
    exit /b 1
)

if exist "%ROOT_DIR%\.git" (
    echo [0/3] Updating submodules...
    git -C "%ROOT_DIR%" submodule update --init --recursive
    if errorlevel 1 (
        echo Submodule update failed.
        exit /b 1
    )
)

rem Drop a CMakeCache.txt whose CMAKE_HOME_DIRECTORY points at a different
rem checkout (e.g., moving the repo between drives). CMake refuses to reuse
rem such a cache; without this guard `examples_smoke` fails on any host that
rem previously built at a different path.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /R /C:"^CMAKE_HOME_DIRECTORY:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt" | findstr /I /C:"%ROOT_DIR:\=/%" >nul 2>&1
    if errorlevel 1 (
        echo [0.5/3] Stale CMakeCache.txt detected in "%BUILD_DIR%"; wiping.
        rmdir /S /Q "%BUILD_DIR%"
    )
)

echo [1/3] Configuring...
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" -T host=x64 -A x64 -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
if errorlevel 1 (
    echo Configure failed.
    exit /b 1
)

echo [2/3] Building...
cmake --build "%BUILD_DIR%" --config %CONFIG% --target %TARGET%
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if not exist "%EXE_PATH%" (
    echo Executable not found: "%EXE_PATH%"
    exit /b 1
)

if "%NO_RUN%"=="1" (
    echo [3/3] Skipping run.
    exit /b 0
)

echo [3/3] Running...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%EXE_PATH%'"
if errorlevel 1 (
    echo Run failed.
    exit /b 1
)

exit /b 0
