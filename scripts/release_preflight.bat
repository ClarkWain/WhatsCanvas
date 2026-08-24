@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT_DIR=%%~fI"

set "BUILD_DIR=%ROOT_DIR%\build-preflight"
set "CONFIG=Debug"
set "GENERATOR="
set "GENERATOR_IS_MULTI_CONFIG=1"

if defined WHATSCANVAS_PREFLIGHT_BUILD_DIR set "BUILD_DIR=%WHATSCANVAS_PREFLIGHT_BUILD_DIR%"
if defined WHATSCANVAS_PREFLIGHT_CONFIG set "CONFIG=%WHATSCANVAS_PREFLIGHT_CONFIG%"
if defined WHATSCANVAS_PREFLIGHT_GENERATOR set "GENERATOR=%WHATSCANVAS_PREFLIGHT_GENERATOR%"
if "%GENERATOR%"=="" if defined CMAKE_GENERATOR set "GENERATOR=%CMAKE_GENERATOR%"

if /I "%GENERATOR%"=="Ninja" set "GENERATOR_IS_MULTI_CONFIG=0"
if /I "%GENERATOR%"=="NMake Makefiles" set "GENERATOR_IS_MULTI_CONFIG=0"
if /I "%GENERATOR%"=="Unix Makefiles" set "GENERATOR_IS_MULTI_CONFIG=0"

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

python "%ROOT_DIR%\scripts\performance_claims_check.py"
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=PERFORMANCE_CLAIMS
    exit /b 1
)

set "CONFIGURE_ARGS=-S "%ROOT_DIR%" -B "%BUILD_DIR%""
if not "%GENERATOR%"=="" set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -G "%GENERATOR%""
if "%GENERATOR_IS_MULTI_CONFIG%"=="0" set "CONFIGURE_ARGS=%CONFIGURE_ARGS% -DCMAKE_BUILD_TYPE=%CONFIG%"

rem The package-consumer gate configures a nested project. Keep it on the same
rem generator so single-config preflight builds do not accidentally fall back
rem to a missing or incompatible build tool.
if not "%GENERATOR%"=="" if not defined WHATSCANVAS_CONSUMER_GENERATOR set "WHATSCANVAS_CONSUMER_GENERATOR=%GENERATOR%"
if not defined WHATSCANVAS_CONSUMER_MULTI_CONFIG set "WHATSCANVAS_CONSUMER_MULTI_CONFIG=%GENERATOR_IS_MULTI_CONFIG%"
if /I "%GENERATOR%"=="Ninja" if not defined WHATSCANVAS_CONSUMER_MAKE_PROGRAM (
    for %%I in (ninja.exe) do set "WHATSCANVAS_CONSUMER_MAKE_PROGRAM=%%~$PATH:I"
)
if /I "%GENERATOR%"=="Ninja" if not defined WHATSCANVAS_CONSUMER_CXX_COMPILER (
    for %%I in (cl.exe) do set "WHATSCANVAS_CONSUMER_CXX_COMPILER=%%~$PATH:I"
)
if /I "%GENERATOR%"=="Ninja" if not defined WHATSCANVAS_CONSUMER_RC_COMPILER (
    for %%I in (rc.exe) do set "WHATSCANVAS_CONSUMER_RC_COMPILER=%%~$PATH:I"
    set "WHATSCANVAS_CONSUMER_RC_COMPILER=!WHATSCANVAS_CONSUMER_RC_COMPILER:\=/!"
)
if /I "%GENERATOR%"=="Ninja" if not defined WHATSCANVAS_CONSUMER_MT (
    for %%I in (mt.exe) do set "WHATSCANVAS_CONSUMER_MT=%%~$PATH:I"
    set "WHATSCANVAS_CONSUMER_MT=!WHATSCANVAS_CONSUMER_MT:\=/!"
)

cmake %CONFIGURE_ARGS%
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

ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -R "^(WhatsCanvasApiReferenceCheck|WhatsCanvasVersionConsistencyCheck|WhatsCanvasPerformanceClaimsCheck|WhatsCanvasPackageConsumerSmoke)$" --output-on-failure
if errorlevel 1 (
    echo RELEASE_PREFLIGHT_RESULT=FAIL
    echo RELEASE_PREFLIGHT_FAILED_STAGE=ENGINEERING_GATES
    exit /b 1
)

echo RELEASE_PREFLIGHT_RESULT=PASS
exit /b 0
