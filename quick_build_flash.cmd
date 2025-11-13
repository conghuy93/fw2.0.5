@echo off
REM Quick Build and Flash for Otto Robot - RGB565 Canvas Fix
REM Run this from regular CMD (will auto-start ESP-IDF environment)

echo.
echo ====================================
echo   Otto Robot - Quick Build & Flash
echo ====================================
echo.

REM Check if ESP-IDF environment is already loaded
where idf.py >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] ESP-IDF environment detected
    goto :build
)

REM Try to initialize ESP-IDF environment
echo [!] Loading ESP-IDF environment...
if exist "C:\Espressif\frameworks\esp-idf-v5.5\export.bat" (
    call "C:\Espressif\frameworks\esp-idf-v5.5\export.bat"
) else if exist "%IDF_PATH%\export.bat" (
    call "%IDF_PATH%\export.bat"
) else (
    echo [ERROR] Cannot find ESP-IDF environment!
    echo Please run this from "ESP-IDF 5.5 CMD" or "ESP-IDF 5.5 PowerShell"
    pause
    exit /b 1
)

:build
echo.
echo [1/3] Building firmware...
echo.
idf.py -B build_otto -DBOARD=otto-robot build
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo [OK] Build successful!
echo.
echo [2/3] Flashing to COM31...
echo.
idf.py -B build_otto -p COM31 flash
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Flash failed! Check if COM31 is correct port.
    pause
    exit /b 1
)

echo.
echo [OK] Flash successful!
echo.
echo ====================================
echo   FIRMWARE DEPLOYED SUCCESSFULLY!
echo ====================================
echo.
echo Changes in this build:
echo   [+] RGB565 canvas format (native to ST7789)
echo   [+] Auto-enable canvas on startup
echo   [+] Debug logging for first 10 pixels
echo   [+] Semi-transparent black background
echo.
echo Test the drawing:
echo   1. Wait for Otto to connect to WiFi
echo   2. Check serial monitor for IP address
echo   3. Open browser: http://[IP]/draw
echo   4. Draw on canvas - should see white pixels on Otto!
echo.
echo [3/3] Starting serial monitor...
echo Press Ctrl+] to exit monitor
echo.
timeout /t 3 /nobreak >nul
idf.py -B build_otto -p COM31 monitor
