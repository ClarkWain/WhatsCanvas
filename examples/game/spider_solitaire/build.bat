@echo off
setlocal
set ROOT=%~dp0
set BUILD=%ROOT%build
set CONFIG=Debug
set NORUN=0
if "%1"=="--release" set CONFIG=Release
if "%1"=="--no-run" set NORUN=1
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
