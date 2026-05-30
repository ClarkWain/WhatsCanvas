@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "DEFAULT_HASH=%~1"
set "CLIP_HASH=%~2"
if "%DEFAULT_HASH%"=="" set "DEFAULT_HASH=2458027664413625913"
if "%CLIP_HASH%"=="" set "CLIP_HASH=12248791335057056593"

call :get_tick REGRESSION_SMOKE_START_MS

call "%ROOT_DIR%\smoke_test.bat" %DEFAULT_HASH%
set "DEFAULT_EXIT=%ERRORLEVEL%"
if not "%DEFAULT_EXIT%"=="0" (
    call :elapsed_ms REGRESSION_SMOKE_START_MS TOTAL_MS
    echo REGRESSION_SMOKE_TOTAL_MS=!TOTAL_MS!
    echo REGRESSION_SMOKE_RESULT=FAIL
    exit /b %DEFAULT_EXIT%
)

call "%ROOT_DIR%\clip_path_smoke.bat" %CLIP_HASH%
set "CLIP_EXIT=%ERRORLEVEL%"
call :elapsed_ms REGRESSION_SMOKE_START_MS TOTAL_MS
echo REGRESSION_SMOKE_TOTAL_MS=!TOTAL_MS!
if not "%CLIP_EXIT%"=="0" (
    echo REGRESSION_SMOKE_RESULT=FAIL
    exit /b %CLIP_EXIT%
)

echo REGRESSION_SMOKE_TEST=PASS
echo REGRESSION_SMOKE_RESULT=PASS
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