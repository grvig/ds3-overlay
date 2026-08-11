@echo off
REM Builds and runs the checks in tests.cpp. Doesn't need the game running.

setlocal
REM Work from the folder this script lives in, so it can be run from anywhere.
pushd "%~dp0"

where g++ >nul 2>&1 || set "PATH=C:\mingw-w64\mingw64\bin;%PATH%"

where g++ >nul 2>&1
if errorlevel 1 (
    echo ERROR: g++ not found. Install MinGW-w64 or add it to your PATH.
    popd
    exit /b 1
)

g++ tests.cpp -o tests.exe -static -lpsapi -lversion
if errorlevel 1 (
    echo BUILD FAILED: tests.exe
    popd
    exit /b 1
)

.\tests.exe
set RESULT=%errorlevel%
popd
exit /b %RESULT%
