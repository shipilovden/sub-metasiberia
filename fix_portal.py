#!/usr/bin/env python3
import sys
import os
import struct

# Read the UTF-16 file
input_path = r"C:\programming\substrata\data\resources\portal.bmesh"
output_path = r"C:\programming\substrata_output\vs2022\cyberspace_x64\RelWithDebInfo\data\resources\portal.bmesh"

if not os.path.exists(input_path):
    print(f"Input file not found: {input_path}")
    sys.exit(1)

# Read as binary
with open(input_path, 'rb') as f:
    data = f.read()

print(f"Input file size: {len(data)} bytes")

# Check for UTF-16 LE BOM
if data[:2] == b'\xFF\xFE':
    print("Detected UTF-16 LE BOM")
    # Remove BOM
    utf16_data = data[2:]
    
    # Convert UTF-16 LE to bytes: each UTF-16 char is 2 bytes, take only low byte
    # This is a hack but should work if the file was originally binary
    binary_data = bytearray()
    for i in range(0, len(utf16_data), 2):
        if i + 1 < len(utf16_data):
            # Take only the low byte from each UTF-16 character
            binary_data.append(utf16_data[i])
    
    # Check magic number
    if len(binary_data) >= 4:
        magic = struct.unpack('<I', binary_data[:4])[0]
        print(f"Magic number: {magic} (expected: 12456751)")
        
        if magic == 12456751:
            # Write binary file
            os.makedirs(os.path.dirname(output_path), exist_ok=True)
            with open(output_path, 'wb') as f:
                f.write(binary_data)
            print(f"Fixed file written to: {output_path}")
            print(f"Size: {len(binary_data)} bytes")
        else:
            print(f"ERROR: Magic number is wrong: {magic}")
            # Try writing anyway
            os.makedirs(os.path.dirname(output_path), exist_ok=True)
            with open(output_path, 'wb') as f:
                f.write(binary_data)
            print(f"Wrote file anyway, size: {len(binary_data)} bytes")
    else:
        print("ERROR: File too small after conversion")
        sys.exit(1)
else:
    print("File does not have UTF-16 BOM, trying direct copy...")
    # Check magic
    if len(data) >= 4:
        magic = struct.unpack('<I', data[:4])[0]
        print(f"Magic: {magic} (expected: 12456751)")
        if magic == 12456751:
            os.makedirs(os.path.dirname(output_path), exist_ok=True)
            with open(output_path, 'wb') as f:
                f.write(data)
            print(f"Copied to: {output_path}")
        else:
            print(f"ERROR: Wrong magic number: {magic}")
