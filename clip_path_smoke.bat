@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "EXPECTED_HASH=%~1"

call :get_tick CLIP_PATH_SMOKE_START_MS
set "CPPDEMO_EXERCISE_CLIP_PATH=1"
call "%ROOT_DIR%\smoke_test.bat" %EXPECTED_HASH%
set "SMOKE_EXIT=%ERRORLEVEL%"
call :elapsed_ms CLIP_PATH_SMOKE_START_MS TOTAL_MS
echo CLIP_PATH_SMOKE_TOTAL_MS=!TOTAL_MS!

if not "!SMOKE_EXIT!"=="0" (
    echo CLIP_PATH_SMOKE_RESULT=FAIL
    exit /b !SMOKE_EXIT!
)

echo CLIP_PATH_SMOKE_TEST=PASS
echo CLIP_PATH_SMOKE_RESULT=PASS
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