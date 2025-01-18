#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "sdcard_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char* TAG = "sdcard_test";

// Pin assignments for XIAO ESP32S3
#define PIN_NUM_MISO  8
#define PIN_NUM_MOSI  9
#define PIN_NUM_CLK   7
#define PIN_NUM_CS    21

// Test buffer size (512 bytes = 1 sector)
#define TEST_BUFFER_SIZE SDCARD_BLOCK_SIZE

void app_main(void)
{
    ESP_LOGI(TAG, "Starting SD card test");

    // Initialize buffer for testing
    uint8_t write_buffer[TEST_BUFFER_SIZE];
    uint8_t read_buffer[TEST_BUFFER_SIZE];
    for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
        write_buffer[i] = i & 0xFF;
    }

    // Configure SD card
    sdcard_config_t config = {
        .host = SPI2_HOST,
        .pin_mosi = PIN_NUM_MOSI,
        .pin_miso = PIN_NUM_MISO,
        .pin_sck = PIN_NUM_CLK,
        .pin_cs = PIN_NUM_CS,
        .freq_khz = 40000,  // 40MHz
    };

    // Initialize card
    sdcard_t* card = NULL;
    esp_err_t ret = sdcard_init(&config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        return;
    }

    // Get card info
    sdcard_info_t info;
    ret = sdcard_get_info(card, &info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get card info");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Card initialized successfully");
    ESP_LOGI(TAG, "Card type: %s", 
        info.type == CARD_MMC ? "MMC" :
        info.type == CARD_SD ? "SD" :
        info.type == CARD_SDHC ? "SDHC" : "Unknown");
    ESP_LOGI(TAG, "Card size: %llu bytes", info.capacity_bytes);

    // Write test
    ESP_LOGI(TAG, "Writing sector 0");
    ret = sdcard_write_blocks(card, 0, 1, write_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write failed");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Write successful");

    // Read test
    ESP_LOGI(TAG, "Reading sector 0");
    ret = sdcard_read_blocks(card, 0, 1, read_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Read successful");

    // Compare buffers
    if (memcmp(write_buffer, read_buffer, TEST_BUFFER_SIZE) != 0) {
        ESP_LOGE(TAG, "Data verification failed!");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Data verification successful!");

cleanup:
    if (card) {
        sdcard_deinit(card);
    }
}
