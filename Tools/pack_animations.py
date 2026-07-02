#!/usr/bin/env python3
"""
Animation Packer for STM32
Packs multiple animations (RLE frames + ADPCM audio) into single binary file
Compatible with animation_player.c
"""

import os
import struct
import sys


def skip_wav_header(wav_data):
    """
    Skip WAV file header and return only raw audio data
    
    WAV format:
    - RIFF header: "RIFF" (4) + size (4) + "WAVE" (4) = 12 bytes
    - Chunks: "fmt " (4) + size (4) + data, "data" (4) + size (4) + audio_data
    
    Returns raw audio data (after "data" chunk header)
    """
    if len(wav_data) < 12:
        return wav_data  # Too small, return as-is
    
    # Check RIFF header
    if wav_data[0:4] != b'RIFF' or wav_data[8:12] != b'WAVE':
        # Not a WAV file, assume it's already raw data
        return wav_data
    
    # Find "data" chunk
    offset = 12
    while offset < len(wav_data) - 8:
        chunk_id = wav_data[offset:offset+4]
        chunk_size = struct.unpack('<I', wav_data[offset+4:offset+8])[0]
        
        if chunk_id == b'data':
            # Found data chunk, return audio data only
            return wav_data[offset+8:offset+8+chunk_size]
        
        # Move to next chunk
        offset += 8 + chunk_size
    
    # No data chunk found, return original
    return wav_data


def build_animation_block(anim_path):
    """
    Build single animation block from folder
    
    Structure:
    - frames/ folder with .rle files
    - sound.wav file (ADPCM format)
    
    Returns binary block with ANIM header + frame table + frames + audio
    """
    frames_dir = os.path.join(anim_path, "frames")
    sound_path = os.path.join(anim_path, "sound.wav")
    
    # Validate paths
    if not os.path.isdir(frames_dir):
        raise FileNotFoundError(f"Frames directory not found: {frames_dir}")
    
    # Get all RLE frame files sorted by name
    frame_files = sorted([
        os.path.join(frames_dir, f)
        for f in os.listdir(frames_dir)
        if f.lower().endswith(".rle")
    ])
    
    if not frame_files:
        raise ValueError(f"No .rle files found in {frames_dir}")
    
    print(f"  Found {len(frame_files)} frames")
    
    # Read audio data (optional)
    sound_data = b''
    if os.path.exists(sound_path):
        with open(sound_path, 'rb') as f:
            raw_data = f.read()
        
        # Check if it's a WAV file with header or raw ADPCM
        if raw_data[:4] == b'RIFF':
            # It's a WAV file - skip header to get raw ADPCM data
            sound_data = skip_wav_header(raw_data)
            print(f"  Found audio: {len(raw_data)} bytes (WAV) -> {len(sound_data)} bytes (raw ADPCM)")
        else:
            # Already raw ADPCM data (no header)
            sound_data = raw_data
            print(f"  Found audio: {len(sound_data)} bytes (raw ADPCM)")
    else:
        print(f"  No audio file found (optional)")
    
    # Build frame table and collect frame data
    frame_table = b''
    frame_data = b''
    current_offset = 0
    
    for frame_path in frame_files:
        with open(frame_path, 'rb') as f:
            data = f.read()
        
        # Frame table entry: offset (4) + size (4)
        frame_table += struct.pack('<I', current_offset)
        frame_table += struct.pack('<I', len(data))
        
        frame_data += data
        current_offset += len(data)
    
    frame_count = len(frame_files)
    
    # Calculate offsets
    # Header size: 4 (magic) + 2 (count) + 4 (table_offset) + 4 (data_size) + 4 (sound_size) = 18
    frame_table_offset = 18
    
    # Build animation header
    header = b'ANIM'
    header += struct.pack('<H', frame_count)           # Frame count
    header += struct.pack('<I', frame_table_offset)    # Frame table offset
    header += struct.pack('<I', len(frame_data))       # Frame data size
    header += struct.pack('<I', len(sound_data))       # Sound data size
    
    return header + frame_table + frame_data + sound_data


