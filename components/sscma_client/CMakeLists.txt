set(srcs "src/sscma_client_ops.c"
         "src/sscma_client_io.c"
         "src/sscma_client_io_i2c.c"
         "src/sscma_client_io_spi.c"
         "src/sscma_client_io_uart.c"
         "src/sscma_client_proto.c"
         "src/sscma_client_proto_xmodem.c"
         )
set(includes "include" "interface" "protocol")
set(priv_requires "driver" "json" "mbedtls" "esp_timer")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${includes}
                       PRIV_REQUIRES ${priv_requires}
                       )
