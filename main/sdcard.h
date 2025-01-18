#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/sdmmc_types.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// SD Card commands
typedef enum {
    CMD_GO_IDLE_STATE = 0,       // GO_IDLE_STATE - init card in spi mode if CS low
    CMD_SEND_OP_COND = 1,        // SEND_OP_COND - Initiate initialization process
    CMD_SEND_IF_COND = 8,        // SEND_IF_COND - verify SD Memory Card interface operating condition
    CMD_SEND_CSD = 9,            // SEND_CSD - read the Card Specific Data (CSD register)
    CMD_SEND_CID = 10,           // SEND_CID - read the card identification information (CID register)
    CMD_STOP_TRANSMISSION = 12,   // STOP_TRANSMISSION - end multiple block read sequence
    CMD_SET_BLOCKLEN = 16,       // SET_BLOCKLEN - change R/W block size
    CMD_READ_BLOCK_SINGLE = 17,  // READ_SINGLE_BLOCK - read a single data block
    CMD_READ_BLOCK_MULTIPLE = 18, // READ_MULTIPLE_BLOCK - read a multiple data blocks
    CMD_WRITE_BLOCK_SINGLE = 24, // WRITE_BLOCK - write a single data block
    CMD_WRITE_BLOCK_MULTIPLE = 25,// WRITE_MULTIPLE_BLOCK - write blocks of data until stopped
    CMD_APP_CMD = 55,            // APP_CMD - escape for application specific command
    CMD_READ_OCR = 58,           // READ_OCR - read the OCR register of a card
    CMD_CRC_ON_OFF = 59,         // CRC_ON_OFF - enable or disable CRC checking
    CMD_APP_OP_COND = 0x29,      // SD_SEND_OP_COND (ACMD41) - Sends host capacity support information
    CMD_APP_CLR_CARD_DETECT = 0x2A // SD_APP_SET_CLR_CARD_DETECT (ACMD42)
} sdcard_cmd_t;

// SD Card types
typedef enum {
    SDCARD_NONE = 0,     // No card detected
    SDCARD_MMC = 1,      // MMC card
    SDCARD_SD = 2,       // SD card
    SDCARD_SDHC = 3,     // SDHC card
    SDCARD_UNKNOWN = 4   // Unknown card type
} sdcard_type_t;

// SD Card configuration
typedef struct {
    int mosi_pin;        // MOSI GPIO number
    int miso_pin;        // MISO GPIO number
    int sclk_pin;        // SCLK GPIO number
    int cs_pin;          // CS GPIO number
    int max_freq_khz;    // Maximum frequency in KHz
    spi_host_device_t host; // SPI host number
} sdcard_config_t;

// SD Card handle
typedef struct {
    sdcard_type_t type;          // Card type
    uint32_t sectors;            // Number of sectors
    uint32_t sector_size;        // Sector size in bytes
    bool supports_crc;           // Whether card supports CRC
    sdcard_config_t config;      // Card configuration
    spi_device_handle_t spi;     // SPI device handle
    sdmmc_card_t *card;          // SD/MMC card info
} sdcard_t;

/**
 * @brief Initialize SD card
 * 
 * @param config SD card configuration
 * @param[out] card Pointer to store card handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sdcard_init(const sdcard_config_t* config, sdcard_t** card);

/**
 * @brief Deinitialize SD card
 * 
 * @param card Card handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sdcard_deinit(sdcard_t* card);

/**
 * @brief Mount SD card filesystem
 * 
 * @param card Card handle
 * @param mount_point Mount point path
 * @param max_files Maximum number of open files
 * @param format_if_mount_failed Whether to format card if mounting fails
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sdcard_mount(sdcard_t* card, const char* mount_point, size_t max_files, bool format_if_mount_failed);

/**
 * @brief Unmount SD card filesystem
 * 
 * @param card Card handle
 * @param mount_point Mount point path
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sdcard_unmount(sdcard_t* card, const char* mount_point);

/**
 * @brief Get card information
 * 
 * @param card Card handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sdcard_get_info(sdcard_t* card);

#ifdef __cplusplus
}
#endif
