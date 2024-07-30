set(srcs "src/sscma_client_ops.c"
         "src/sscma_client_io.c"
         "src/sscma_client_io_i2c.c"
         "src/sscma_client_io_spi.c"
         "src/sscma_client_io_uart.c"
         )
set(includes "include" "interface")
set(priv_requires "driver" "json" "mbedtls")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${includes}
                       PRIV_REQUIRES ${priv_requires}
                       )
