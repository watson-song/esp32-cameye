#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_vfs.h"
#include "sdcard_hal.h"
#include "fs_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "fs_test2";

// Pin assignments for XIAO ESP32S3
#define PIN_NUM_MISO  8
#define PIN_NUM_MOSI  9
#define PIN_NUM_CLK   7
#define PIN_NUM_CS    21

// 文件系统路径
#define MOUNT_POINT "/sdcard"
#define TEST_DIR "d"
#define TEST_SUBDIR "s"
#define TEST_FILE1 "1.txt"
#define TEST_FILE2 "2.txt"
#define TEST_FILE3 "3.txt"

static void print_file_info(const fs_file_info_t* info) {
    time_t mtime = info->last_modified;
    struct tm timeinfo;
    localtime_r(&mtime, &timeinfo);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);

    ESP_LOGI(TAG, "File: %s", info->name);
    ESP_LOGI(TAG, "  Type: %s", info->is_directory ? "Directory" : "File");
    ESP_LOGI(TAG, "  Size: %llu bytes", info->size);
    ESP_LOGI(TAG, "  Modified: %s", time_str);
}

static void list_directory(const char* path) {
    ESP_LOGI(TAG, "Listing directory: %s", path);
    
    fs_dir_iterator_t iterator = NULL;
    esp_err_t ret = fs_opendir(path, &iterator);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open directory");
        return;
    }

    fs_file_info_t info;
    while (fs_readdir(iterator, &info) == ESP_OK) {
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }
        print_file_info(&info);
    }

    fs_closedir(iterator);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting extended filesystem test");

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

    // 检查空间
    uint64_t required_space = 1024 * 1024; // 1MB
    if (!fs_has_space(required_space)) {
        ESP_LOGE(TAG, "Not enough space for testing");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Sufficient space available");

    // 删除可能存在的测试目录
    fs_remove_recursive(TEST_DIR);

    // 创建测试目录
    ret = fs_mkdir(TEST_DIR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create test directory");
        goto cleanup;
    }

    // 创建子目录
    char path[32];
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, TEST_SUBDIR);
    ret = fs_mkdir(path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create test subdirectory");
        goto cleanup;
    }

    // 创建测试文件1
    char mount_path[64];
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, TEST_FILE1);
    snprintf(mount_path, sizeof(mount_path), "%s/%s", MOUNT_POINT, path);
    FILE* f = fopen(mount_path, "w");
    if (f) {
        fprintf(f, "This is test file 1");
        fclose(f);
    }

    // 创建测试文件2
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, TEST_FILE2);
    snprintf(mount_path, sizeof(mount_path), "%s/%s", MOUNT_POINT, path);
    f = fopen(mount_path, "w");
    if (f) {
        fprintf(f, "This is test file 2\nWith multiple lines\n");
        fclose(f);
    }

    // 创建测试文件3
    snprintf(path, sizeof(path), "%s/%s/%s", TEST_DIR, TEST_SUBDIR, TEST_FILE3);
    snprintf(mount_path, sizeof(mount_path), "%s/%s", MOUNT_POINT, path);
    f = fopen(mount_path, "w");
    if (f) {
        fprintf(f, "This is test file 3 in subdirectory");
        fclose(f);
    }

    // 获取并显示文件信息
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, TEST_FILE1);
    fs_file_info_t info;
    ret = fs_stat(path, &info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File 1 info:");
        print_file_info(&info);
    }

    // 列出目录内容
    list_directory(TEST_DIR);
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, TEST_SUBDIR);
    list_directory(path);

    // 获取文件大小
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, TEST_FILE2);
    uint64_t file_size;
    ret = fs_get_file_size(path, &file_size);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "File 2 size: %llu bytes", file_size);
    }

    // 递归删除测试目录
    ESP_LOGI(TAG, "Removing test directory recursively");
    ret = fs_remove_recursive(TEST_DIR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove test directory");
        goto cleanup;
    }

    ESP_LOGI(TAG, "All extended filesystem tests passed!");

cleanup:
    // 清理资源
    fs_deinit();
    sdcard_deinit(card);
}
