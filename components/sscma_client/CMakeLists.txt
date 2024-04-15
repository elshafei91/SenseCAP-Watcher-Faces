set(srcs "src/sscma_client_ops.c"
         "src/sscma_client_io.c"
         "src/sscma_client_io_i2c.c"
         "src/sscma_client_io_spi.c"
         "src/sscma_client_io_uart.c"
         "src/sscma_client_flasher.c"
         "src/sscma_client_flasher_we2.c"
         )
set(includes "include" "interface")
set(priv_requires "driver" "json" "mbedtls" "esp_timer")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${includes}
                       PRIV_REQUIRES ${priv_requires}
                       )
