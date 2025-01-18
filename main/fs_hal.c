#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "fs_hal.h"

static const char* TAG = "fs_hal";
static sdmmc_card_t* s_card = NULL;
static bool s_is_mounted = false;
static char s_mount_point[ESP_VFS_PATH_MAX + 1] = { 0 };

#define FS_MAX_PATH_LEN 128  // 设置合适的路径长度

// 目录迭代器结构体
struct fs_dir_iterator_s {
    DIR* dir;
    char base_path[FS_MAX_PATH_LEN];
};

// 用于构建完整路径的辅助函数
static esp_err_t build_full_path(char* full_path, size_t max_len, const char* base, const char* path) {
    ESP_LOGD(TAG, "Building path - base: '%s', path: '%s'", base, path);
    
    // 检查参数
    if (!full_path || !base || !path || max_len == 0) {
        ESP_LOGE(TAG, "Invalid parameters in build_full_path");
        return ESP_ERR_INVALID_ARG;
    }

    // 计算所需长度
    size_t base_len = strlen(base);
    size_t path_len = strlen(path);
    size_t needed_len = base_len + 1 + path_len + 1;  // +1 for '/' and +1 for null terminator

    // 检查缓冲区大小
    if (needed_len > max_len) {
        ESP_LOGE(TAG, "Path too long: base='%s' (%d) + path='%s' (%d) = %d chars (max %d)",
                 base, (int)base_len, path, (int)path_len, (int)needed_len, (int)max_len);
        return ESP_ERR_INVALID_ARG;
    }

    // 复制基础路径
    strlcpy(full_path, base, max_len);
    
    // 删除基础路径末尾的斜杠
    while (base_len > 1 && full_path[base_len - 1] == '/') {
        full_path[--base_len] = '\0';
    }

    // 添加斜杠
    full_path[base_len] = '/';
    
    // 跳过路径开头的斜杠
    while (*path == '/') {
        path++;
    }

    // 复制路径
    strlcpy(full_path + base_len + 1, path, max_len - base_len - 1);

    ESP_LOGD(TAG, "Built path: '%s'", full_path);
    return ESP_OK;
}

esp_err_t fs_init(sdcard_t* sdcard, const fs_config_t* config) {
    if (!sdcard || !config || !config->mount_point) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_is_mounted) {
        ESP_LOGE(TAG, "Filesystem already mounted");
        return ESP_ERR_INVALID_STATE;
    }

    // 保存挂载点
    strlcpy(s_mount_point, config->mount_point, sizeof(s_mount_point));

    ESP_LOGI(TAG, "Initializing filesystem at %s", config->mount_point);

    // 挂载配置
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = config->format_if_mount_failed,
        .max_files = config->max_files,
        .allocation_unit_size = 16 * 1024  // 16KB clusters
    };

    // 准备SDMMC主机配置
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = sdcard->spi;
    host.max_freq_khz = sdcard->sdcard->max_freq_khz;

    // 准备SDSPI设备配置
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = sdcard->pin_cs;
    slot_config.host_id = sdcard->host;

    // 挂载文件系统
    esp_err_t ret = esp_vfs_fat_sdspi_mount(config->mount_point, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s)", esp_err_to_name(ret));
        }
        s_mount_point[0] = '\0';
        return ret;
    }

    s_is_mounted = true;
    ESP_LOGI(TAG, "Filesystem mounted successfully");
    return ESP_OK;
}

esp_err_t fs_deinit(void) {
    if (!s_is_mounted) {
        return ESP_OK;  // 如果没有挂载，直接返回成功
    }

    // 卸载文件系统
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount filesystem");
        return ret;
    }

    s_is_mounted = false;
    s_mount_point[0] = '\0';
    s_card = NULL;
    return ESP_OK;
}

esp_err_t fs_get_info(fs_info_t* info) {
    if (!info || !s_is_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs;
    DWORD free_clusters;  // 修改为DWORD类型
    int res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "Failed to get filesystem info (%d)", res);
        return ESP_FAIL;
    }

    info->total_bytes = ((uint64_t)fs->n_fatent - 2) * fs->csize * 512;
    info->free_bytes = free_clusters * fs->csize * 512;
    info->used_bytes = info->total_bytes - info->free_bytes;

    return ESP_OK;
}

bool fs_exists(const char* path) {
    if (!path || !s_is_mounted) {
        return false;
    }

    struct stat st;
    char full_path[ESP_VFS_PATH_MAX + 1];
    if (build_full_path(full_path, sizeof(full_path), s_mount_point, path) != ESP_OK) {
        return false;
    }
    return stat(full_path, &st) == 0;
}

