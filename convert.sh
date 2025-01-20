#!/bin/bash

# 检查参数
if [ $# -ne 1 ]; then
    echo "Usage: $0 <timestamp>"
    echo "Example: $0 0000"
    exit 1
fi

TIMESTAMP=$1
VIDEO_FILE="${TIMESTAMP}.vid"
AUDIO_FILE="${TIMESTAMP}.pcm"
OUTPUT_VIDEO="${TIMESTAMP}.mp4"
OUTPUT_AUDIO="${TIMESTAMP}.wav"

# 检查文件是否存在
if [ ! -f "$VIDEO_FILE" ]; then
    echo "Error: Video file $VIDEO_FILE not found"
    exit 1
fi

if [ ! -f "$AUDIO_FILE" ]; then
    echo "Error: Audio file $AUDIO_FILE not found"
    exit 1
fi

# 转换视频
echo "Converting video..."
ffmpeg -f mjpeg -i "$VIDEO_FILE" "$OUTPUT_VIDEO"

# 转换音频
echo "Converting audio..."
ffmpeg -f s16le -ar 16000 -ac 1 -i "$AUDIO_FILE" "$OUTPUT_AUDIO"

echo "Conversion complete!"
echo "Video saved as: $OUTPUT_VIDEO"
echo "Audio saved as: $OUTPUT_AUDIO"
