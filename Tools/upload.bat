@echo off
REM W25Q64 Flash Upload Script for Windows
REM Usage: upload.bat <firmware.bin> [address]

if "%1"=="" (
    echo Usage: upload.bat ^<firmware.bin^> [address]
    echo Example: upload.bat firmware.bin 0x00000000
    exit /b 1
)

set FIRMWARE=%1
set ADDRESS=%2

if "%ADDRESS%"=="" (
    set ADDRESS=0x00000000
)

REM Change COM3 to your actual COM port
set COM_PORT=COM3
set BAUDRATE=115200

echo ========================================
echo W25Q64 UART Bootloader - Upload Tool
echo ========================================
echo.
echo Firmware: %FIRMWARE%
echo Address:  %ADDRESS%
echo Port:     %COM_PORT%
echo Baudrate: %BAUDRATE%
echo.

python flash_upload.py -p %COM_PORT% -b %BAUDRATE% -f %FIRMWARE% -a %ADDRESS% -v

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Upload completed successfully!
    echo ========================================
) else (
    echo.
    echo ========================================
    echo Upload failed! Please check connection.
    echo ========================================
)

pause
