
set(priv_requires "esp_mm" "esp_psram" "esp_lcd" "fatfs")
set(requires "driver" "spiffs")

idf_component_register(
    SRCS "indoor_ai_camera.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
    REQUIRES ${requires}
)
