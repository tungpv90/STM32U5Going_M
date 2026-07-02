#!/bin/bash
# W25Q64 Flash Upload Script for Linux/Mac
# Usage: ./upload.sh <firmware.bin> [address]

if [ -z "$1" ]; then
    echo "Usage: ./upload.sh <firmware.bin> [address]"
    echo "Example: ./upload.sh firmware.bin 0x00000000"
    exit 1
fi

FIRMWARE="$1"
ADDRESS="${2:-0x00000000}"

# Change /dev/ttyUSB0 to your actual serial port
COM_PORT="/dev/ttyUSB0"
BAUDRATE=115200

echo "========================================"
echo "W25Q64 UART Bootloader - Upload Tool"
echo "========================================"
echo ""
echo "Firmware: $FIRMWARE"
echo "Address:  $ADDRESS"
echo "Port:     $COM_PORT"
echo "Baudrate: $BAUDRATE"
echo ""

python3 flash_upload.py -p "$COM_PORT" -b $BAUDRATE -f "$FIRMWARE" -a "$ADDRESS" -v

if [ $? -eq 0 ]; then
    echo ""
    echo "========================================"
    echo "Upload completed successfully!"
    echo "========================================"
else
    echo ""
    echo "========================================"
    echo "Upload failed! Please check connection."
    echo "========================================"
    exit 1
fi
