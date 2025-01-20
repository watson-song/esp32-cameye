idf_component_register(
    SRCS 
        "main.c"
        "fs_hal.c"
        "sdcard_hal.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer fatfs esp_camera console nvs_flash vfs
)
