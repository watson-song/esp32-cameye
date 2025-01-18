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

# Flash the project
echo "Flashing..."
if idf.py -p $PORT flash; then
    echo "Flash successful! Starting monitor..."
    idf.py -p $PORT monitor
else
    echo "Flash failed! Please check the errors above."
    exit 1
fi