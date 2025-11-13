@echo off
REM Quick Build for GitHub Pages OTA
REM Run this from ESP-IDF CMD

echo.
echo ================================================
echo   Building Firmware for GitHub Pages OTA
echo ================================================
echo.

REM Build firmware
idf.py -B build_otto -D BOARD_TYPE=otto-robot build

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Build completed!
echo.

REM Copy binary to docs for GitHub Pages
set BUILD_DIR=build_otto
set OUTPUT_BIN=docs\firmware.bin

REM Find the .bin file
for %%f in (%BUILD_DIR%\*.bin) do (
    if not "%%~nxf"=="bootloader.bin" (
        if not "%%~nxf"=="partition-table.bin" (
            copy /Y "%%f" "%OUTPUT_BIN%"
            echo.
            echo [SUCCESS] Firmware copied to: %OUTPUT_BIN%
            echo File size: %%~zf bytes
            echo.
        )
    )
)

echo.
echo ================================================
echo   NEXT STEPS:
echo ================================================
echo.
echo 1. Update version in docs\version.json
echo 2. git add docs/
echo 3. git commit -m "Update firmware vX.X.X"
echo 4. git push origin main
echo 5. Enable GitHub Pages: Settings ^> Pages ^> /docs
echo.
echo Your OTA URL:
echo https://nguyenconghuy2904-source.github.io/xiaozhi-esp32-otto-robot/version.json
echo.
pause
