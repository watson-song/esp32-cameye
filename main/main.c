#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "driver/i2s_std.h"
#include "esp_vfs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "camera_pins.h"
#include "fs_hal.h"

static const char *TAG = "video_recorder";

// Mount point configuration
#define MOUNT_POINT "/sdcard"
static const char mount_point[] = MOUNT_POINT;

// I2S PDM microphone configuration
#define I2S_SAMPLE_RATE     (44100)
#define I2S_CHANNEL_NUM     (2)
#define I2S_BITS_PER_SAMPLE (16)
#define DMA_BUFFER_COUNT    (8)
#define DMA_BUFFER_LEN      (1024)

// Pin assignments for XIAO ESP32S3
#define PIN_NUM_MISO  8
#define PIN_NUM_MOSI  9
#define PIN_NUM_CLK   7
#define PIN_NUM_CS    21

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
    .frame_size = FRAMESIZE_VGA,     // 降低分辨率
    .jpeg_quality = 12,
    .fb_count = 1,                   // 减少缓冲区数量
    .fb_location = CAMERA_FB_IN_DRAM,// 使用 DRAM 而不是 PSRAM
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    
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

    // 降低初始设置，提高兼容性
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_quality(s, 15);
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 1);
    s->set_sharpness(s, 1);

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

static esp_err_t init_i2s(void)
{
    // I2S configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS_PER_SAMPLE, I2S_CHANNEL_NUM),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_41,
            .ws = GPIO_NUM_42,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_2,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_LOGI(TAG, "Initializing I2S");
    ESP_LOGI(TAG, "BCLK: %d, WS: %d, DIN: %d", 
             std_cfg.gpio_cfg.bclk, std_cfg.gpio_cfg.ws, std_cfg.gpio_cfg.din);

    // Initialize I2S channel
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &i2s_handle));
    
    // Initialize I2S standard mode
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_handle, &std_cfg));
    
    // Start I2S channel
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_handle));

    ESP_LOGI(TAG, "I2S initialized successfully");
    return ESP_OK;
}

