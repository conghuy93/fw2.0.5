@echo off
REM Quick Flash Otto Robot - Stops monitor first
echo.
echo ================================================
echo   Otto Robot - Force Flash (Stop Monitor First)
echo ================================================
echo.

REM Kill any existing monitor processes
echo [1/4] Stopping any running monitors...
taskkill /F /IM python.exe /FI "WINDOWTITLE eq *monitor*" >nul 2>&1
timeout /t 2 /nobreak >nul

REM Initialize ESP-IDF
echo [2/4] Loading ESP-IDF environment...
if exist "C:\Espressif\frameworks\esp-idf-v5.5\export.bat" (
    call "C:\Espressif\frameworks\esp-idf-v5.5\export.bat" >nul
) else (
    echo ERROR: ESP-IDF not found!
    pause
    exit /b 1
)

REM Flash
echo [3/4] Flashing firmware to COM31...
idf.py -B build_otto -p COM31 flash
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Flash failed!
    echo.
    echo Try manually:
    echo   1. Press Ctrl+] in any monitor window to close it
    echo   2. Run this script again
    echo.
    pause
    exit /b 1
)

echo.
echo ================================================
echo   FLASH SUCCESSFUL!
echo ================================================
echo.
echo Changes in this build:
echo   [+] Canvas visibility forced to top layer
echo   [+] Full opacity for canvas object  
echo   [+] Clickable flag added
echo   [+] Fixed format string (%%zu to %%u)
echo   [+] Debug logging for first 10 pixels
echo.
echo [4/4] Starting monitor...
echo Press Ctrl+] to exit monitor
echo.
timeout /t 2 /nobreak >nul
idf.py -B build_otto -p COM31 monitor
