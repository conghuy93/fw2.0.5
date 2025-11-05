@echo off
echo Building and flashing Otto Robot firmware...
echo.

cd /d C:\Users\congh\Downloads\Compressed\xiaozhi-esp32-2.0.3otto2\xiaozhi-esp32-2.0.3

REM Try to find and run ESP-IDF export script
if exist "C:\Espressif\frameworks\esp-idf-v5.5\export.bat" (
    call C:\Espressif\frameworks\esp-idf-v5.5\export.bat
) else if exist "C:\esp\esp-idf\export.bat" (
    call C:\esp\esp-idf\export.bat
) else (
    echo ERROR: Cannot find ESP-IDF export.bat
    echo Please run this script from ESP-IDF CMD/PowerShell
    pause
    exit /b 1
)

echo.
echo Building firmware...
idf.py -B build_otto build

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Flashing to COM31...
idf.py -p COM31 -B build_otto flash monitor

pause
