@echo off
REM Builds both programs. Run this from the project folder.
REM   overlay.exe - the actual on-screen overlay
REM   main.exe    - console tool that prints the same info once, for testing

setlocal

REM Add MinGW to PATH for this script only, if it isn't already there.
where g++ >nul 2>&1 || set "PATH=C:\mingw-w64\mingw64\bin;%PATH%"

where g++ >nul 2>&1
if errorlevel 1 (
    echo ERROR: g++ not found. Install MinGW-w64 or add it to your PATH.
    exit /b 1
)

echo Building main.exe...
g++ main.cpp -o main.exe -static -lpsapi
if errorlevel 1 (
    echo BUILD FAILED: main.exe
    exit /b 1
)

echo Building overlay.exe...
g++ overlay.cpp -o overlay.exe -mwindows -static -lmsimg32
if errorlevel 1 (
    echo BUILD FAILED: overlay.exe
    exit /b 1
)

echo.
echo Build complete.
