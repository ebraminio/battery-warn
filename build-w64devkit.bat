@echo off
setlocal

set "W64DEVKIT_DIR=%USERPROFILE%\w64devkit\bin"
set "PATH=%W64DEVKIT_DIR%;%PATH%"

echo ==^> Checking for running instances...
taskkill /F /IM BatteryWarn.exe >nul 2>&1

echo ==^> Compiling resources...
windres battery-warn.rc -o battery-warn-res.o
if errorlevel 1 goto :error

echo ==^> Building BatteryWarn.exe with GCC (w64devkit)...
gcc battery-warn.cc toast.cc battery-warn-res.o -o BatteryWarn.exe ^
    -std=c++23 ^
    -Os -s ^
    -fno-exceptions -fno-rtti ^
    -ffunction-sections -fdata-sections ^
    -nostdlib -nodefaultlibs -nostartfiles ^
    -Wl,-e,start ^
    -Wl,-subsystem,windows ^
    -Wl,--gc-sections ^
    -lkernel32 -luser32 -lshell32 -ladvapi32 -lole32 -luuid
if errorlevel 1 goto :error

del battery-warn-res.o >nul 2>&1

echo ==^> Build successful! Launching...
start BatteryWarn.exe
goto :eof

:error
echo.
echo !! BUILD FAILED !!
echo Please check the compiler output above.
pause
exit /b 1
