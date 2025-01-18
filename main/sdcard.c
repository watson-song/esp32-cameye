#include "sdcard.h"
#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"

static const char* TAG = "sdcard";

// Send command to SD card
static esp_err_t sdcard_cmd(sdcard_t* card, sdcard_cmd_t cmd, uint32_t arg, uint32_t* response) {
    uint8_t cmd_buffer[7];  // Extra byte for dummy
    esp_err_t ret;

    // Try command up to 3 times
    for (int retry = 0; retry < 3; retry++) {
        // Prepare command
        cmd_buffer[0] = 0x40 | (cmd & 0x3F);  // Start bit (0) + Transmission bit (1) + Command
        cmd_buffer[1] = (arg >> 24) & 0xFF;   // Argument[31:24]
        cmd_buffer[2] = (arg >> 16) & 0xFF;   // Argument[23:16]
        cmd_buffer[3] = (arg >> 8) & 0xFF;    // Argument[15:8]
        cmd_buffer[4] = arg & 0xFF;           // Argument[7:0]
        
        // Use CRC for CMD0 and CMD8, or if CRC is enabled
        if (card->supports_crc || cmd == CMD_GO_IDLE_STATE || cmd == CMD_SEND_IF_COND) {
            cmd_buffer[5] = 0x95;  // CRC7 + end bit (valid for CMD0)
        } else {
            cmd_buffer[5] = 0x01;  // Just end bit
        }
        cmd_buffer[6] = 0xFF;  // Extra byte

        // Select card
        gpio_set_level(card->config.cs_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(1));

        // Send command
        for (int i = 0; i < 7; i++) {  // Send all 7 bytes including dummy
            spi_transaction_t t = {
                .flags = SPI_TRANS_USE_TXDATA,
                .length = 8,
                .tx_data = {cmd_buffer[i]},
            };
            ret = spi_device_transmit(card->spi, &t);
            if (ret != ESP_OK) {
                gpio_set_level(card->config.cs_pin, 1);
                return ret;
            }
        }

        // Wait for response (up to 9 bytes)
        uint8_t token = 0xFF;
        for (int i = 0; i < 9; i++) {
            spi_transaction_t t = {
                .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
                .length = 8,
                .tx_data = {0xFF},
            };
            ret = spi_device_transmit(card->spi, &t);
            if (ret != ESP_OK) {
                gpio_set_level(card->config.cs_pin, 1);
                return ret;
            }
            token = t.rx_data[0];
            if (!(token & 0x80)) break;  // Response starts with 0
        }

        // Check response
        if (token == 0xFF || (token & 0x08)) {
            // No response or error response
            gpio_set_level(card->config.cs_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(100));  // Wait 100ms before retry
            continue;
        } else if (token > 1) {
            // Error response
            gpio_set_level(card->config.cs_pin, 1);
            return ESP_ERR_INVALID_RESPONSE;
        }

        // Read rest of response if needed
        if (response != NULL) {
            uint32_t resp = 0;
            if (cmd == CMD_SEND_IF_COND || cmd == CMD_READ_OCR) {
                // Read 4 bytes for R3/R7 response
                for (int i = 0; i < 4; i++) {
                    spi_transaction_t t = {
                        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
                        .length = 8,
                        .tx_data = {0xFF},
                    };
                    ret = spi_device_transmit(card->spi, &t);
                    if (ret != ESP_OK) {
                        gpio_set_level(card->config.cs_pin, 1);
                        return ret;
                    }
                    resp = (resp << 8) | t.rx_data[0];
                }
                *response = resp;
            }
        }

        // Success
        gpio_set_level(card->config.cs_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        return ESP_OK;
    }

    // All retries failed
    return ESP_ERR_TIMEOUT;
}

// Wait for card to be ready
static esp_err_t sdcard_wait_ready(sdcard_t* card, int timeout_ms) {
    uint8_t resp;
    int64_t start = esp_timer_get_time() / 1000;

    do {
        spi_transaction_t t = {
            .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
            .length = 8,
            .tx_data = {0xFF},
            .rx_data = {0}
        };
        esp_err_t ret = spi_device_transmit(card->spi, &t);
        if (ret != ESP_OK) return ret;
        resp = t.rx_data[0];
        if (resp == 0xFF) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while ((esp_timer_get_time() / 1000 - start) < timeout_ms);

    return ESP_ERR_TIMEOUT;
}

esp_err_t sdcard_init(const sdcard_config_t* config, sdcard_t** out_card) {
    esp_err_t ret;
    uint32_t response;
    sdcard_t* card;

    // Allocate card structure
    card = (sdcard_t*)calloc(1, sizeof(sdcard_t));
    if (!card) return ESP_ERR_NO_MEM;

    // Copy configuration
    memcpy(&card->config, config, sizeof(sdcard_config_t));
    card->type = SDCARD_NONE;
    card->supports_crc = true;

    // Configure CS pin first
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << config->cs_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&cs_cfg);
    if (ret != ESP_OK) goto cleanup;

    // Initial CS high and wait
    gpio_set_level(config->cs_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait 100ms for card power up

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Initialize SPI bus
    ret = spi_bus_initialize(config->host, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) goto cleanup;

    // Configure SPI device
    spi_device_interface_config_t dev_cfg = {
        .mode = 0,                // SPI mode 0
        .clock_speed_hz = 400000, // 400kHz for initialization
        .spics_io_num = -1,       // CS pin managed by us
        .queue_size = 1,
        .flags = 0,
    };

    // Add SPI device
    ret = spi_bus_add_device(config->host, &dev_cfg, &card->spi);
    if (ret != ESP_OK) goto cleanup;

    // Configure pull-ups for other pins
    gpio_set_pull_mode(config->mosi_pin, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(config->miso_pin, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(config->sclk_pin, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "Starting card initialization");

    // Send dummy bytes with CS high
    for (int i = 0; i < 20; i++) {
        spi_transaction_t t = {
            .flags = SPI_TRANS_USE_TXDATA,
            .length = 8,
            .tx_data = {0xFF}
        };
        ret = spi_device_transmit(card->spi, &t);
        if (ret != ESP_OK) goto cleanup;
        vTaskDelay(pdMS_TO_TICKS(1));  // Add small delay between bytes
    }

    // // Send CMD0 (GO_IDLE_STATE) until we get a response
    // int retries = 10;
    // do {
    //     ret = sdcard_cmd(card, CMD_GO_IDLE_STATE, 0, NULL);
    //     if (ret == ESP_OK) break;
    //     vTaskDelay(pdMS_TO_TICKS(10));
    // } while (--retries > 0);

    // if (retries <= 0) {
    //     ESP_LOGE(TAG, "Card did not respond to CMD0");
    //     ret = ESP_ERR_TIMEOUT;
    //     goto cleanup;
    // }

    // // Disable CRC (CMD59)
    // ret = sdcard_cmd(card, CMD_CRC_ON_OFF, 0, NULL);
    // if (ret != ESP_OK) {
    //     ESP_LOGW(TAG, "Failed to disable CRC, continuing anyway");
    //     card->supports_crc = false;
    // }

    // // Send CMD8 to check card version
    // ret = sdcard_cmd(card, CMD_SEND_IF_COND, 0x1AA, &response);
    // if (ret == ESP_OK) {
    //     if ((response & 0xFFF) != 0x1AA) {
    //         ESP_LOGE(TAG, "Card returned invalid voltage range");
    //         ret = ESP_ERR_INVALID_RESPONSE;
    //         goto cleanup;
    //     }

    //     // SDHC/SDXC card
    //     ESP_LOGI(TAG, "Card is SDHC/SDXC");
        
    //     // Send ACMD41 with HCS bit
    //     uint32_t start = esp_timer_get_time() / 1000;
    //     do {
    //         // Send CMD55 first (APP_CMD)
    //         ret = sdcard_cmd(card, CMD_APP_CMD, 0, NULL);
    //         if (ret != ESP_OK) continue;

    //         // Send ACMD41
    //         ret = sdcard_cmd(card, CMD_APP_OP_COND, 0x40000000, &response);
    //         if (ret != ESP_OK) continue;

    //         if (!(response & 0x80000000)) {
    //             // Card is still initializing
    //             vTaskDelay(pdMS_TO_TICKS(10));
    //             continue;
    //         }

    //         // Card is ready
    //         card->type = (response & 0x40000000) ? SDCARD_SDHC : SDCARD_SD;
    //         break;

    //     } while ((esp_timer_get_time() / 1000 - start) < 1000);

    //     if (card->type == SDCARD_NONE) {
    //         ESP_LOGE(TAG, "Card initialization timed out");
    //         ret = ESP_ERR_TIMEOUT;
    //         goto cleanup;
    //     }
    // } else {
    //     // SD1.x or MMC card
    //     ESP_LOGI(TAG, "Card might be SD1.x or MMC");
        
    //     // Try SD1.x initialization
    //     uint32_t start = esp_timer_get_time() / 1000;
    //     do {
    //         // Send CMD55 first (APP_CMD)
    //         ret = sdcard_cmd(card, CMD_APP_CMD, 0, NULL);
    //         if (ret != ESP_OK) continue;

    //         // Send ACMD41
    //         ret = sdcard_cmd(card, CMD_APP_OP_COND, 0, &response);
    //         if (ret != ESP_OK) continue;

    //         if (!(response & 0x80000000)) {
    //             // Card is still initializing
    //             vTaskDelay(pdMS_TO_TICKS(10));
    //             continue;
    //         }

    //         // Card is ready
    //         card->type = SDCARD_SD;
    //         break;

    //     } while ((esp_timer_get_time() / 1000 - start) < 1000);

    //     if (card->type == SDCARD_NONE) {
    //         // Try MMC initialization
    //         start = esp_timer_get_time() / 1000;
    //         do {
    //             ret = sdcard_cmd(card, CMD_SEND_OP_COND, 0, &response);
    //             if (ret != ESP_OK) continue;

    //             if (!(response & 0x80000000)) {
    //                 // Card is still initializing
    //                 vTaskDelay(pdMS_TO_TICKS(10));
    //                 continue;
    //             }

    //             // Card is ready
    //             card->type = SDCARD_MMC;
    //             break;

    //         } while ((esp_timer_get_time() / 1000 - start) < 1000);

    //         if (card->type == SDCARD_NONE) {
    //             ESP_LOGE(TAG, "Card initialization failed");
    //             ret = ESP_ERR_NOT_FOUND;
    //             goto cleanup;
    //         }
    //     }
    // }

    // // Set block size to 512 bytes for non-SDHC cards
    // if (card->type != SDCARD_SDHC) {
    //     ret = sdcard_cmd(card, CMD_SET_BLOCKLEN, 512, NULL);
    //     if (ret != ESP_OK) {
    //         ESP_LOGE(TAG, "Failed to set block length");
    //         goto cleanup;
    //     }
    // }

    // // Set higher clock speed for normal operation
    // dev_cfg.clock_speed_hz = config->max_freq_khz * 1000;
    // if (dev_cfg.clock_speed_hz > 25000000) {
    //     dev_cfg.clock_speed_hz = 25000000;  // Maximum 25MHz
    // }

    // // Remove old device and add with new configuration
    // spi_bus_remove_device(card->spi);
    // ret = spi_bus_add_device(config->host, &dev_cfg, &card->spi);
    // if (ret != ESP_OK) goto cleanup;

    // ESP_LOGI(TAG, "Card initialized successfully");
    // ESP_LOGI(TAG, "Card type: %s", 
    //     card->type == SDCARD_SD ? "SD" :
    //     card->type == SDCARD_SDHC ? "SDHC" :
    //     card->type == SDCARD_MMC ? "MMC" : "Unknown");

    *out_card = card;
    return ESP_OK;

cleanup:
    if (card->spi) {
        spi_bus_remove_device(card->spi);
    }
    spi_bus_free(config->host);
    free(card);
    return ret;
}

esp_err_t sdcard_deinit(sdcard_t* card) {
    if (!card) return ESP_ERR_INVALID_ARG;

    spi_bus_remove_device(card->spi);
    spi_bus_free(card->config.host);
    free(card);

    return ESP_OK;
}

esp_err_t sdcard_mount(sdcard_t* card, const char* mount_point, size_t max_files, bool format_if_mount_failed) {
    if (!card || !mount_point) return ESP_ERR_INVALID_ARG;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = max_files,
        .allocation_unit_size = 16 * 1024
    };

    // Initialize SD card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = card->config.host;
    host.max_freq_khz = card->config.max_freq_khz;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = card->config.cs_pin;
    slot_config.host_id = card->config.host;

    return esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card->card);
}

esp_err_t sdcard_unmount(sdcard_t* card, const char* mount_point) {
    if (!card || !mount_point) return ESP_ERR_INVALID_ARG;

    return esp_vfs_fat_sdcard_unmount(mount_point, card->card);
}

esp_err_t sdcard_get_info(sdcard_t* card) {
    if (!card) return ESP_ERR_INVALID_ARG;

    // Get CSD register
    uint32_t response;
    esp_err_t ret = sdcard_cmd(card, CMD_SEND_CSD, 0, &response);
    if (ret != ESP_OK) return ret;

    // Parse card capacity
    if (card->type == SDCARD_SDHC) {
        // SDHC cards use a different CSD structure
        uint32_t c_size = ((response >> 8) & 0x3FFFFF) + 1;
        card->sectors = c_size * 1024; // Capacity = (C_SIZE + 1) * 512KB
        card->sector_size = 512;
    } else {
        // SD/MMC cards
        uint8_t read_bl_len = (response >> 16) & 0xF;
        uint16_t c_size = ((response >> 30) & 0x3FF) | ((response & 0x3FF) << 2);
        uint8_t c_size_mult = (response >> 15) & 0x7;
        
        card->sector_size = 1 << read_bl_len;
        card->sectors = (c_size + 1) * (1 << (c_size_mult + 2));
    }

    ESP_LOGI(TAG, "Card capacity: %lu MB", 
        (uint32_t)(card->sectors * card->sector_size / (1024 * 1024)));

    return ESP_OK;
}
