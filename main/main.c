#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "driver/i2s_std.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "camera_pins.h"

static const char *TAG = "video_recorder";

// SD card configuration
#define MOUNT_POINT "/sdcard"
sdmmc_card_t *card;
const char mount_point[] = MOUNT_POINT;

// I2S PDM microphone configuration
#define I2S_SAMPLE_RATE     (44100)
#define I2S_CHANNEL_NUM     (2)
#define I2S_BITS_PER_SAMPLE (16)
#define DMA_BUFFER_COUNT    (8)
#define DMA_BUFFER_LEN      (1024)

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
    esp_err_t ret;

    // Options for mounting the filesystem.
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SD card");

    // XIAO ESP32S3 Sense SD Card Pins
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 21,    // SD Card MOSI
        .miso_io_num = 20,    // SD Card MISO
        .sclk_io_num = 19,    // SD Card SCK
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // 为所有 SPI 引脚启用上拉
    gpio_set_pull_mode(21, GPIO_PULLUP_ONLY);    // CMD
    gpio_set_pull_mode(20, GPIO_PULLUP_ONLY);    // D0
    gpio_set_pull_mode(19, GPIO_PULLUP_ONLY);    // CLK
    gpio_set_pull_mode(18, GPIO_PULLUP_ONLY);    // CS

    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = 18;     // SD Card CS
    slot_config.host_id = SPI2_HOST;

    // 使用较低的频率进行初始化
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SDMMC_FREQ_PROBING;  // 使用探测频率

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pullup resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }

    // 初始化成功后，提高频率
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

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

    // Initialize I2S channel
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, NULL));
    
    // Initialize I2S standard mode
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(I2S_NUM_0, &std_cfg));
    
    // Start I2S channel
    ESP_ERROR_CHECK(i2s_channel_enable(I2S_NUM_0));

    return ESP_OK;
}

void record_video() {
    char filename[64];
    int64_t time_ms = esp_timer_get_time() / 1000000;
    sprintf(filename, "/sdcard/video_%lld.mp4", time_ms);
    FILE* fp = fopen(filename, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }

    camera_fb_t *pic = NULL;
    size_t buf_len = DMA_BUFFER_LEN * sizeof(int16_t) * I2S_CHANNEL_NUM;
    int16_t* i2s_buffer = malloc(buf_len);
    if (i2s_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate I2S buffer");
        fclose(fp);
        return;
    }

    // Record for 10 seconds
    int64_t fr_start = esp_timer_get_time();
    int frame_count = 0;

    while ((esp_timer_get_time() - fr_start) < 10000000) {
        pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGE(TAG, "Camera capture failed");
            continue;
        }

        // Write video frame
        if (fwrite(pic->buf, 1, pic->len, fp) != pic->len) {
            ESP_LOGE(TAG, "Failed to write frame data to file");
        }

        // Read audio data
        size_t bytes_read = 0;
        ESP_ERROR_CHECK(i2s_channel_read(I2S_NUM_0, i2s_buffer, buf_len, &bytes_read, portMAX_DELAY));
        
        // Write audio data
        if (fwrite(i2s_buffer, 1, bytes_read, fp) != bytes_read) {
            ESP_LOGE(TAG, "Failed to write audio data to file");
        }

        esp_camera_fb_return(pic);
        frame_count++;
    }

    free(i2s_buffer);
    fclose(fp);

    ESP_LOGI(TAG, "Recorded %d frames", frame_count);
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

    // Initialize peripherals
    ESP_ERROR_CHECK(init_camera());
    ESP_ERROR_CHECK(init_sdcard());
    ESP_ERROR_CHECK(init_i2s());

    // Start recording
    record_video();

    // Clean up
    ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount(mount_point, card));
    ESP_LOGI(TAG, "Card unmounted");
}
