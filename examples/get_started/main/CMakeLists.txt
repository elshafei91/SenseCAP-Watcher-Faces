idf_component_register(
    SRCS "get_started.c" "image.c"
    INCLUDE_DIRS "."
)

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
