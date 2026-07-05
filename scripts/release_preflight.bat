@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT_DIR=%%~fI"

set "BUILD_DIR=%ROOT_DIR%\build-preflight"
set "CONFIG=Debug"

if defined WHATSCANVAS_PREFLIGHT_BUILD_DIR set "BUILD_DIR=%WHATSCANVAS_PREFLIGHT_BUILD_DIR%"
if defined WHATSCANVAS_PREFLIGHT_CONFIG set "CONFIG=%WHATSCANVAS_PREFLIGHT_CONFIG%"

call "%SCRIPT_DIR%\api_reference_check.bat"
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=API_REFERENCE
    exit /b 1
)

call "%SCRIPT_DIR%\version_consistency_check.bat"
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=VERSION
    exit /b 1
)

cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=CONFIGURE
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=BUILD
    exit /b 1
)

ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -L unit --output-on-failure
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=UNIT
    exit /b 1
)

ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -R "^(WhatsCanvasApiReferenceCheck|WhatsCanvasVersionConsistencyCheck|WhatsCanvasPackageConsumerSmoke)$" --output-on-failure
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=ENGINEERING_GATES
    exit /b 1
)

echo RELEASE_PREFLIGHT_RESULT=PASS
exit /b 0
