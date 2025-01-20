#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2s_pdm.h"
#include "esp_camera.h"
#include "driver/uart.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include "fs_hal.h"
#include "camera_pins.h"

static const char *TAG = "video_recorder";

// Mount point configuration
#define MOUNT_POINT "/sdcard"
static const char mount_point[] = MOUNT_POINT;

// Pin assignments for XIAO ESP32S3 Sense
#define PIN_NUM_MISO  8
#define PIN_NUM_MOSI  9
#define PIN_NUM_CLK   7
#define PIN_NUM_CS    21

// I2S PDM配置
#define I2S_CLK_IO      41  // PDM Clock
#define I2S_DIN_IO      42  // PDM Data Input
#define I2S_SAMPLE_RATE 16000
#define I2S_CHANNEL_NUM 1   // PDM通常是单声道
#define I2S_DATA_BIT_WIDTH I2S_DATA_BIT_WIDTH_16BIT

// DMA配置
#define DMA_BUFFER_COUNT    8
#define DMA_BUFFER_LEN      1024

// Camera configuration
static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM,
    .pin_sccb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,    // 使用更小的分辨率
    .jpeg_quality = 15,              // 提高质量以减少压缩时间
    .fb_count = 2,                   // 增加缓冲区数量
    .fb_location = CAMERA_FB_IN_PSRAM,// 使用 PSRAM
    .grab_mode = CAMERA_GRAB_LATEST,  // 总是获取最新的帧
    
    .sccb_i2c_port = 1
};

static esp_err_t init_camera(void)
{
    //initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed with error 0x%x", err);
        return err;
    }

    sensor_t * s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get sensor");
        return ESP_FAIL;
    }

    // 使用与配置一致的设置
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 1);
    s->set_sharpness(s, 1);
    s->set_gainceiling(s, GAINCEILING_2X);  // 增加增益
    s->set_exposure_ctrl(s, 1);             // 启用自动曝光
    s->set_aec2(s, 1);                      // 启用高级自动曝光
    s->set_gain_ctrl(s, 1);                 // 启用自动增益
    s->set_awb_gain(s, 1);                  // 启用自动白平衡增益

    ESP_LOGI(TAG, "Camera Init Success");
    return ESP_OK;
}

static esp_err_t init_sdcard(void)
{
    // 配置文件系统
    fs_config_t fs_config = {
        .mount_point = mount_point,
        .max_files = 5,
        .format_if_mount_failed = false,
        .sdcard = {
            .host = SPI2_HOST,
            .pin_mosi = PIN_NUM_MOSI,
            .pin_miso = PIN_NUM_MISO,
            .pin_sck = PIN_NUM_CLK,
            .pin_cs = PIN_NUM_CS,
            .freq_khz = 40000    // 40MHz
        }
    };

    ESP_LOGI(TAG, "Initializing filesystem");
    
    // 初始化文件系统（包含SD卡初始化）
    esp_err_t ret = fs_init(&fs_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize filesystem");
        return ret;
    }

    // 获取并显示文件系统信息
    fs_info_t fs_info;
    ret = fs_get_info(&fs_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Filesystem info:");
        ESP_LOGI(TAG, "- Total space: %llu bytes", fs_info.total_bytes);
        ESP_LOGI(TAG, "- Used space: %llu bytes", fs_info.used_bytes);
        ESP_LOGI(TAG, "- Free space: %llu bytes", fs_info.free_bytes);
    }

    ESP_LOGI(TAG, "Filesystem initialized successfully");
    return ESP_OK;
}

static i2s_chan_handle_t i2s_handle = NULL;

static esp_err_t init_i2s(void) {
    ESP_LOGI(TAG, "Initializing I2S PDM...");
    
    // Log GPIO configuration
    ESP_LOGI(TAG, "CLK: %d, DIN: %d", I2S_CLK_IO, I2S_DIN_IO);
    
    // Initialize I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_BUFFER_COUNT;
    chan_cfg.dma_frame_num = DMA_BUFFER_LEN;
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &i2s_handle));
    
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH, I2S_CHANNEL_NUM),
        .gpio_cfg = {
            .clk = I2S_CLK_IO,
            .din = I2S_DIN_IO,
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(i2s_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_handle));
    
    ESP_LOGI(TAG, "I2S PDM initialized successfully");
    return ESP_OK;
}

static void deinit_i2s(void) {
    if (i2s_handle) {
        i2s_channel_disable(i2s_handle);
        i2s_del_channel(i2s_handle);
        i2s_handle = NULL;
    }
}

