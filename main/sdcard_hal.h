#pragma once

#include <esp_err.h>
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

#define SDCARD_BLOCK_SIZE 512

typedef enum {
    CARD_NONE = 0,
    CARD_MMC = 1,
    CARD_SD = 2,
    CARD_SDHC = 3,
} sdcard_type_t;

typedef struct {
    spi_host_device_t host;
    int pin_mosi;
    int pin_miso;
    int pin_sck;
    int pin_cs;
    int freq_khz;
} sdcard_config_t;

typedef struct {
    sdcard_type_t type;
    uint64_t capacity_bytes;
} sdcard_info_t;

typedef struct {
    sdcard_type_t type;
    uint32_t sectors;
    sdspi_dev_handle_t spi;
    spi_host_device_t host;
    gpio_num_t pin_cs;  // CS引脚
    sdmmc_card_t* sdcard;  // Keep the sdcard structure for read/write operations
} sdcard_t;

// SPI模式的SD卡操作函数
esp_err_t sdspi_card_init(const sdcard_config_t* config, sdcard_t** out_card);
esp_err_t sdspi_card_deinit(sdcard_t* card);
esp_err_t sdcard_read_blocks(sdcard_t* card, size_t start_block, size_t n_blocks, void* dst);
esp_err_t sdcard_write_blocks(sdcard_t* card, size_t start_block, size_t n_blocks, const void* src);
esp_err_t sdcard_get_info(sdcard_t* card, sdcard_info_t* out_info);

// 为了向后兼容，保留旧的函数名作为别名
#define sdcard_init sdspi_card_init
#define sdcard_deinit sdspi_card_deinit