void record_video() {
    // 检查文件系统是否已挂载
    fs_info_t fs_info;
    esp_err_t ret = fs_get_info(&fs_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get filesystem info (err: %d)", ret);
        return;
    }

    // 检查剩余空间
    if (fs_info.free_bytes < (1024 * 1024)) {  // 至少需要1MB空间
        ESP_LOGE(TAG, "Not enough space on filesystem (free: %llu bytes)", fs_info.free_bytes);
        return;
    }

    // 检查目录状态
    fs_file_info_t dir_info;
    ESP_LOGI(TAG, "Checking directory status for %s", MOUNT_POINT);
    if (fs_stat(MOUNT_POINT, &dir_info) != ESP_OK) {
        ESP_LOGI(TAG, "Directory %s does not exist, creating...", MOUNT_POINT);
        if (fs_mkdir(MOUNT_POINT) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create directory %s", MOUNT_POINT);
            return;
        }
        // 重新获取目录状态
        if (fs_stat(MOUNT_POINT, &dir_info) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get directory status after creation");
            return;
        }
    }

    // 检查是否是目录
    if (!dir_info.is_directory) {
        ESP_LOGE(TAG, "%s is not a directory", MOUNT_POINT);
        return;
    }

    // 尝试在目录中创建一个测试文件
    char test_file[64];
    snprintf(test_file, sizeof(test_file), MOUNT_POINT "/.test_write");
    ESP_LOGI(TAG, "Attempting to create test file: %s", test_file);
    
    // 先检查文件是否已存在
    if (fs_exists(test_file)) {
        ESP_LOGI(TAG, "Test file already exists, attempting to delete");
        if (fs_remove(test_file) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete existing test file");
            return;
        }
    }

    fs_file_t test_fp = fs_open(test_file, FS_FILE_WRITE);
    if (test_fp == NULL) {
        ESP_LOGE(TAG, "Directory %s is not writable", MOUNT_POINT);
        return;
    }
    
    ESP_LOGI(TAG, "Successfully created test file");
    fs_close(test_fp);
    
    if (fs_remove(test_file) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to delete test file, but continuing...");
    } else {
        ESP_LOGI(TAG, "Successfully deleted test file");
    }

    // 设置时区为中国标准时间
    setenv("TZ", "CST-8", 1);
    tzset();

    char filename[64];
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // 使用更具可读性的时间戳命名文件
    snprintf(filename, sizeof(filename), MOUNT_POINT "/VID_%04d%02d%02d_%02d%02d%02d.mp4",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
             
    ESP_LOGI(TAG, "Starting video recording to: %s", filename);
    
    // 尝试以二进制写入模式打开文件
    fs_file_t fp = fs_open(filename, FS_FILE_WRITE);
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return;
    }

    // 分配视频和音频缓冲区
    size_t audio_buf_len = DMA_BUFFER_LEN * sizeof(int16_t) * I2S_CHANNEL_NUM;
    int16_t* i2s_buffer = heap_caps_malloc(audio_buf_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (i2s_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate I2S buffer");
        fs_close(fp);
        return;
    }

    // 录制参数
    const int RECORD_TIME_SECONDS = 10;
    const int64_t RECORD_TIME_MS = RECORD_TIME_SECONDS * 1000000;
    int64_t fr_start = esp_timer_get_time();
    int frame_count = 0;
    int write_errors = 0;
    const int MAX_WRITE_ERRORS = 5;

    ESP_LOGI(TAG, "Recording %d seconds of video...", RECORD_TIME_SECONDS);

    while ((esp_timer_get_time() - fr_start) < RECORD_TIME_MS) {
        // 获取摄像头帧
        camera_fb_t *pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGE(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 写入视频帧
        size_t bytes_written = fs_write(fp, pic->buf, pic->len);
        if (bytes_written != pic->len) {
            ESP_LOGE(TAG, "Failed to write frame data: written %d of %d bytes", bytes_written, pic->len);
            write_errors++;
            if (write_errors >= MAX_WRITE_ERRORS) {
                ESP_LOGE(TAG, "Too many write errors, stopping recording");
                esp_camera_fb_return(pic);
                break;
            }
        }

        // 读取音频数据
        size_t bytes_read = 0;
        esp_err_t i2s_err = i2s_channel_read(I2S_NUM_0, i2s_buffer, audio_buf_len, &bytes_read, portMAX_DELAY);
        if (i2s_err == ESP_OK && bytes_read > 0) {
            // 写入音频数据
            bytes_written = fs_write(fp, i2s_buffer, bytes_read);
            if (bytes_written != bytes_read) {
                ESP_LOGE(TAG, "Failed to write audio data: written %d of %d bytes", bytes_written, bytes_read);
                write_errors++;
            }
        } else if (i2s_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read I2S data: %d", i2s_err);
        }

        esp_camera_fb_return(pic);
        frame_count++;

        // 每秒打印一次状态
        if (frame_count % 30 == 0) {
            int elapsed_sec = (esp_timer_get_time() - fr_start) / 1000000;
            ESP_LOGI(TAG, "Recording... %d seconds, %d frames captured", elapsed_sec, frame_count);
        }
    }

    // 清理并关闭文件
    free(i2s_buffer);
    fs_close(fp);

    float fps = frame_count / ((float)RECORD_TIME_SECONDS);
    ESP_LOGI(TAG, "Recording finished: %d frames in %d seconds (%.1f fps)", 
             frame_count, RECORD_TIME_SECONDS, fps);
}

void app_main(void)
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
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

    // Initialize peripherals
    ESP_ERROR_CHECK(init_camera());
    ESP_ERROR_CHECK(init_sdcard());
    ESP_ERROR_CHECK(init_i2s());

    // Start recording
    record_video();

    // Clean up
    if (i2s_handle) {
        i2s_channel_disable(i2s_handle);
        i2s_del_channel(i2s_handle);
    }
    fs_deinit();
    ESP_LOGI(TAG, "Cleanup completed");
}
