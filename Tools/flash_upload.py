#!/usr/bin/env python3
"""
W25Q128 UART Bootloader - PC Upload Tool
-----------------------------------------
Uploads .bin files to STM32U5 which writes them to W25Q128 flash memory.

Usage:
    python flash_upload.py -p COM3 -f animations.bin -a 0x00000000

Requirements:
    pip install pyserial
"""

import serial
import struct
import time
import sys
import argparse
from pathlib import Path

# Protocol constants
BOOT_START_MARKER1 = 0xAA
BOOT_START_MARKER2 = 0x55
BOOT_ACK  = 0x79
BOOT_NACK = 0x1F

# Commands
BOOT_CMD_WRITE        = 0x01
BOOT_CMD_READ         = 0x02
BOOT_CMD_ERASE_SECTOR = 0x03
BOOT_CMD_ERASE_CHIP   = 0x04
BOOT_CMD_GET_INFO     = 0x05

# Configuration
MAX_CHUNK_SIZE = 4096   # bytes per write packet
SECTOR_SIZE    = 4096
TIMEOUT        = 5      # seconds

class W25Q128Flasher:
    def __init__(self, port, baudrate=115200):
        """Initialize serial connection"""
        try:
            self.ser = serial.Serial(port, baudrate, timeout=TIMEOUT)
            time.sleep(2)  # Wait for device to be ready
            print(f"Connected to {port} at {baudrate} baud")
        except serial.SerialException as e:
            print(f"Error opening serial port: {e}")
            sys.exit(1)
    
    def close(self):
        """Close serial connection"""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def calculate_crc16(self, data):
        """Calculate CRC16-CCITT"""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                if crc & 0x8000:
                    crc = (crc << 1) ^ 0x1021
                else:
                    crc <<= 1
                crc &= 0xFFFF
        return crc
    
    def send_command(self, command, data=b''):
        """Send command packet"""
        # Send start markers
        packet = bytes([BOOT_START_MARKER1, BOOT_START_MARKER2])
        # Send command
        packet += bytes([command])
        # Send data
        packet += data
        
        self.ser.write(packet)
        self.ser.flush()
    
    def wait_for_ack(self):
        """Wait for ACK response"""
        response = self.ser.read(1)
        if len(response) == 0:
            print("Timeout waiting for response")
            return False
        if response[0] == BOOT_ACK:
            return True
        elif response[0] == BOOT_NACK:
            print("Received NACK")
            return False
        else:
            print(f"Unexpected response: 0x{response[0]:02X}")
            return False
    
    def get_info(self):
        """Get flash information"""
        print("Getting flash info...")
        self.send_command(BOOT_CMD_GET_INFO)
        
        if not self.wait_for_ack():
            return None
        
        # Read 13 bytes of info
        info = self.ser.read(13)
        if len(info) != 13:
            print("Error reading flash info")
            return None
        
        manufacturer_id = info[0]
        device_id = info[1]
        jedec = [info[2], info[3], info[4]]
        capacity = struct.unpack('<I', info[5:9])[0]
        page_size = struct.unpack('<H', info[9:11])[0]
        sector_size = struct.unpack('<H', info[11:13])[0]
        
        print(f"Manufacturer ID: 0x{manufacturer_id:02X}")
        print(f"Device ID: 0x{device_id:02X}")
        print(f"JEDEC ID: 0x{jedec[0]:02X} 0x{jedec[1]:02X} 0x{jedec[2]:02X}")
        print(f"Capacity: {capacity} bytes ({capacity/(1024*1024):.1f} MB)")
        print(f"Page Size: {page_size} bytes")
        print(f"Sector Size: {sector_size} bytes")
        
        return {
            'manufacturer_id': manufacturer_id,
            'device_id': device_id,
            'jedec': jedec,
            'capacity': capacity,
            'page_size': page_size,
            'sector_size': sector_size
        }
    
    def erase_sectors(self, start_address, size):
        """Erase necessary sectors for the given size"""
        num_sectors = (size + SECTOR_SIZE - 1) // SECTOR_SIZE
        print(f"Erasing {num_sectors} sectors starting at 0x{start_address:08X}...")
        
        for i in range(num_sectors):
            sector_addr = start_address + (i * SECTOR_SIZE)
            print(f"  Erasing sector at 0x{sector_addr:08X}... ", end='', flush=True)
            
            # Prepare erase command
            data = struct.pack('<I', sector_addr)
            self.send_command(BOOT_CMD_ERASE_SECTOR, data)
            
            if self.wait_for_ack():
                print("OK")
            else:
                print("FAILED")
                return False
        
        return True
    
    def write_data(self, address, data):
        """Write data to flash"""
        data_length = len(data)
        
        # Prepare write command data
        cmd_data = struct.pack('<I', data_length)  # Data length
        cmd_data += struct.pack('<I', address)      # Address
        cmd_data += data                             # Data
        
        # Calculate CRC
        crc = self.calculate_crc16(data)
        cmd_data += struct.pack('<H', crc)
        
        # Send command
        self.send_command(BOOT_CMD_WRITE, cmd_data)
        
        # Wait for ACK
        return self.wait_for_ack()
    
    def write_file(self, filename, start_address=0x00000000):
        """Write binary file to flash"""
        # Read file
        try:
            with open(filename, 'rb') as f:
                file_data = f.read()
        except IOError as e:
            print(f"Error reading file: {e}")
            return False
        
        file_size = len(file_data)
        print(f"\nFile: {filename}")
        print(f"Size: {file_size} bytes ({file_size/1024:.2f} KB)")
        print(f"Start address: 0x{start_address:08X}")
        
        # Get flash info
        info = self.get_info()
        if not info:
            return False
        
        # Check if file fits in flash
        if start_address + file_size > info['capacity']:
            print(f"Error: data exceeds flash capacity "
                  f"(0x{start_address + file_size:08X} > 0x{info['capacity']:08X})")
            return False

        # Erase necessary sectors
        if not self.erase_sectors(start_address, file_size):
            return False

        # Write data in chunks
        total_chunks = (file_size + MAX_CHUNK_SIZE - 1) // MAX_CHUNK_SIZE
        print(f"\nWriting {file_size} bytes in {total_chunks} chunk(s)...")

        for chunk_num in range(total_chunks):
            offset     = chunk_num * MAX_CHUNK_SIZE
            chunk_data = file_data[offset:offset + MAX_CHUNK_SIZE]
            chunk_addr = start_address + offset
            pct        = (chunk_num + 1) / total_chunks * 100
            print(f"  [{pct:5.1f}%] chunk {chunk_num+1}/{total_chunks} "
                  f"@ 0x{chunk_addr:08X} ({len(chunk_data)} B) ... ", end='', flush=True)

            if self.write_data(chunk_addr, chunk_data):
                print("OK")
            else:
                print("FAILED")
                return False

        print("\nWrite complete!")
        return True
    
    def verify_data(self, address, expected_data):
        """Verify written data"""
        data_length = len(expected_data)
        
        # Prepare read command data
        cmd_data = struct.pack('<I', data_length)  # Data length
        cmd_data += struct.pack('<I', address)      # Address
        
        # Send command
        self.send_command(BOOT_CMD_READ, cmd_data)
        
        # Wait for ACK
        if not self.wait_for_ack():
            return False
        
        # Read data
        read_data = self.ser.read(data_length)
        if len(read_data) != data_length:
            print(f"Error: Expected {data_length} bytes, got {len(read_data)}")
            return False
        
        # Read CRC
        crc_bytes = self.ser.read(2)
        if len(crc_bytes) != 2:
            print("Error reading CRC")
            return False
        
        received_crc = struct.unpack('<H', crc_bytes)[0]
        calculated_crc = self.calculate_crc16(read_data)
        
        if received_crc != calculated_crc:
            print(f"CRC mismatch: received 0x{received_crc:04X}, calculated 0x{calculated_crc:04X}")
            return False
        
        # Compare data
        if read_data != expected_data:
            print("Data mismatch!")
            return False
        
        return True
    
    def verify_file(self, filename, start_address=0x00000000):
        """Verify file in flash"""
        # Read file
        try:
            with open(filename, 'rb') as f:
                file_data = f.read()
        except IOError as e:
            print(f"Error reading file: {e}")
            return False
        
        file_size    = len(file_data)
        total_chunks = (file_size + MAX_CHUNK_SIZE - 1) // MAX_CHUNK_SIZE
        print(f"\nVerifying {file_size} bytes...")

        for chunk_num in range(total_chunks):
            offset     = chunk_num * MAX_CHUNK_SIZE
            chunk_data = file_data[offset:offset + MAX_CHUNK_SIZE]
            chunk_addr = start_address + offset
            pct        = (chunk_num + 1) / total_chunks * 100
            print(f"  [{pct:5.1f}%] chunk {chunk_num+1}/{total_chunks} "
                  f"@ 0x{chunk_addr:08X} ... ", end='', flush=True)

            if self.verify_data(chunk_addr, chunk_data):
                print("OK")
            else:
                print("FAILED")
                return False

        print("\nVerification complete!")
        return True

