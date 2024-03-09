#ifndef APP_SSCMA_CLIENT_H
#define APP_SSCMA_CLIENT_H

#include "config.h"
#include "view_data.h"

/* SPI settings */
#define EXAMPLE_SSCMA_SPI_NUM (SPI3_HOST)
#define EXAMPLE_SSCMA_SPI_CLK_HZ (12 * 1000 * 1000)

/* SPI pins */
#define EXAMPLE_SSCMA_SPI_SCLK (4)
#define EXAMPLE_SSCMA_SPI_MOSI (5)
#define EXAMPLE_SSCMA_SPI_MISO (6)
#define EXAMPLE_SSCMA_SPI_CS (21)
#define EXAMPLE_SSCMA_SPI_SYNC (14)

#define EXAMPLE_SSCMA_RESET (-1)


#ifdef __cplusplus
extern "C" {
#endif

int app_sscma_client_init();

#ifdef __cplusplus
}
#endif

#endif
