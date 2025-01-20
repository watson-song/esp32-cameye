#!/usr/bin/env python3
import sys
import re

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 receive.py <output_file>")
        print("Example: python3 receive.py 0000.vid")
        sys.exit(1)

    output_file = sys.argv[1]
    hex_data = ""
    
    print("Paste the hex data from ESP32 (press Ctrl+D when done):")
    try:
        for line in sys.stdin:
            # 去除进度信息行
            if "Transferred:" in line or "Transfer" in line or "File size:" in line:
                continue
            # 只保留十六进制字符
            hex_line = re.sub(r'[^0-9a-fA-F]', '', line)
            hex_data += hex_line
    except KeyboardInterrupt:
        print("\nInput terminated by user")
    
    # 将十六进制字符串转换为字节
    try:
        binary_data = bytes.fromhex(hex_data)
        with open(output_file, 'wb') as f:
            f.write(binary_data)
        print(f"\nFile saved as: {output_file}")
        print(f"Size: {len(binary_data)} bytes")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
