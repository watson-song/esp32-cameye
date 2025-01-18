#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stdio.h>
#include "sdcard_hal.h"

// 文件系统配置
typedef struct {
    const char* mount_point;     // 挂载点路径
    size_t max_files;           // 最大同时打开文件数
    bool format_if_mount_failed; // 挂载失败时是否格式化
    sdcard_config_t sdcard;     // SD卡配置
} fs_config_t;

// 文件系统信息
typedef struct {
    uint64_t total_bytes;      // 总容量
    uint64_t used_bytes;       // 已使用容量
    uint64_t free_bytes;       // 剩余容量
} fs_info_t;

// 文件信息
typedef struct {
    char name[256];           // 文件名
    uint64_t size;           // 文件大小
    uint32_t last_modified;  // 最后修改时间
    bool is_directory;       // 是否是目录
} fs_file_info_t;

// 文件句柄
typedef struct fs_file_s* fs_file_t;

// 目录句柄
typedef struct fs_dir_s* fs_dir_t;

// 文件打开模式
typedef enum {
    FS_FILE_READ = 0x01,    // 只读模式
    FS_FILE_WRITE = 0x02,   // 只写模式
    FS_FILE_APPEND = 0x04,  // 追加模式
} fs_mode_t;

// 文件定位起点
typedef enum {
    FS_SEEK_SET = SEEK_SET,  // 文件开头
    FS_SEEK_CUR = SEEK_CUR,  // 当前位置
    FS_SEEK_END = SEEK_END   // 文件末尾
} fs_seek_mode_t;

// 目录迭代器
typedef struct fs_dir_iterator_s* fs_dir_iterator_t;

/**
 * @brief 初始化文件系统
 * @param config 文件系统配置，包含SD卡配置
 * @return ESP_OK 成功
 */
esp_err_t fs_init(const fs_config_t* config);

/**
 * @brief 卸载文件系统
 * @return ESP_OK 成功
 */
esp_err_t fs_deinit(void);

/**
 * @brief 获取文件系统信息
 * @param info 输出的文件系统信息
 * @return ESP_OK 成功
 */
esp_err_t fs_get_info(fs_info_t* info);

/**
 * @brief 检查文件是否存在
 * @param path 文件路径
 * @return true 存在, false 不存在
 */
bool fs_exists(const char* path);

/**
 * @brief 创建目录
 * @param path 目录路径
 * @return ESP_OK 成功
 */
esp_err_t fs_mkdir(const char* path);

/**
 * @brief 删除文件或空目录
 * @param path 文件或目录路径
 * @return ESP_OK 成功
 */
esp_err_t fs_remove(const char* path);

/**
 * @brief 重命名文件
 * @param old_path 原文件路径
 * @param new_path 新文件路径
 * @return ESP_OK 成功
 */
esp_err_t fs_rename(const char* old_path, const char* new_path);

/**
 * @brief 获取文件信息
 * @param path 文件路径
 * @param info 输出的文件信息
 * @return ESP_OK 成功
 */
esp_err_t fs_stat(const char* path, fs_file_info_t* info);

/**
 * @brief 打开文件
 * @param path 文件路径
 * @param mode 打开模式
 * @return 文件句柄，NULL表示失败
 */
fs_file_t fs_open(const char* path, fs_mode_t mode);

/**
 * @brief 关闭文件
 * @param file 文件句柄
 * @return ESP_OK 成功
 */
esp_err_t fs_close(fs_file_t file);

/**
 * @brief 读取文件
 * @param file 文件句柄
 * @param buf 输出缓冲区
 * @param size 要读取的字节数
 * @return 实际读取的字节数，-1表示错误
 */
int fs_read(fs_file_t file, void* buf, size_t size);

/**
 * @brief 写入文件
 * @param file 文件句柄
 * @param buf 输入缓冲区
 * @param size 要写入的字节数
 * @return 实际写入的字节数，-1表示错误
 */
int fs_write(fs_file_t file, const void* buf, size_t size);

/**
 * @brief 定位文件指针
 * @param file 文件句柄
 * @param offset 偏移量
 * @param mode 定位模式
 * @return ESP_OK 成功
 */
esp_err_t fs_seek(fs_file_t file, long offset, fs_seek_mode_t mode);

/**
 * @brief 获取文件指针位置
 * @param file 文件句柄
 * @return 当前位置，-1表示错误
 */
long fs_position(fs_file_t file);

/**
 * @brief 获取文件大小
 * @param file 文件句柄
 * @return 文件大小，-1表示错误
 */
long fs_size(fs_file_t file);

/**
 * @brief 打开目录
 * @param path 目录路径
 * @return 目录句柄，NULL表示失败
 */
fs_dir_t fs_openDir(const char* path);

/**
 * @brief 读取目录下一个文件
 * @param dir 目录句柄
 * @param info 输出的文件信息
 * @return true 成功，false 没有更多文件
 */
bool fs_nextFile(fs_dir_t dir, fs_file_info_t* info);

/**
 * @brief 关闭目录
 * @param dir 目录句柄
 * @return ESP_OK 成功
 */
esp_err_t fs_closeDir(fs_dir_t dir);

/**
 * @brief 打开目录迭代器
 * @param path 目录路径
 * @param out_iterator 输出的迭代器句柄
 * @return ESP_OK 成功
 */
esp_err_t fs_opendir(const char* path, fs_dir_iterator_t* out_iterator);

/**
 * @brief 读取下一个目录项
 * @param iterator 迭代器句柄
 * @param out_info 输出的文件信息
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 没有更多项
 */
esp_err_t fs_readdir(fs_dir_iterator_t iterator, fs_file_info_t* out_info);

/**
 * @brief 关闭目录迭代器
 * @param iterator 迭代器句柄
 * @return ESP_OK 成功
 */
esp_err_t fs_closedir(fs_dir_iterator_t iterator);

/**
 * @brief 递归删除目录及其内容
 * @param path 目录路径
 * @return ESP_OK 成功
 */
esp_err_t fs_remove_recursive(const char* path);

/**
 * @brief 检查剩余空间是否足够
 * @param required_size 需要的空间大小
 * @return true 空间足够, false 空间不足
 */
bool fs_has_space(uint64_t required_size);

/**
 * @brief 获取文件大小
 * @param path 文件路径
 * @param out_size 输出的文件大小
 * @return ESP_OK 成功
 */
esp_err_t fs_get_file_size(const char* path, uint64_t* out_size);
