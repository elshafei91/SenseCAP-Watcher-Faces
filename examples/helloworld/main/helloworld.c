#include <dirent.h>
#include <stdio.h>


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    while (1)
    {
        printf("Hello World!\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}