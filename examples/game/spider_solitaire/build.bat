@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "CONFIG=Release"
set "NORUN=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--release" set "CONFIG=Release"
if /I "%~1"=="--debug" set "CONFIG=Debug"
if /I "%~1"=="--no-run" set "NORUN=1"
if /I not "%~1"=="--release" if /I not "%~1"=="--debug" if /I not "%~1"=="--no-run" (
  echo Unknown argument: %~1
  echo Usage: build.bat [--no-run] [--debug^|--release]
  exit /b 1
)
shift
goto parse_args

:args_done
cmake -S "%ROOT%" -B "%BUILD%" -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b 1
cmake --build "%BUILD%" --config %CONFIG% --target SpiderSolitaire
if errorlevel 1 exit /b 1
if "%NORUN%"=="1" exit /b 0
if exist "%BUILD%\%CONFIG%\SpiderSolitaire.exe" (
  "%BUILD%\%CONFIG%\SpiderSolitaire.exe"
) else (
  "%BUILD%\SpiderSolitaire.exe"
)
