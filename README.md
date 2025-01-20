# ESP32-S3 视频录制器

这个项目使用 ESP32-S3 开发板实现视频和音频的录制功能。

## 硬件要求

- Seeed XIAO ESP32S3 Sense 开发板
- 板载 OV2640 摄像头
- PDM 麦克风
- Micro SD 卡

## 引脚连接

### 摄像头 (OV2640)
- PWDN: -1 (未使用)
- RESET: -1 (未使用)
- XCLK: GPIO10
- SIOD: GPIO40 (I2C SDA)
- SIOC: GPIO39 (I2C SCL)
- D7: GPIO48
- D6: GPIO11
- D5: GPIO12
- D4: GPIO14
- D3: GPIO16
- D2: GPIO18
- D1: GPIO17
- D0: GPIO15
- VSYNC: GPIO38
- HREF: GPIO47
- PCLK: GPIO13

### PDM 麦克风
- CLK: GPIO41
- DIN: GPIO42

### SD 卡 (SPI)
- MOSI: GPIO9
- MISO: GPIO8
- CLK: GPIO7
- CS: GPIO21

## 编译和烧录

1. 设置 ESP-IDF 环境
```bash
. $HOME/esp/esp-idf/export.sh
```

2. 编译项目
```bash
idf.py build
```

3. 烧录并监视输出
```bash
idf.py flash monitor
```

## 使用说明

### 录制视频和音频

1. 上电后，设备会自动开始录制
2. 录制的文件将保存在 SD 卡根目录下：
   - 视频文件：`HHMM.vid`（例如：`0000.vid`）
   - 音频文件：`HHMM.pcm`（例如：`0000.pcm`）
3. 录制完成后，设备会显示文件信息和录制统计

### 获取录制文件

有两种方法可以获取录制的文件：

#### 方法1：直接读取 SD 卡

1. 关闭设备电源
2. 取出 SD 卡
3. 使用读卡器将 SD 卡连接到电脑
4. 从 SD 卡根目录复制文件

#### 方法2：通过串口传输（无需取出 SD 卡）

1. 在监视器中使用 `ls` 命令查看可用文件：
```
esp32> ls
```

2. 使用 `transfer` 命令传输文件：
```
esp32> transfer 0000.vid
```

3. 使用 `receive.py` 脚本保存传输的数据：
```bash
python3 receive.py 0000.vid
# 粘贴从 ESP32 输出的十六进制数据
# 按 Ctrl+D 结束输入
```

4. 对音频文件重复相同步骤：
```
esp32> transfer 0000.pcm
python3 receive.py 0000.pcm
```

### 转换文件格式

使用提供的 `convert.sh` 脚本将录制的文件转换为标准格式：

```bash
# 安装依赖
brew install ffmpeg

# 转换文件
./convert.sh 0000
```

这将生成：
- `0000.mp4`：可以用任何视频播放器播放
- `0000.wav`：可以用任何音频播放器播放

## 文件格式说明

- `.vid`：MJPEG 格式的视频文件
- `.pcm`：16位有符号小端格式的原始音频数据
  - 采样率：16kHz
  - 通道数：1（单声道）

## 故障排除

如果遇到问题：

1. 检查所有硬件连接
2. 确保 SD 卡已正确格式化（FAT32）
3. 检查串口输出中的错误信息
4. 如果文件传输失败，尝试使用读卡器直接读取 SD 卡

## 开发工具

- ESP-IDF v5.x
- Python 3.x
- FFmpeg（用于文件转换）