esp_err_t fs_mkdir(const char* path) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Creating directory: %s/%s", s_mount_point, path);

    // 构建完整路径
    char full_path[ESP_VFS_PATH_MAX + 1];
    esp_err_t ret = build_full_path(full_path, sizeof(full_path), s_mount_point, path);
    if (ret != ESP_OK) {
        return ret;
    }

    // 创建目录
    if (mkdir(full_path, 0755) != 0) {
        if (errno == EEXIST) {
            ESP_LOGI(TAG, "Directory already exists");
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to create directory: %s (errno: %d)", full_path, errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t fs_remove(const char* path) {
    if (!path || !s_is_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[ESP_VFS_PATH_MAX + 1];
    esp_err_t ret = build_full_path(full_path, sizeof(full_path), s_mount_point, path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        ESP_LOGE(TAG, "Path does not exist: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }

    if (S_ISDIR(st.st_mode)) {
        if (rmdir(full_path) != 0) {
            ESP_LOGE(TAG, "Failed to remove directory: %s (errno: %d)", full_path, errno);
            return ESP_FAIL;
        }
    } else {
        if (unlink(full_path) != 0) {
            ESP_LOGE(TAG, "Failed to remove file: %s (errno: %d)", full_path, errno);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t fs_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path || !s_is_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char full_old_path[ESP_VFS_PATH_MAX + 1];
    char full_new_path[ESP_VFS_PATH_MAX + 1];
    
    esp_err_t ret = build_full_path(full_old_path, sizeof(full_old_path), s_mount_point, old_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = build_full_path(full_new_path, sizeof(full_new_path), s_mount_point, new_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (rename(full_old_path, full_new_path) != 0) {
        ESP_LOGE(TAG, "Failed to rename %s to %s (errno: %d)", full_old_path, full_new_path, errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t fs_stat(const char* path, fs_file_info_t* info) {
    if (!path || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    // 构建完整路径
    char full_path[FS_MAX_PATH_LEN];
    esp_err_t ret = build_full_path(full_path, sizeof(full_path), s_mount_point, path);
    if (ret != ESP_OK) {
        return ret;
    }

    // 获取文件信息
    struct stat st;
    if (stat(full_path, &st) != 0) {
        if (errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Failed to get file info for %s (errno: %d)", full_path, errno);
        return ESP_FAIL;
    }

    // 提取文件名
    const char* name = strrchr(path, '/');
    if (name) {
        name++; // 跳过斜杠
    } else {
        name = path;
    }

    // 填充文件信息
    strlcpy(info->name, name, sizeof(info->name));
    info->size = st.st_size;
    info->last_modified = st.st_mtime;
    info->is_directory = S_ISDIR(st.st_mode);

    return ESP_OK;
}

esp_err_t fs_opendir(const char* path, fs_dir_iterator_t* out_iterator) {
    if (!path || !out_iterator) {
        return ESP_ERR_INVALID_ARG;
    }

    // 分配迭代器内存
    struct fs_dir_iterator_s* iterator = calloc(1, sizeof(struct fs_dir_iterator_s));
    if (!iterator) {
        ESP_LOGE(TAG, "Failed to allocate directory iterator");
        return ESP_ERR_NO_MEM;
    }

    // 构建完整路径
    char full_path[FS_MAX_PATH_LEN];
    esp_err_t ret = build_full_path(full_path, sizeof(full_path), s_mount_point, path);
    if (ret != ESP_OK) {
        free(iterator);
        return ret;
    }

    // 打开目录
    iterator->dir = opendir(full_path);
    if (!iterator->dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s (errno: %d)", full_path, errno);
        free(iterator);
        return ESP_FAIL;
    }

    // 保存基础路径
    strlcpy(iterator->base_path, full_path, sizeof(iterator->base_path));

    *out_iterator = iterator;
    return ESP_OK;
}

esp_err_t fs_readdir(fs_dir_iterator_t iterator, fs_file_info_t* out_info) {
    if (!iterator || !out_info) {
        return ESP_ERR_INVALID_ARG;
    }

    struct dirent* entry;
    
    // 读取并跳过 . 和 .. 目录
    do {
        errno = 0;  // 清除之前的错误
        entry = readdir(iterator->dir);
        
        if (!entry) {
            if (errno != 0) {
                ESP_LOGE(TAG, "Failed to read directory entry (errno: %d)", errno);
                return ESP_FAIL;
            }
            return ESP_ERR_NOT_FOUND;  // 没有更多条目
        }
    } while (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0);

    // 构建完整路径以获取文件信息
    char full_path[FS_MAX_PATH_LEN];
    esp_err_t ret = build_full_path(full_path, sizeof(full_path), iterator->base_path, entry->d_name);
    if (ret != ESP_OK) {
        return ret;
    }

    // 获取文件信息
    struct stat st;
    if (stat(full_path, &st) != 0) {
        if (errno == ENOENT) {
            // 文件可能刚被删除，继续读取下一个
            return fs_readdir(iterator, out_info);
        }
        ESP_LOGE(TAG, "Failed to get file info for %s (errno: %d)", full_path, errno);
        return ESP_FAIL;
    }

    // 填充文件信息
    strlcpy(out_info->name, entry->d_name, sizeof(out_info->name));
    out_info->size = st.st_size;
    out_info->last_modified = st.st_mtime;
    out_info->is_directory = S_ISDIR(st.st_mode);

    return ESP_OK;
}

esp_err_t fs_closedir(fs_dir_iterator_t iterator) {
    if (!iterator) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    if (closedir(iterator->dir) != 0) {
        ESP_LOGE(TAG, "Failed to close directory (errno: %d)", errno);
        ret = ESP_FAIL;
    }

    free(iterator);
    return ret;
}

static esp_err_t remove_dir_contents(const char* path) {
    fs_dir_iterator_t iterator = NULL;
    esp_err_t ret = fs_opendir(path, &iterator);
    if (ret != ESP_OK) {
        return ret;
    }

    fs_file_info_t info;
    while (fs_readdir(iterator, &info) == ESP_OK) {
        // 跳过 "." 和 ".." 目录
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }

        char path_buf[ESP_VFS_PATH_MAX + 1];
        if (snprintf(path_buf, sizeof(path_buf), "%s/%s", path, info.name) >= sizeof(path_buf)) {
            fs_closedir(iterator);
            return ESP_ERR_INVALID_ARG;
        }

        if (info.is_directory) {
            // 递归删除子目录
            ret = fs_remove_recursive(path_buf);
        } else {
            // 删除文件
            ret = fs_remove(path_buf);
        }

        if (ret != ESP_OK) {
            fs_closedir(iterator);
            return ret;
        }
    }

    fs_closedir(iterator);
    return ESP_OK;
}

esp_err_t fs_remove_recursive(const char* path) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Removing recursively: %s", path);

    // 构建完整路径
    char full_path[FS_MAX_PATH_LEN];
    esp_err_t ret = build_full_path(full_path, sizeof(full_path), s_mount_point, path);
    if (ret != ESP_OK) {
        return ret;
    }

    // 先尝试作为文件删除
    if (unlink(full_path) == 0) {
        ESP_LOGD(TAG, "Removed file: %s", full_path);
        return ESP_OK;
    }

    // 如果不是文件，尝试作为目录处理
    DIR* dir = opendir(full_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s (errno: %d)", full_path, errno);
        return ESP_FAIL;
    }

    struct dirent* entry;
    char entry_path[FS_MAX_PATH_LEN];
    size_t path_len = strlen(path);

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t name_len = strlen(entry->d_name);
        if (path_len + name_len + 2 > sizeof(entry_path)) {
            ESP_LOGE(TAG, "Path too long: '%s/%s'", path, entry->d_name);
            continue;
        }

        if (path_len > 0) {
            strlcpy(entry_path, path, sizeof(entry_path));
            strlcat(entry_path, "/", sizeof(entry_path));
            strlcat(entry_path, entry->d_name, sizeof(entry_path));
        } else {
            strlcpy(entry_path, entry->d_name, sizeof(entry_path));
        }

        // 递归删除子项
        ret = fs_remove_recursive(entry_path);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove: %s", entry_path);
            // 继续处理其他文件
        }
    }

    closedir(dir);

    // 删除空目录
    ESP_LOGD(TAG, "Removing empty directory: %s", full_path);
    for (int retry = 0; retry < 3; retry++) {
        if (rmdir(full_path) == 0) {
            return ESP_OK;
        }
        
        if (errno != EACCES && errno != EBUSY) {
            break;
        }
        
        // 如果是权限或忙的问题，等待一下再试
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGE(TAG, "Failed to remove directory: %s (errno: %d)", full_path, errno);
    return ESP_FAIL;
}

esp_err_t fs_get_file_size(const char* path, uint64_t* out_size) {
    if (!path || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    fs_file_info_t info;
    esp_err_t ret = fs_stat(path, &info);
    if (ret != ESP_OK) {
        return ret;
    }

    *out_size = info.size;
    return ESP_OK;
}

bool fs_has_space(uint64_t required_size) {
    if (!s_is_mounted) {
        return false;
    }

    fs_info_t info;
    if (fs_get_info(&info) != ESP_OK) {
        return false;
    }

    return info.free_bytes >= required_size;
}
