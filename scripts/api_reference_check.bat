@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT_DIR=%%~fI"

python "%ROOT_DIR%\scripts\generate_api_reference.py" --check
if errorlevel 1 (
    echo API_REFERENCE_CHECK_RESULT=FAIL
    exit /b 1
)

echo API_REFERENCE_CHECK_RESULT=PASS
exit /b 0