def main():
    parser = argparse.ArgumentParser(
        description='W25Q128 UART Bootloader - Upload Tool')
    parser.add_argument('-p', '--port',     required=True,
                        help='Serial port (e.g. COM3 or /dev/ttyUSB0)')
    parser.add_argument('-b', '--baudrate', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    parser.add_argument('-f', '--file',     required=False, default=None,
                        help='Binary file to upload (e.g. animations.bin)')
    parser.add_argument('-a', '--address',  default='0x00000000',
                        help='Start address in hex (default: 0x00000000)')
    parser.add_argument('-v', '--verify',   action='store_true',
                        help='Verify after writing')
    parser.add_argument('-i', '--info',     action='store_true',
                        help='Only query flash info, do not write')
    args = parser.parse_args()

    try:
        start_address = int(args.address, 16)
    except ValueError:
        print(f"Invalid address: {args.address}")
        sys.exit(1)

    if not args.info and (args.file is None or not Path(args.file).is_file()):
        print(f"File not found: {args.file}")
        sys.exit(1)

    flasher = W25Q128Flasher(args.port, args.baudrate)
    try:
        if args.info:
            flasher.get_info()
        else:
            if flasher.write_file(args.file, start_address):
                print("\n\u2713 Upload successful!")
                if args.verify:
                    if flasher.verify_file(args.file, start_address):
                        print("\u2713 Verification successful!")
                    else:
                        print("\u2717 Verification failed!")
                        sys.exit(1)
            else:
                print("\n\u2717 Upload failed!")
                sys.exit(1)
    except KeyboardInterrupt:
        print("\nOperation cancelled by user")
    finally:
        flasher.close()

if __name__ == '__main__':
    main()
