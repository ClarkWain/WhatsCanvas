@echo off
setlocal
set "ROOT=%~dp0"
set "CONFIG=Release"
set "NORUN=0"
:parse
if "%~1"=="" goto build
if /I "%~1"=="--debug" (set "CONFIG=Debug") else if /I "%~1"=="--release" (set "CONFIG=Release") else if /I "%~1"=="--no-run" (set "NORUN=1") else (
  echo Usage: build.bat [--no-run] [--debug^|--release]
  exit /b 1
)
shift
goto parse
:build
cmake -S "%ROOT%." -B "%ROOT%build" -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b 1
cmake --build "%ROOT%build" --config %CONFIG% --parallel
if errorlevel 1 exit /b 1
if "%NORUN%"=="1" exit /b 0
if exist "%ROOT%build\%CONFIG%\Xiangqi.exe" (
  "%ROOT%build\%CONFIG%\Xiangqi.exe"
) else (
  "%ROOT%build\Xiangqi.exe"
)