#define AUDIO_BUFFER_SIZE (DMA_BUFFER_LEN * 2)  // 每个采样16位
static uint8_t audio_buffer[AUDIO_BUFFER_SIZE];

static esp_err_t record_audio_chunk(fs_file_t audio_file) {
    if (!i2s_handle || !audio_file) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(i2s_handle, audio_buffer, AUDIO_BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read I2S data: %d", ret);
        return ret;
    }

    if (bytes_read > 0) {
        int bytes_written = fs_write(audio_file, audio_buffer, bytes_read);
        if (bytes_written != bytes_read) {
            ESP_LOGE(TAG, "Failed to write audio data to file: %d != %d", bytes_written, bytes_read);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

void record_video() {
    // 检查文件系统是否已挂载
    fs_info_t fs_info;
    if (fs_get_info(&fs_info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get filesystem info");
        return;
    }

    // 创建录制文件
    char video_path[32];
    char audio_path[32];
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // 使用更短的文件名格式：HHMM.xxx
    snprintf(video_path, sizeof(video_path), "%02d%02d.vid",
             timeinfo.tm_hour, timeinfo.tm_min);
             
    snprintf(audio_path, sizeof(audio_path), "%02d%02d.pcm",
             timeinfo.tm_hour, timeinfo.tm_min);

    // 检查文件是否已存在
    if (fs_exists(video_path)) {
        ESP_LOGE(TAG, "Video file already exists: %s", video_path);
        return;
    }
    if (fs_exists(audio_path)) {
        ESP_LOGE(TAG, "Audio file already exists: %s", audio_path);
        return;
    }

    // 打开视频文件
    fs_file_t video_file = fs_open(video_path, FS_FILE_WRITE);
    if (video_file == NULL) {
        ESP_LOGE(TAG, "Failed to open video file for writing: %s", video_path);
        return;
    }

    // 打开音频文件
    fs_file_t audio_file = fs_open(audio_path, FS_FILE_WRITE);
    if (audio_file == NULL) {
        ESP_LOGE(TAG, "Failed to open audio file for writing: %s", audio_path);
        fs_close(video_file);
        return;
    }

    ESP_LOGI(TAG, "Started recording to:");
    ESP_LOGI(TAG, "Video: %s", video_path);
    ESP_LOGI(TAG, "Audio: %s", audio_path);

    uint32_t frame_count = 0;
    uint64_t start_time = esp_timer_get_time();
    const uint64_t RECORD_TIME_US = 30 * 1000 * 1000; // 30 seconds

    // 初始化摄像头
    camera_fb_t *fb = NULL;
    ESP_LOGI(TAG, "Starting capture loop...");

    while ((esp_timer_get_time() - start_time) < RECORD_TIME_US) {
        // 捕获视频帧
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Failed to capture frame");
            continue;
        }

        // 写入视频数据
        int written = fs_write(video_file, fb->buf, fb->len);
        if (written != fb->len) {
            ESP_LOGE(TAG, "Failed to write frame data: written %d of %d bytes", written, fb->len);
        } else {
            frame_count++;
            if (frame_count % 30 == 0) {
                ESP_LOGI(TAG, "Recorded %" PRIu32 " frames", frame_count);
            }
        }

        // 释放帧缓冲区
        esp_camera_fb_return(fb);

        // 录制音频
        if (record_audio_chunk(audio_file) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to record audio chunk");
        }

        // 给其他任务一些执行时间
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 录制完成，关闭文件
    fs_close(video_file);
    fs_close(audio_file);

    ESP_LOGI(TAG, "Recording finished. Recorded %" PRIu32 " frames", frame_count);

    // 获取并打印文件信息
    fs_file_info_t video_info;
    fs_file_info_t audio_info;
    
    if (fs_stat(video_path, &video_info) == ESP_OK) {
        struct tm timeinfo;
        time_t modified_time = (time_t)video_info.last_modified;
        localtime_r(&modified_time, &timeinfo);
        
        ESP_LOGI(TAG, "Video file information:");
        ESP_LOGI(TAG, "- Path: %s", video_path);
        ESP_LOGI(TAG, "- Size: %" PRIu64 " bytes", video_info.size);
        ESP_LOGI(TAG, "- Created: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    
    if (fs_stat(audio_path, &audio_info) == ESP_OK) {
        struct tm timeinfo;
        time_t modified_time = (time_t)audio_info.last_modified;
        localtime_r(&modified_time, &timeinfo);
        
        ESP_LOGI(TAG, "Audio file information:");
        ESP_LOGI(TAG, "- Path: %s", audio_path);
        ESP_LOGI(TAG, "- Size: %" PRIu64 " bytes", audio_info.size);
        ESP_LOGI(TAG, "- Created: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    // 计算平均帧率
    float elapsed_time = (float)(esp_timer_get_time() - start_time) / 1000000.0f; // 转换为秒
    float fps = frame_count / elapsed_time;
    ESP_LOGI(TAG, "Recording statistics:");
    ESP_LOGI(TAG, "- Duration: %.2f seconds", elapsed_time);
    ESP_LOGI(TAG, "- Average FPS: %.2f", fps);
    ESP_LOGI(TAG, "- Total frames: %" PRIu32, frame_count);

    ESP_LOGI(TAG, "Video saved to: %s", video_path);
    ESP_LOGI(TAG, "Audio saved to: %s", audio_path);
}

// 添加文件传输命令处理函数
static void handle_transfer_command(const char* file_path) {
    fs_file_t file = fs_open(file_path, FS_FILE_READ);
    if (file == NULL) {
        printf("Error: Could not open file %s\n", file_path);
        return;
    }

    // 打印文件大小
    fs_file_info_t info;
    if (fs_stat(file_path, &info) != ESP_OK) {
        printf("Error: Could not get file info\n");
        fs_close(file);
        return;
    }
    printf("File size: %" PRIu64 " bytes\n", info.size);
    printf("Transfer starting...\n");

    // 以十六进制格式传输文件内容
    uint8_t buffer[1024];
    size_t bytes_read;
    uint32_t total_bytes = 0;

    while ((bytes_read = fs_read(file, buffer, sizeof(buffer))) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            printf("%02x", buffer[i]);
        }
        total_bytes += bytes_read;
        
        // 每传输64KB打印一次进度
        if (total_bytes % (64 * 1024) == 0) {
            printf("\nTransferred: %u bytes (%.1f%%)\n", 
                   total_bytes, (total_bytes * 100.0f) / info.size);
        }
    }

    printf("\nTransfer complete: %u bytes transferred\n", total_bytes);
    fs_close(file);
}

// 添加命令处理函数
static int console_handler(int argc, char **argv) {
    if (argc < 1) {
        return 0;
    }

    if (strcmp(argv[0], "transfer") == 0) {
        if (argc != 2) {
            printf("Usage: transfer <filename>\n");
            printf("Example: transfer 0000.vid\n");
            return 0;
        }
        handle_transfer_command(argv[1]);
    } else if (strcmp(argv[0], "ls") == 0) {
        // 列出根目录下的所有文件
        fs_dir_iterator_t it;
        if (fs_opendir("/", &it) != ESP_OK) {
            printf("Error: Could not open root directory\n");
            return 0;
        }

        fs_file_info_t info;
        while (fs_readdir(it, &info) == ESP_OK && info.name[0] != '\0') {
            if (!info.is_directory) {
                printf("%s\t%" PRIu64 " bytes\n", info.name, info.size);
            }
        }
        fs_closedir(it);
    }

    return 0;
}

void app_main(void)
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 设置系统时间（这里设置一个固定的时间作为示例）
    struct timeval tv = {
        .tv_sec = 1705622400,  // 2024-01-19 00:00:00
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);

    // 初始化硬件
    ESP_ERROR_CHECK(init_camera());
    ESP_ERROR_CHECK(init_sdcard());
    ESP_ERROR_CHECK(init_i2s());

    // 初始化控制台
    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 8,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    // 初始化 UART 设备
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    const uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

    // 注册命令
    esp_console_cmd_t cmd = {
        .command = "transfer",
        .help = "Transfer file content. Usage: transfer <filename>",
        .hint = NULL,
        .func = &console_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "ls";
    cmd.help = "List files in root directory";
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    // 启动控制台
    linenoiseSetMultiLine(1);
    linenoiseSetDumbMode(1);

    printf("\n"
           "Type 'ls' to list files\n"
           "Type 'transfer <filename>' to transfer a file\n"
           "\n");

    // 主循环
    while(1) {
        char* line = linenoise("esp32> ");
        if (line == NULL) {
            continue;
        }
        
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            
            int ret;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Command not found\n");
            } else if (err == ESP_ERR_INVALID_ARG) {
                printf("Invalid arguments\n");
            } else if (err == ESP_OK && ret != ESP_OK) {
                printf("Command returned non-zero error code: 0x%x\n", ret);
            }
        }
        linenoiseFree(line);
    }

    // 开始录制
    record_video();
}
