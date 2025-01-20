#include <string.h>
#include "esp_log.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "sdcard_hal.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

static const char* TAG = "sdcard_hal";

esp_err_t sdspi_card_init(const sdcard_config_t* config, sdcard_t** out_card) {
    esp_err_t ret = ESP_OK;
    sdcard_t* card = NULL;

    ESP_LOGI(TAG, "Starting SD card initialization (SPI mode)");
    ESP_LOGI(TAG, "Using pins - MOSI: %d, MISO: %d, CLK: %d, CS: %d",
             config->pin_mosi, config->pin_miso, config->pin_sck, config->pin_cs);

    // Allocate card structure
    card = (sdcard_t*)heap_caps_calloc(1, sizeof(sdcard_t), MALLOC_CAP_DEFAULT);
    if (card == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    card->host = config->host;
    card->pin_cs = config->pin_cs;  // 保存CS引脚信息

    // Configure GPIO pins with pull-ups
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->pin_cs),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(config->pin_cs, 1);  // CS high initially

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = config->pin_miso,
        .sclk_io_num = config->pin_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    ret = spi_bus_initialize(config->host, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %d", ret);
        goto cleanup;
    }

    // Configure SD SPI device
    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.gpio_cs = config->pin_cs;
    dev_cfg.host_id = config->host;

    ret = sdspi_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SDSPI host: %d", ret);
        goto cleanup;
    }

    sdspi_dev_handle_t handle;
    ret = sdspi_host_init_device(&dev_cfg, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SDSPI device: %d", ret);
        goto cleanup;
    }

    // Initialize SD card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = handle;

    // First initialize at low speed
    host.max_freq_khz = 400;
    
    sdmmc_card_t* sdcard = (sdmmc_card_t*)heap_caps_malloc(sizeof(sdmmc_card_t), MALLOC_CAP_DEFAULT);
    if (!sdcard) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    ret = sdmmc_card_init(&host, sdcard);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card: %d", ret);
        free(sdcard);
        goto cleanup;
    }

    // Switch to full speed
    host.max_freq_khz = config->freq_khz;
    ret = sdspi_host_set_card_clk(host.slot, MIN(config->freq_khz, host.max_freq_khz));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set card clock: %d", ret);
        free(sdcard);
        goto cleanup;
    }

    // Store card info
    card->type = (sdcard->ocr & (1ULL << 30)) ? CARD_SDHC : CARD_SD;
    card->sectors = sdcard->csd.capacity;
    card->spi = handle;
    card->sdcard = sdcard;  // Keep the sdcard structure for read/write operations

    ESP_LOGI(TAG, "Card initialized at %d kHz", config->freq_khz);
    ESP_LOGI(TAG, "Card type: %s", card->type == CARD_SDHC ? "SDHC" : "SD");
    ESP_LOGI(TAG, "Card size: %lu sectors", card->sectors);

    *out_card = card;
    return ESP_OK;

cleanup:
    if (card) {
        if (card->spi) {
            sdspi_host_remove_device(card->spi);
            spi_bus_free(card->host);
        }
        if (card->sdcard) {
            free(card->sdcard);
        }
        free(card);
    }
    return ret;
}

esp_err_t sdcard_read_blocks(sdcard_t* card, size_t start_block, size_t n_blocks, void* dst) {
    if (!card || !dst) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = sdmmc_read_sectors(card->sdcard, dst, start_block, n_blocks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read sectors: %d", ret);
    }
    return ret;
}

esp_err_t sdcard_write_blocks(sdcard_t* card, size_t start_block, size_t n_blocks, const void* src) {
    if (!card || !src) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = sdmmc_write_sectors(card->sdcard, src, start_block, n_blocks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write sectors: %d", ret);
    }
    return ret;
}

esp_err_t sdcard_get_info(sdcard_t* card, sdcard_info_t* out_info) {
    if (!card || !out_info) {
        return ESP_ERR_INVALID_ARG;
    }

    out_info->type = card->type;
    out_info->capacity_bytes = card->sectors * SDCARD_BLOCK_SIZE;
    return ESP_OK;
}

esp_err_t sdspi_card_deinit(sdcard_t* card) {
    if (!card) return ESP_ERR_INVALID_ARG;
    
    // 先释放SD卡结构
    if (card->sdcard) {
        // 不需要显式释放sdcard结构，它会在sdspi_host_remove_device中被释放
        card->sdcard = NULL;
    }

    // 移除SPI设备并释放总线
    if (card->spi) {
        sdspi_host_remove_device(card->spi);
        spi_bus_free(card->host);
    }

    // 最后释放card结构体
    free(card);
    return ESP_OK;
}
