@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
for %%I in ("%ROOT%\..\..\..\..") do set "REPO_ROOT=%%~fI"

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%REPO_ROOT%\out\wasm-web"

if "%EMSDK_ROOT%"=="" set "EMSDK_ROOT=%REPO_ROOT%\out\emsdk"
set "EMCMAKE=%EMSDK_ROOT%\upstream\emscripten\emcmake.bat"

if exist "%EMCMAKE%" (
    if exist "%EMSDK_ROOT%\emsdk_env.bat" call "%EMSDK_ROOT%\emsdk_env.bat" >nul
) else (
    set "EMCMAKE=emcmake"
)

if /I "%EMCMAKE%"=="emcmake" (
    where emcmake >nul 2>nul
    if errorlevel 1 (
        echo emcmake is not available. Install emsdk and ensure EMSDK_ROOT points to emsdk_env.bat.
        exit /b 1
    )
)

"%EMCMAKE%" cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_TESTING=OFF ^
    -DWHATSCANVAS_BUILD_BENCHMARKS=OFF ^
    -DWHATSCANVAS_BUILD_DEMO=OFF ^
    -DWHATSCANVAS_BUILD_DESKTOP_PLATFORM=OFF ^
    -DWHATSCANVAS_BUILD_METAL=OFF ^
    -DWHATSCANVAS_BUILD_OPENGL=OFF ^
    -DWHATSCANVAS_BUILD_OPENGLES=ON ^
    -DWHATSCANVAS_BUILD_SOFTWARE=OFF ^
    -DWHATSCANVAS_BUILD_WASM_WEB=ON ^
    -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON ^
    -DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON ^
    -DWHATSCANVAS_INSTALL=OFF ^
    -DWHATSCANVAS_X11=OFF
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --target SpiderSolitaireWeb --parallel
if errorlevel 1 exit /b 1

echo Spider Solitaire Web build: %BUILD_DIR%\platforms\wasm\web\spider.html
