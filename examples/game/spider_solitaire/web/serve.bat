@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
for %%I in ("%ROOT%\..\..\..\..") do set "REPO_ROOT=%%~fI"

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%REPO_ROOT%\out\wasm-web"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=8081"
set "WEB_ROOT=%BUILD_DIR%\platforms\wasm\web"

if not exist "%WEB_ROOT%\spider.html" (
    echo Spider Web build is missing. Run build.bat first.
    exit /b 1
)

echo Serving http://127.0.0.1:%PORT%/spider.html
python -m http.server %PORT% --bind 127.0.0.1 --directory "%WEB_ROOT%"
if errorlevel 1 py -3 -m http.server %PORT% --bind 127.0.0.1 --directory "%WEB_ROOT%"
