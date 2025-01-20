#pragma once

// XIAO ESP32S3 Sense Camera Pins
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    10
#define SIOD_GPIO_NUM    40
#define SIOC_GPIO_NUM    39

#define Y9_GPIO_NUM      48
#define Y8_GPIO_NUM      11
#define Y7_GPIO_NUM      12
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      16
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM      17
#define Y2_GPIO_NUM      15
#define VSYNC_GPIO_NUM   38
#define HREF_GPIO_NUM    47
#define PCLK_GPIO_NUM    13

// SD Card SPI Pins
#define PIN_NUM_MISO     8
#define PIN_NUM_MOSI     9
#define PIN_NUM_CLK      7
#define PIN_NUM_CS       21

// PDM Microphone Pins
#define I2S_CLK_IO       41  // PDM Clock Output
#define I2S_DIN_IO       42  // PDM Data Input

// Audio Recording Parameters
#define I2S_PORT         I2S_NUM_0
#define I2S_SAMPLE_RATE  16000
#define I2S_CHANNEL_NUM  1
#define I2S_DATA_BIT_WIDTH I2S_DATA_BIT_WIDTH_16BIT

// Mount point for the filesystem
#define MOUNT_POINT "/sdcard"