# ESP32-S3 Video Recorder

基于 Seeed XIAO ESP32S3 Sense 开发板的视频录制项目。该项目使用板载摄像头捕获视频，并将视频保存到 SD 卡中。

## 硬件要求

- Seeed XIAO ESP32S3 Sense 开发板
- MicroSD 卡

## 引脚配置

### 摄像头引脚 (OV2640)

| 功能  | GPIO |
|-------|------|
| PWDN  | -1   |
| RESET | -1   |
| XCLK  | 10   |
| SIOD  | 40   |
| SIOC  | 39   |
| D7    | 48   |
| D6    | 11   |
| D5    | 12   |
| D4    | 14   |
| D3    | 16   |
| D2    | 18   |
| D1    | 17   |
| D0    | 15   |
| VSYNC | 38   |
| HREF  | 47   |
| PCLK  | 13   |

### SD 卡引脚 (SPI 模式)

| 功能   | GPIO |
|--------|------|
| MOSI   | 21   |
| MISO   | 20   |
| SCK    | 19   |
| CS     | 18   |

## 配置说明

### 摄像头配置

- 图像尺寸：VGA (640x480)
- 帧缓冲：使用 DRAM，大小为 16KB
- JPEG 压缩质量：12

### SD 卡配置

- 使用 SPI 模式
- 文件系统：FAT
- 最大文件数：5
- 分配单元大小：16KB

### PSRAM 配置

- 模式：OCT (8 线)
- 速度：80MHz
- 自动初始化

## 构建和烧录

项目包含以下脚本：

- `build.sh`: 编译项目
- `flash.sh`: 烧录程序到开发板
- `monitor.sh`: 查看串口输出
- `stop_monitor.sh`: 停止串口监视

使用方法：

```bash
# 编译项目
./build.sh

# 烧录并监视输出
./flash.sh

# 仅监视输出
./monitor.sh

# 停止监视
./stop_monitor.sh
```

## 文件结构

- `main/`
  - `main.c`: 主程序
  - `camera_pins.h`: 摄像头引脚定义
  - `CMakeLists.txt`: 主程序构建配置
- `components/`
  - `esp32-camera/`: 摄像头驱动组件
- `CMakeLists.txt`: 项目构建配置
- `partitions.csv`: 分区表配置
- `sdkconfig`: ESP-IDF 配置文件

## 依赖

- ESP-IDF v5.0 或更高版本
- esp32-camera 组件

## 注意事项

1. SD 卡需要格式化为 FAT32 文件系统
2. 所有 SPI 引脚都启用了内部上拉电阻
3. 视频文件保存在 SD 卡根目录，格式为 `video_[timestamp].mp4`

## 故障排除

1. 如果 SD 卡初始化失败：
   - 确保 SD 卡已正确插入
   - 检查 SD 卡是否为 FAT32 格式
   - 确认 SPI 引脚连接正确

2. 如果摄像头初始化失败：
   - 检查摄像头排线是否正确连接
   - 验证摄像头型号是否为 OV2640

## 许可

[添加许可信息]
