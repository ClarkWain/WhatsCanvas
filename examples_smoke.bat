@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

call :get_tick EXAMPLES_START_MS

echo EXAMPLES_SMOKE_TARGETS=Tetris,Racer,BubbleShooter

call :run_example Tetris "%ROOT_DIR%\example\game\tetris\build.bat"
if errorlevel 1 goto fail

call :run_example Racer "%ROOT_DIR%\example\game\racer\build.bat"
if errorlevel 1 goto fail

call :run_example BubbleShooter "%ROOT_DIR%\example\game\bubble_shooter\build.bat"
if errorlevel 1 goto fail

call :elapsed_ms EXAMPLES_START_MS TOTAL_MS
echo EXAMPLES_SMOKE_TOTAL_MS=!TOTAL_MS!
echo EXAMPLES_SMOKE_RESULT=PASS
exit /b 0

:run_example
set "EXAMPLE_NAME=%~1"
set "EXAMPLE_SCRIPT=%~2"

if not exist "%EXAMPLE_SCRIPT%" (
    echo Example build script not found: "%EXAMPLE_SCRIPT%"
    echo EXAMPLES_SMOKE_RESULT=FAIL
    echo EXAMPLES_SMOKE_FAILED_TARGET=!EXAMPLE_NAME!
    echo EXAMPLES_SMOKE_FAILED_STAGE=SCRIPT_MISSING
    exit /b 1
)

echo [EXAMPLE] !EXAMPLE_NAME!
call :get_tick STEP_START_MS
call "%EXAMPLE_SCRIPT%" --no-run
set "STEP_EXIT=!ERRORLEVEL!"
call :elapsed_ms STEP_START_MS STEP_MS
echo EXAMPLES_SMOKE_!EXAMPLE_NAME!_MS=!STEP_MS!
if not "!STEP_EXIT!"=="0" (
    echo EXAMPLES_SMOKE_RESULT=FAIL
    echo EXAMPLES_SMOKE_FAILED_TARGET=!EXAMPLE_NAME!
    echo EXAMPLES_SMOKE_FAILED_STAGE=BUILD
    exit /b 1
)

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

:fail
exit /b 1