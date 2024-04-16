idf_component_register(
    SRCS
        "sscma_client_proxy.c"
    INCLUDE_DIRS
        "")

set(priv_requires "mqtt")
#spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)