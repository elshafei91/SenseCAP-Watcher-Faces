set(UI_DIR ./ui)
file(GLOB_RECURSE UI_SRCS ${UI_DIR}/*.c)

set(APP_DIR ./app)
file(GLOB_RECURSE APP_SRCS ${APP_DIR}/*.c)

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
    INCLUDE_DIRS
        "."
        ${UI_DIR}
        ${APP_DIR}
        ${VIEW_DIR}
        ${UTIL_DIR}
)

# spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
