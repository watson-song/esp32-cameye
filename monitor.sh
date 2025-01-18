#!/bin/bash

# Source ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Find the ESP32-S3 port
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null)

if [ -z "$PORT" ]; then
    echo "Error: ESP32-S3 device not found!"
    echo "Please make sure the device is connected and try again."
    exit 1
fi

echo "Found ESP32-S3 at port: $PORT"

# Start monitor
echo "Starting monitor..."
echo "Press Ctrl+] to exit"
idf.py -p $PORT monitor
