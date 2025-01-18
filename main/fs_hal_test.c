#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_vfs.h"
#include "sdcard_hal.h"
#include "fs_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "fs_test";

// Pin assignments for XIAO ESP32S3
#define PIN_NUM_MISO  8
#define PIN_NUM_MOSI  9
#define PIN_NUM_CLK   7
#define PIN_NUM_CS    21

#define MOUNT_POINT "/sdcard"
#define TEST_FILE_PATH "/test.txt"
#define TEST_DIR_PATH "/testdir"
#define TEST_FILE_CONTENT "Hello SD Card Filesystem!"

static void write_and_verify_file(const char* path, const char* content) {
    char full_path[ESP_VFS_PATH_MAX + 1];
    snprintf(full_path, sizeof(full_path), "%s%s", MOUNT_POINT, path);
    
    ESP_LOGI(TAG, "Writing to file: %s", full_path);
    FILE* f = fopen(full_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path);
        return;
    }
    
    if (fprintf(f, "%s", content) < 0) {
        ESP_LOGE(TAG, "Failed to write to file: %s", full_path);
        fclose(f);
        return;
    }
    fclose(f);
    
    // 验证文件内容
    f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", full_path);
        return;
    }
    
    char buf[128];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    
    if (strcmp(buf, content) != 0) {
        ESP_LOGE(TAG, "File content verification failed");
        ESP_LOGE(TAG, "Expected: %s", content);
        ESP_LOGE(TAG, "Got: %s", buf);
    } else {
        ESP_LOGI(TAG, "File content verified successfully");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting filesystem test");

    // 初始化SD卡
    sdcard_config_t sd_config = {
        .host = SPI2_HOST,
        .pin_mosi = PIN_NUM_MOSI,
        .pin_miso = PIN_NUM_MISO,
        .pin_sck = PIN_NUM_CLK,
        .pin_cs = PIN_NUM_CS,
        .freq_khz = 40000,
    };

    sdcard_t* card = NULL;
    esp_err_t ret = sdcard_init(&sd_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        return;
    }

    // 初始化文件系统
    fs_config_t fs_config = {
        .mount_point = MOUNT_POINT,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    ret = fs_init(card, &fs_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize filesystem");
        sdcard_deinit(card);
        return;
    }

    // 获取文件系统信息
    fs_info_t info;
    ret = fs_get_info(&info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get filesystem info");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Filesystem info:");
    ESP_LOGI(TAG, "  Total: %llu bytes", info.total_bytes);
    ESP_LOGI(TAG, "  Used:  %llu bytes", info.used_bytes);
    ESP_LOGI(TAG, "  Free:  %llu bytes", info.free_bytes);

    // 如果测试目录已存在，先删除它
    if (fs_exists(TEST_DIR_PATH)) {
        ESP_LOGI(TAG, "Test directory already exists, removing it first");
        ret = fs_remove(TEST_DIR_PATH);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove existing test directory");
            goto cleanup;
        }
    }

    // 创建测试目录
    ESP_LOGI(TAG, "Creating test directory");
    ret = fs_mkdir(TEST_DIR_PATH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create directory");
        goto cleanup;
    }

    // 写入并验证测试文件
    ESP_LOGI(TAG, "Writing and verifying test file");
    write_and_verify_file(TEST_FILE_PATH, TEST_FILE_CONTENT);

    // 验证文件是否存在
    if (!fs_exists(TEST_FILE_PATH)) {
        ESP_LOGE(TAG, "Test file does not exist");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Test file exists");

    // 重命名文件
    ESP_LOGI(TAG, "Renaming file");
    ret = fs_rename(TEST_FILE_PATH, "/renamed.txt");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to rename file");
        goto cleanup;
    }

    // 删除文件
    ESP_LOGI(TAG, "Removing file");
    ret = fs_remove("/renamed.txt");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove file");
        goto cleanup;
    }

    // 删除目录
    ESP_LOGI(TAG, "Removing directory");
    ret = fs_remove(TEST_DIR_PATH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove directory");
        goto cleanup;
    }

    ESP_LOGI(TAG, "All filesystem tests passed!");

cleanup:
    // 清理资源
    fs_deinit();
    sdcard_deinit(card);
}