def pack_all_animations(input_root_folder, output_bin_path):
    """
    Pack all animation folders into single animations.bin file
    
    Input structure:
    input_root_folder/
    ├── animation1/
    │   ├── frames/
    │   │   ├── frame_001.rle
    │   │   └── ...
    │   └── sound.wav (optional)
    ├── animation2/
    └── ...
    
    Output: Single binary file with PACK header + table + animation blocks
    """
    
    print(f"Scanning animations in: {input_root_folder}")
    
    # Get all animation folders (sorted alphabetically)
    animation_names = sorted([
        name for name in os.listdir(input_root_folder)
        if os.path.isdir(os.path.join(input_root_folder, name))
    ])
    
    if not animation_names:
        print("ERROR: No animation folders found!")
        return False
    
    print(f"Found {len(animation_names)} animation(s):")
    for name in animation_names:
        print(f"  - {name}")
    
    output = bytearray()
    table = bytearray()
    
    # Calculate offset after header and table
    # Header: 4 (magic) + 2 (count) = 6
    # Table: 40 bytes per animation (32 name + 4 offset + 4 size)
    offset = 6 + len(animation_names) * 40
    
    # PACK header
    output += b'PACK'
    output += struct.pack('<H', len(animation_names))
    
    # Build animation blocks
    animation_blocks = []
    
    for anim_name in animation_names:
        print(f"\nProcessing: {anim_name}")
        anim_path = os.path.join(input_root_folder, anim_name)
        
        try:
            anim_block = build_animation_block(anim_path)
        except Exception as e:
            print(f"ERROR building {anim_name}: {e}")
            return False
        
        # Prepare name (max 31 chars + null terminator)
        name_bytes = anim_name.encode('utf-8')[:31]
        name_bytes += b'\x00' * (32 - len(name_bytes))  # Pad to 32 bytes
        
        # Animation table entry
        table += name_bytes
        table += struct.pack('<I', offset)           # Offset to animation block
        table += struct.pack('<I', len(anim_block))  # Size of animation block
        
        offset += len(anim_block)
        animation_blocks.append(anim_block)
        
        print(f"  Block size: {len(anim_block)} bytes")
    
    # Assemble final binary
    output += table
    
    for block in animation_blocks:
        output += block
    
    # Write to file
    try:
        with open(output_bin_path, 'wb') as f:
            f.write(output)
    except Exception as e:
        print(f"ERROR writing output file: {e}")
        return False
    
    print(f"\n{'='*60}")
    print(f"✅ Successfully packed {len(animation_names)} animation(s)")
    print(f"📦 Output: {output_bin_path}")
    print(f"📊 Total size: {len(output):,} bytes ({len(output)/1024:.2f} KB)")
    print(f"{'='*60}")
    
    # Print summary table
    print("\nAnimation Summary:")
    print(f"{'Name':<30} {'Offset':<12} {'Size':<12}")
    print("-" * 60)
    
    # Parse back the table for display
    table_offset = 6
    for i, name in enumerate(animation_names):
        entry_offset = table_offset + i * 40
        anim_offset = struct.unpack('<I', output[entry_offset+32:entry_offset+36])[0]
        anim_size = struct.unpack('<I', output[entry_offset+36:entry_offset+40])[0]
        print(f"{name:<30} {anim_offset:<12} {anim_size:<12}")
    
    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: python pack_animations.py <input_folder> [output_file]")
        print("\nExample:")
        print("  python pack_animations.py animations/")
        print("  python pack_animations.py animations/ output/animations.bin")
        print("\nInput folder structure:")
        print("  animations/")
        print("  ├── idle/")
        print("  │   ├── frames/")
        print("  │   │   ├── frame_001.rle")
        print("  │   │   └── ...")
        print("  │   └── sound.wav (optional - raw ADPCM data)")
        print("  ├── happy/")
        print("  └── ...")
        print("\nNote:")
        print("  sound.wav must be raw ADPCM data (no WAV header)")
        print("  Use convert_audio.py to create compatible audio files")
        sys.exit(1)
    
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Input directory
    input_dir = sys.argv[1] if len(sys.argv) > 1 else script_dir
    
    # Output file
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
        # Auto-add .bin extension if missing
        if not output_file.endswith('.bin'):
            output_file = output_file + '.bin'
    else:
        output_file = os.path.join(script_dir, "animations.bin")
    
    # Validate input
    if not os.path.isdir(input_dir):
        print(f"ERROR: Input directory not found: {input_dir}")
        sys.exit(1)
    
    # Pack animations
    success = pack_all_animations(input_dir, output_file)
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
