#!/usr/bin/env python3
"""
RLE Encoder for Animation Frames
Converts images to RLE-encoded RGB565 format for STM32 SSD1331 display
"""

import sys
import os
from PIL import Image


def rgb_to_rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def encode_frame_to_rle(image_path, output_path, width=96, height=64):
    """
    Convert image to RLE format
    
    Parameters:
    - image_path: Input image file
    - output_path: Output .rle file
    - width, height: Target dimensions (default: 96x64 for SSD1331)
    
    RLE Format:
    [count][color_hi][color_lo] repeating
    - count: Number of consecutive pixels with same color (1-255)
    - color_hi, color_lo: RGB565 color bytes (big-endian)
    """
    
    try:
        # Load and resize image
        image = Image.open(image_path)
        image = image.convert('RGB')
        image = image.resize((width, height), Image.Resampling.LANCZOS)
        
        print(f"Processing: {image_path}")
        print(f"  Size: {width}x{height}")
        
    except Exception as e:
        print(f"Error loading image: {e}")
        return False
    
    # Encode to RLE
    rle_data = bytearray()
    prev_color = None
    count = 0
    total_pixels = 0
    
    for y in range(height):
        for x in range(width):
            r, g, b = image.getpixel((x, y))
            rgb565 = rgb_to_rgb565(r, g, b)
            
            if rgb565 == prev_color and count < 255:
                count += 1
            else:
                # Write previous run
                if prev_color is not None:
                    rle_data.extend([count, prev_color >> 8, prev_color & 0xFF])
                
                prev_color = rgb565
                count = 1
            
            total_pixels += 1
    
    # Write last run
    if count > 0:
        rle_data.extend([count, prev_color >> 8, prev_color & 0xFF])
    
    # Save to file
    try:
        with open(output_path, 'wb') as f:
            f.write(rle_data)
        
        uncompressed_size = total_pixels * 2  # RGB565 = 2 bytes per pixel
        compressed_size = len(rle_data)
        ratio = uncompressed_size / compressed_size if compressed_size > 0 else 1
        
        print(f"  Uncompressed: {uncompressed_size} bytes")
        print(f"  Compressed: {compressed_size} bytes")
        print(f"  Ratio: {ratio:.2f}x")
        print(f"  Output: {output_path}")
        
        return True
        
    except Exception as e:
        print(f"Error writing RLE file: {e}")
        return False


def batch_convert(input_folder, output_folder, width=96, height=64):
    """
    Convert all images in folder to RLE format
    
    Supports: PNG, JPG, BMP, GIF
    """
    
    if not os.path.isdir(input_folder):
        print(f"Error: Input folder not found: {input_folder}")
        return False
    
    # Create output folder
    os.makedirs(output_folder, exist_ok=True)
    
    # Supported image extensions
    extensions = ('.png', '.jpg', '.jpeg', '.bmp', '.gif')
    
    # Get all image files sorted by name
    image_files = sorted([
        f for f in os.listdir(input_folder)
        if f.lower().endswith(extensions)
    ])
    
    if not image_files:
        print(f"No image files found in {input_folder}")
        return False
    
    print(f"Found {len(image_files)} image(s)")
    print(f"Converting to {width}x{height} RLE format...\n")
    
    success_count = 0
    
    for i, filename in enumerate(image_files, 1):
        input_path = os.path.join(input_folder, filename)
        
        # Generate output filename: frame_001.rle, frame_002.rle, ...
        output_filename = f"frame_{i:03d}.rle"
        output_path = os.path.join(output_folder, output_filename)
        
        if encode_frame_to_rle(input_path, output_path, width, height):
            success_count += 1
        
        print()
    
    print(f"{'='*60}")
    print(f"✅ Successfully converted {success_count}/{len(image_files)} frame(s)")
    print(f"📁 Output folder: {output_folder}")
    print(f"{'='*60}")
    
    return True


def main():
    if len(sys.argv) < 2:
        print("RLE Encoder for STM32 Animation Frames")
        print("\nUsage:")
        print("  Single file:  python encode_rle.py <input_image> [output_file]")
        print("  Batch mode:   python encode_rle.py <input_folder> <output_folder>")
        print("\nExamples:")
        print("  python encode_rle.py frame.png frame.rle")
        print("  python encode_rle.py input_frames/ output_frames/")
        print("\nOptions:")
        print("  --width  W    Target width (default: 96)")
        print("  --height H    Target height (default: 64)")
        sys.exit(1)
    
    # Parse arguments
    width = 96
    height = 64
    
    args = sys.argv[1:]
    
    # Check for dimension overrides
    if '--width' in args:
        idx = args.index('--width')
        width = int(args[idx + 1])
        args.pop(idx)
        args.pop(idx)
    
    if '--height' in args:
        idx = args.index('--height')
        height = int(args[idx + 1])
        args.pop(idx)
        args.pop(idx)
    
    input_path = args[0]
    
    # Determine mode: file or folder
    if os.path.isdir(input_path):
        # Batch mode
        output_folder = args[1] if len(args) > 1 else input_path + '_rle'
        success = batch_convert(input_path, output_folder, width, height)
    else:
        # Single file mode
        if not os.path.exists(input_path):
            print(f"Error: File not found: {input_path}")
            sys.exit(1)
        
        # Generate output filename
        if len(args) > 1:
            output_path = args[1]
        else:
            base = os.path.splitext(input_path)[0]
            output_path = base + '.rle'
        
        success = encode_frame_to_rle(input_path, output_path, width, height)
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
