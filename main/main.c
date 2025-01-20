#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "camera_pins.h"

static const char *TAG = "video_recorder";

// Mount point configuration
#define MOUNT_POINT "/sdcard"

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
    .frame_size = FRAMESIZE_QVGA,    // 使用较小的分辨率
    .jpeg_quality = 12,              // 较低的质量设置
    .fb_count = 1,                   // 减少帧缓冲区数量
    .fb_location = CAMERA_FB_IN_DRAM,// 使用 DRAM 而不是 PSRAM
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

// I2S PDM configuration
static i2s_chan_handle_t i2s_handle = NULL;
#define AUDIO_BUFFER_SIZE (DMA_BUFFER_LEN * 2)  // 每个采样16位
static uint8_t audio_buffer[AUDIO_BUFFER_SIZE];

static esp_err_t init_sdcard(void)
{
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SD card");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, NULL);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "Filesystem mounted");

    return ESP_OK;
}

static esp_err_t init_i2s(void)
{
    ESP_LOGI(TAG, "Initializing I2S");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &i2s_handle));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH, I2S_CHANNEL_NUM),
        .gpio_cfg = {
            .clk = I2S_CLK_IO,
            .din = I2S_DIN_IO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(i2s_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_handle));

    return ESP_OK;
}

static void deinit_i2s(void)
{
    if (i2s_handle) {
        i2s_channel_disable(i2s_handle);
        i2s_del_channel(i2s_handle);
        i2s_handle = NULL;
    }
}

