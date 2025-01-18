idf_component_register(
    SRCS 
        "fs_hal.c"
        "sdcard_hal.c"
        "main.c"  # 使用新的测试文件
    INCLUDE_DIRS "."
)
