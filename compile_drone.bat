@echo off
echo Compiling Windows Drone Agent...
gcc src/windows_drone.c -o drone.exe -lws2_32
if %errorlevel% neq 0 (
    echo Compilation Failed! Ensure MinGW/GCC is in your PATH.
    pause
    exit /b
)
echo Success! Run drone.exe as Administrator.
pause
