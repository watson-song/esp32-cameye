idf_component_register(
    SRCS 
        "main.c"
        "fs_hal.c"
        "sdcard_hal.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer fatfs esp32-camera console nvs_flash vfs
)
