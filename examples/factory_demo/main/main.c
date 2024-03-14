#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"

#include "indoor_ai_camera.h"

#include "storage.h"
// #include "audio_player.h"
// #include "app_sr.h"
// #include "app_audio.h"
#include "app_wifi.h"
#include "app_time.h"
#include "app_cmd.h"
#include "app_sensecraft.h"
#include "app_tasklist.h"
#include "app_sscma_client.h"
#include "app_rgb.h"

#include "view.h"


static const char *TAG = "app_main";

#define VERSION   "v1.0.0"

#define SENSECAP  "\n\
   _____                      _________    ____         \n\
  / ___/___  ____  ________  / ____/   |  / __ \\       \n\
  \\__ \\/ _ \\/ __ \\/ ___/ _ \\/ /   / /| | / /_/ /   \n\
 ___/ /  __/ / / (__  )  __/ /___/ ___ |/ ____/         \n\
/____/\\___/_/ /_/____/\\___/\\____/_/  |_/_/           \n\
--------------------------------------------------------\n\
 Version: %s %s %s\n\
--------------------------------------------------------\n\
"

ESP_EVENT_DEFINE_BASE(VIEW_EVENT_BASE);
esp_event_loop_handle_t view_event_handle;

ESP_EVENT_DEFINE_BASE(CTRL_EVENT_BASE);
esp_event_loop_handle_t ctrl_event_handle;

int board_init(void)
{
    storage_init();
    
    bsp_io_expander_init();

    lv_disp_t *lvgl_disp = bsp_lvgl_init();
    assert(lvgl_disp != NULL);

    bsp_rgb_init();


    // bsp_lcd_brightness_set(100);


    return ESP_OK;
}

int app_init(void)
{
    app_rgb_init();
    app_sensecraft_init();
    app_sscma_client_init();

    // app_sr_start(false);

    app_wifi_init();
    app_time_init();
    app_cmd_init();
    return ESP_OK;
}


void app_main(void)
{
    ESP_LOGI("", SENSECAP, VERSION, __DATE__, __TIME__);

    ESP_ERROR_CHECK(board_init());

    esp_event_loop_args_t view_event_task_args = {
        .queue_size = 10,
        .task_name = "view_event_task",
        .task_priority =1, // uxTaskPriorityGet(NULL),
        .task_stack_size = 10240,
        .task_core_id = tskNO_AFFINITY
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&view_event_task_args, &view_event_handle));

    // esp_event_loop_args_t ctrl_event_task_args = {
    //     .queue_size = 10,
    //     .task_name = "ctrl_event_task",
    //     .task_priority = uxTaskPriorityGet(NULL),
    //     .task_stack_size = 1024*5,
    //     .task_core_id = tskNO_AFFINITY
    // };
    // ESP_ERROR_CHECK(esp_event_loop_create(&ctrl_event_task_args, &ctrl_event_handle));

   // UI init
    view_init();

    // app init
    app_init();

   
    // static struct view_data_wifi_config cfg;
    // cfg.have_password = true;
    // strcpy( cfg.ssid, "TP-LINK_CS" );
    // strcpy( cfg.password, "Emo8568..");
    // esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, &cfg, sizeof(struct view_data_wifi_config), portMAX_DELAY);

    
    static char buffer[254];    /* Make sure buffer is enough for `sprintf` */
    while (1) {
        sprintf(buffer, "   Biggest /     Free /    Total\n"
                "\t  DRAM : [%8d / %8d / %8d]\n"
                "\t  PSRAM : [%8d / %8d / %8d]\n"
                "\t  DMA : [%8d / %8d / %8d]",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                heap_caps_get_free_size(MALLOC_CAP_DMA),
                heap_caps_get_total_size(MALLOC_CAP_DMA));

        ESP_LOGI("MEM", "%s", buffer);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
