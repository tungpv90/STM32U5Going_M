#!/usr/bin/env python3
"""
Audio Converter for STM32 ADPCM Playback
Converts MP3/WAV to 16kHz Mono WAV with IMA ADPCM encoding
Compatible with STM32L4 SAI audio playback
"""

import sys
import os
import subprocess
import struct
import wave


def convert_audio_for_stm32(input_file, output_file=None):
    """
    Convert audio file to format compatible with STM32 ADPCM decoder
    
    Parameters:
    - input_file: Path to input audio file (MP3, WAV, etc.)
    - output_file: Path to output WAV file (optional, defaults to input_name_adpcm.wav)
    
    Output format:
    - Sample rate: 16kHz
    - Channels: Mono
    - Encoding: IMA ADPCM (4-bit per sample)
    - Bit depth: 16-bit PCM decoded
    """
    
    # Generate output filename if not provided
    if output_file is None:
        base_name = os.path.splitext(input_file)[0]
        output_file = f"{base_name}_adpcm.wav"
    
    print(f"Converting: {input_file}")
    print(f"Output: {output_file}")
    
    # Get input file info using ffprobe
    try:
        probe_cmd = [
            'ffprobe',
            '-v', 'error',
            '-show_entries', 'stream=sample_rate,channels,codec_name',
            '-of', 'default=noprint_wrappers=1',
            input_file
        ]
        result = subprocess.run(probe_cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print("Original format:")
            for line in result.stdout.strip().split('\n'):
                if line:
                    print(f"  {line}")
    except Exception as e:
        print(f"Warning: Could not probe input file: {e}")
    
    # Step 1: Convert to 16-bit PCM WAV for reference
    pcm_output = output_file.replace('.wav', '_16bit.wav')
    try:
        pcm_cmd = [
            'ffmpeg',
            '-i', input_file,
            '-ar', '16000',          # Sample rate: 16kHz
            '-ac', '1',               # Mono
            '-acodec', 'pcm_s16le',   # 16-bit PCM
            '-af', 'loudnorm',        # Normalize audio
            '-y',                     # Overwrite output
            pcm_output
        ]
        
        print(f"\nConverting to 16-bit PCM...")
        result = subprocess.run(pcm_cmd, capture_output=True, text=True)
        
        if result.returncode != 0:
            print(f"Error converting to PCM: {result.stderr}")
            return False
        
        print(f"✓ Reference 16-bit PCM WAV saved to: {pcm_output}")
        
    except FileNotFoundError:
        print("Error: ffmpeg not found in PATH. Please install ffmpeg.")
        print("Download from: https://ffmpeg.org/download.html")
        return False
    except Exception as e:
        print(f"Error converting audio: {e}")
        return False
    
    print(f"Converted to: 16000Hz, 1 channel, 16-bit")
    
    # Step 2: Convert to IMA ADPCM WAV
    adpcm_wav = output_file
    try:
        adpcm_cmd = [
            'ffmpeg',
            '-i', pcm_output,
            '-acodec', 'adpcm_ima_wav',  # IMA ADPCM encoding
            '-y',
            adpcm_wav
        ]
        
        print(f"\nEncoding to IMA ADPCM...")
        result = subprocess.run(adpcm_cmd, capture_output=True, text=True)
        
        if result.returncode != 0:
            print(f"Error encoding to ADPCM: {result.stderr}")
            return False
        
    except Exception as e:
        print(f"Error encoding to ADPCM: {e}")
        return False
    
    # Step 3: Extract raw ADPCM data from WAV file
    raw_output = output_file.replace('.wav', '.adpcm')
    try:
        # Read the WAV file manually to extract ADPCM data
        with open(adpcm_wav, 'rb') as f:
            # Read RIFF header
            riff = f.read(4)
            if riff != b'RIFF':
                raise ValueError("Not a valid WAV file")
            
            file_size = struct.unpack('<I', f.read(4))[0]
            wave_tag = f.read(4)
            if wave_tag != b'WAVE':
                raise ValueError("Not a valid WAV file")
            
            # Read chunks until we find 'fmt ' and 'data'
            fmt_data = None
            adpcm_data = None
            sample_rate = 16000
            channels = 1
            
            while True:
                chunk_header = f.read(4)
                if len(chunk_header) < 4:
                    break
                    
                chunk_size = struct.unpack('<I', f.read(4))[0]
                
                if chunk_header == b'fmt ':
                    fmt_data = f.read(chunk_size)
                    # Parse format chunk
                    format_tag = struct.unpack('<H', fmt_data[0:2])[0]
                    channels = struct.unpack('<H', fmt_data[2:4])[0]
                    sample_rate = struct.unpack('<I', fmt_data[4:8])[0]
                    
                elif chunk_header == b'data':
                    # This is the raw ADPCM data
                    adpcm_data = f.read(chunk_size)
                    break
                else:
                    # Skip unknown chunks
                    f.read(chunk_size)
            
            if adpcm_data is None:
                raise ValueError("No audio data found in WAV file")
        
        # Write raw ADPCM data to binary file
        with open(raw_output, 'wb') as f:
            f.write(adpcm_data)
        
        print(f"\n✓ Raw ADPCM saved to: {raw_output}")
        print("  Rename this file to 'sound.wav' and place in animation folder")
        
        # Calculate compression ratio (ADPCM is approximately 4:1)
        # For 16kHz mono, each second = 16000 samples = 32000 bytes PCM
        # ADPCM compresses to ~4000 bytes per second (4:1 ratio)
        duration_seconds = len(adpcm_data) / (sample_rate / 4)
        pcm_size = int(duration_seconds * sample_rate * 2)  # 16-bit = 2 bytes per sample
        adpcm_size = len(adpcm_data)
        
        print(f"\nADPCM encoding complete")
        print(f"Estimated PCM size: {pcm_size} bytes")
        print(f"ADPCM size: {adpcm_size} bytes")
        print(f"Compression ratio: {pcm_size/adpcm_size:.2f}x")
        
        # Calculate duration
        print(f"\nAudio duration: {duration_seconds:.2f} seconds")
        print(f"Memory required for playback: {adpcm_size * 4:,} bytes ({adpcm_size * 4 / 1024:.2f} KB)")
        
    except Exception as e:
        print(f"Error extracting ADPCM data: {e}")
        return False
    
    return True


def create_c_header(adpcm_file, header_file=None, array_name="audio_data"):
    """
    Create C header file with ADPCM data array for embedding in STM32
    
    Parameters:
    - adpcm_file: Path to ADPCM binary file
    - header_file: Path to output C header file (optional)
    - array_name: Name of the array in C code
    """
    
    if header_file is None:
        header_file = adpcm_file.replace('.adpcm', '.h')
    
    with open(adpcm_file, 'rb') as f:
        adpcm_data = f.read()
    
    with open(header_file, 'w') as f:
        f.write(f"/* Auto-generated ADPCM audio data */\n")
        f.write(f"#ifndef __{array_name.upper()}_H__\n")
        f.write(f"#define __{array_name.upper()}_H__\n\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"const uint32_t {array_name}_length = {len(adpcm_data)};\n\n")
        f.write(f"const uint8_t {array_name}[] = {{\n")
        
        for i in range(0, len(adpcm_data), 16):
            chunk = adpcm_data[i:i+16]
            hex_values = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f"    {hex_values},\n")
        
        f.write(f"}};\n\n")
        f.write(f"#endif /* __{array_name.upper()}_H__ */\n")
    
    print(f"✓ C header file created: {header_file}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python convert_audio.py <input_file> [output_file]")
        print("\nExample:")
        print("  python convert_audio.py song.mp3")
        print("  python convert_audio.py song.mp3 output.wav")
        print("\nSupported input formats: MP3, WAV, OGG, FLAC, etc.")
        print("\nOutput files:")
        print("  - <name>_adpcm.adpcm      : Raw ADPCM binary (rename to sound.wav)")
        print("  - <name>_adpcm_16bit.wav  : Reference PCM WAV")
        print("  - <name>_adpcm.wav        : IMA ADPCM WAV")
        print("  - <name>_adpcm.h          : C header (optional)")
        print("\nRequirements:")
        print("  Install ffmpeg (required for audio conversion)")
        print("  Download from: https://ffmpeg.org/download.html")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    if not os.path.exists(input_file):
        print(f"Error: File not found: {input_file}")
        sys.exit(1)
    
    # Convert audio
    success = convert_audio_for_stm32(input_file, output_file)
    
    if success:
        # Check if raw ADPCM file was created
        if output_file:
            adpcm_file = output_file.replace('.wav', '.adpcm')
        else:
            base_name = os.path.splitext(input_file)[0]
            adpcm_file = f"{base_name}_adpcm.adpcm"
        
        if os.path.exists(adpcm_file):
            print("\n" + "="*60)
            response = input("Generate C header file for STM32? (y/n): ")
            if response.lower() == 'y':
                create_c_header(adpcm_file)
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