static esp_err_t record_audio_chunk(FIL* audio_file)
{
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(i2s_handle, audio_buffer, AUDIO_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        return ret;
    }

    UINT bytes_written;
    FRESULT res = f_write(audio_file, audio_buffer, bytes_read, &bytes_written);
    if (res != FR_OK || bytes_written != bytes_read) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void record_video(void)
{
    char video_path[32];
    char audio_path[32];
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Generate filenames based on current time
    snprintf(video_path, sizeof(video_path), MOUNT_POINT "/%02d%02d.vid",
             timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(audio_path, sizeof(audio_path), MOUNT_POINT "/%02d%02d.pcm",
             timeinfo.tm_hour, timeinfo.tm_min);

    // Check if files already exist
    FILINFO fno;
    if (f_stat(video_path, &fno) == FR_OK) {
        ESP_LOGE(TAG, "Video file already exists: %s", video_path);
        return;
    }
    if (f_stat(audio_path, &fno) == FR_OK) {
        ESP_LOGE(TAG, "Audio file already exists: %s", audio_path);
        return;
    }

    // Open files for writing
    FIL video_file, audio_file;
    if (f_open(&video_file, video_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        ESP_LOGE(TAG, "Failed to open video file: %s", video_path);
        return;
    }

    if (f_open(&audio_file, audio_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        ESP_LOGE(TAG, "Failed to open audio file: %s", audio_path);
        f_close(&video_file);
        return;
    }

    // Start recording
    ESP_LOGI(TAG, "Starting recording...");
    uint32_t frame_count = 0;
    uint64_t start_time = esp_timer_get_time();
    const uint64_t RECORD_LENGTH = 30 * 1000000; // 30 seconds

    while ((esp_timer_get_time() - start_time) < RECORD_LENGTH) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            continue;
        }

        // Write video frame
        UINT bytes_written;
        FRESULT res = f_write(&video_file, fb->buf, fb->len, &bytes_written);
        if (res != FR_OK || bytes_written != fb->len) {
            ESP_LOGE(TAG, "Failed to write frame data: written %u of %u bytes",
                     bytes_written, fb->len);
        } else {
            frame_count++;
            if (frame_count % 30 == 0) {
                ESP_LOGI(TAG, "Recorded %"PRIu32" frames", frame_count);
            }
        }

        esp_camera_fb_return(fb);

        // Record audio
        if (record_audio_chunk(&audio_file) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to record audio chunk");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Close files
    f_close(&video_file);
    f_close(&audio_file);

    ESP_LOGI(TAG, "Recording finished. Recorded %"PRIu32" frames", frame_count);

    // Get file information
    if (f_stat(video_path, &fno) == FR_OK) {
        ESP_LOGI(TAG, "Video file information:");
        ESP_LOGI(TAG, "- Path: %s", video_path);
        ESP_LOGI(TAG, "- Size: %"PRIu32" bytes", fno.fsize);
    }

    if (f_stat(audio_path, &fno) == FR_OK) {
        ESP_LOGI(TAG, "Audio file information:");
        ESP_LOGI(TAG, "- Path: %s", audio_path);
        ESP_LOGI(TAG, "- Size: %"PRIu32" bytes", fno.fsize);
    }
}

// File transfer command handler
static void handle_transfer_command(const char* file_path)
{
    FIL file;
    FRESULT res = f_open(&file, file_path, FA_READ);
    if (res != FR_OK) {
        printf("Error: Could not open file %s\n", file_path);
        return;
    }

    // Get file size
    FILINFO fno;
    if (f_stat(file_path, &fno) != FR_OK) {
        printf("Error: Could not get file info\n");
        f_close(&file);
        return;
    }

    printf("File size: %"PRIu32" bytes\n", fno.fsize);
    printf("Transfer starting...\n");

    // Transfer file in hex format
    uint8_t buffer[1024];
    UINT bytes_read;
    uint32_t total_bytes = 0;

    while (f_read(&file, buffer, sizeof(buffer), &bytes_read) == FR_OK && bytes_read > 0) {
        for (UINT i = 0; i < bytes_read; i++) {
            printf("%02x", buffer[i]);
        }
        total_bytes += bytes_read;

        // Print progress every 64KB
        if (total_bytes % (64 * 1024) == 0) {
            printf("\nTransferred: %"PRIu32" bytes (%.1f%%)\n",
                   total_bytes, (total_bytes * 100.0f) / fno.fsize);
        }
    }

    printf("\nTransfer complete: %"PRIu32" bytes transferred\n", total_bytes);
    f_close(&file);
}

// Console command handler
static int console_handler(int argc, char **argv)
{
    if (argc < 1) {
        return 0;
    }

    if (strcmp(argv[0], "record") == 0) {
        record_video();
    } else if (strcmp(argv[0], "transfer") == 0) {
        if (argc != 2) {
            printf("Usage: transfer <filename>\n");
            return 0;
        }
        handle_transfer_command(argv[1]);
    } else if (strcmp(argv[0], "ls") == 0) {
        // List all files in root directory
        FF_DIR dir;
        FILINFO fno;
        FRESULT res = f_opendir(&dir, "/");
        if (res != FR_OK) {
            printf("Error: Could not open root directory\n");
            return 0;
        }

        printf("Files in root directory:\n");
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            if (!(fno.fattrib & AM_DIR)) {
                printf("%s\t%"PRIu32" bytes\n", fno.fname, fno.fsize);
            }
        }
        f_closedir(&dir);
    }

    return 0;
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize camera
    ESP_ERROR_CHECK(esp_camera_init(&camera_config));
    ESP_LOGI(TAG, "Camera initialized");

    // Initialize I2S for audio recording
    ESP_ERROR_CHECK(init_i2s());
    ESP_LOGI(TAG, "I2S initialized");

    // Initialize SD card
    ESP_ERROR_CHECK(init_sdcard());
    ESP_LOGI(TAG, "SD card initialized");

    // Initialize console
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32> ";
    repl_config.max_cmdline_length = 64;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    esp_console_cmd_t cmd = {
        .command = "record",
        .help = "Start recording video and audio",
        .hint = NULL,
        .func = &console_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "transfer";
    cmd.help = "Transfer a file in hex format";
    cmd.hint = "<filename>";
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "ls";
    cmd.help = "List files in root directory";
    cmd.hint = NULL;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    // 在程序退出时调用此函数
    atexit(deinit_i2s);
}
