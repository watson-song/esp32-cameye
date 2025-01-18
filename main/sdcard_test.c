#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "sdcard_test";

// Pin assignments
// Pin assignments for XIAO ESP32S3
#define PIN_NUM_MISO  8
#define PIN_NUM_MOSI  9
#define PIN_NUM_CLK   7
#define PIN_NUM_CS    21

// DMA channel to be used by the SPI peripheral
#ifndef SPI_DMA_CHAN
#define SPI_DMA_CHAN    SPI_DMA_CH_AUTO
#endif

static void init_gpio(void)
{
    ESP_LOGI(TAG, "Initializing GPIOs");
    
    // Configure CS GPIO
    gpio_config_t cs_cfg = {
        .pin_bit_mask = BIT64(PIN_NUM_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cs_cfg));
    
    // Set CS high
    gpio_set_level(PIN_NUM_CS, 1);
    
    // Configure other pins with pull-ups
    gpio_set_pull_mode(PIN_NUM_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_NUM_CLK, GPIO_PULLUP_ONLY);
    
    // Let pins settle
    vTaskDelay(pdMS_TO_TICKS(100));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Using SPI peripheral");

    // Initialize GPIOs first
    init_gpio();

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    // Initialize SPI bus
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CHAN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // Configure SD Card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 400; // Start with very low frequency

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Mount filesystem
    sdmmc_card_t* card;
    const char mount_point[] = "/sdcard";
    ESP_LOGI(TAG, "Mounting filesystem");
    
    for (int retry = 0; retry < 3; retry++) {
        ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "Card mount failed (0x%x), retrying...", ret);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s)", esp_err_to_name(ret));
            ESP_LOGE(TAG, "Make sure SD card lines have pull-up resistors in place.");
        }
        spi_bus_free(SPI2_HOST);
        return;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // Try writing to the card
    const char* file_path = "/sdcard/test.txt";
    ESP_LOGI(TAG, "Opening file %s", file_path);
    FILE* f = fopen(file_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        spi_bus_free(SPI2_HOST);
        return;
    }
    fprintf(f, "Hello SD Card!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Try reading from the card
    ESP_LOGI(TAG, "Reading file %s", file_path);
    f = fopen(file_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        spi_bus_free(SPI2_HOST);
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    ESP_LOGI(TAG, "Read from file: %s", line);

    // All done, unmount partition and disable SPI peripheral
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");
    spi_bus_free(SPI2_HOST);
}
