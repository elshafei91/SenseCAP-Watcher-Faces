idf_component_register(
    SRCS
        "lvgl_demo.c" "playwav.c"
    INCLUDE_DIRS
        "." "./ui")

# spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)