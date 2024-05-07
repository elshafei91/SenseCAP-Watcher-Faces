set(UI_DIR ./ui)
file(GLOB_RECURSE UI_SRCS ${UI_DIR}/*.c)

set(APP_DIR ./app)
file(GLOB_RECURSE APP_SRCS ${APP_DIR}/*.c)

set(PM_DIR ./PageManager)
file(GLOB_RECURSE PM_SRCS ${PM_DIR}/*.c)

set(VIEW_DIR ./view)
file(GLOB_RECURSE VIEW_SRCS ${VIEW_DIR}/*.c)

set(UTIL_DIR ./util)
file(GLOB_RECURSE UTIL_SRCS ${UTIL_DIR}/*.c)


idf_component_register(
    SRCS
        "main.c"
        ${UI_SRCS}
        ${APP_SRCS}
        ${VIEW_SRCS}
        ${UTIL_SRCS}
        ${PM_SRCS}
    INCLUDE_DIRS
        "."
        ${UI_DIR}
        ${APP_DIR}
        ${VIEW_DIR}
        ${UTIL_DIR}
        ${PM_DIR}
)

if (CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG)
    # to override the CONFIG_LOG_MAXIMUM_LEVEL for 'main'
    # still need to call esp_log_level_set() for specific module.
    # The ESP-IDF doc is obscure for this.
    target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG")
endif()

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
