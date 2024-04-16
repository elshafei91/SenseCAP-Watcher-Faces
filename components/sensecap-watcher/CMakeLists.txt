
set(priv_requires "esp_mm" "esp_psram" "esp_lcd" "fatfs" "esp_timer")
set(requires "driver" "spiffs" "lvgl")

idf_component_register(
    SRCS "sensecap-watcher.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
    REQUIRES ${requires}
)
