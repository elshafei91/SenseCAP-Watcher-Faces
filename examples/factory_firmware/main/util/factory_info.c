#include "factory_info.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "util.h"


#define FACTORY_INFO_TAG "factorynvs"

#define FACTORY_NVS_PART_NAME "nvsfactory"

#define SN          "SN_SK"
#define EUI         "EUI_SK"
#define CODE        "CODE_SK"
#define DEVICE_KEY  "DEV_KEY_SK"
#define AI_KEY      "AI_KEY_SK"
#define DEV_CTL_KEY "DEV_CTL_KEY_SK"
#define SERVER_CODE "SERVER_CODE_SK"


static bool flag = false;
static factory_info_t *gp_info = NULL;

esp_err_t factory_info_init(void)
{
    esp_err_t ret =  nvs_flash_init_partition(FACTORY_NVS_PART_NAME);
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(FACTORY_INFO_TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    nvs_handle_t handle;
    esp_err_t err;
    size_t len = 0;

    gp_info = psram_malloc(sizeof(factory_info_t));
    if( gp_info == NULL ) {
        ESP_LOGE(FACTORY_INFO_TAG, "malloc failed");
        return ESP_ERR_NO_MEM;
    }
    memset(gp_info, 0, sizeof(factory_info_t));

    err = nvs_open(FACTORY_NVS_PART_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        free(gp_info);
        return err;
    }
    
    flag = true;

    nvs_get_str(handle, SN, NULL, &len);
    if (len > 0)
    {
        gp_info->sn = psram_malloc(len);
        nvs_get_str(handle, SN, gp_info->sn, &len);
        printf("#SN: %s\n", gp_info->sn);
    } else {
        printf("No SN value found in NVS\n");
    }

    nvs_get_str(handle, EUI, NULL, &len);
    if (len > 0)
    {
        gp_info->eui = psram_malloc(len);
        nvs_get_str(handle, EUI, gp_info->eui, &len);
        printf("#EUI: %s\n", gp_info->eui);
    } else {
        printf("No EUI value found in NVS\n");
    }

    nvs_get_str(handle, CODE, NULL, &len);
    if (len > 0)
    {
        gp_info->code = psram_malloc(len);
        nvs_get_str(handle, CODE, gp_info->code, &len);
        printf("#CODE: %s\n", gp_info->code);
    } else {
        printf("No CODE value found in NVS\n");
    }

    nvs_get_str(handle, DEVICE_KEY, NULL, &len);
    if (len > 0)
    {
        gp_info->device_key = psram_malloc(len);
        nvs_get_str(handle, DEVICE_KEY, gp_info->device_key, &len);
        printf("#DEVICE_KEY: %s\n", gp_info->device_key);
    } else {
        printf("No DEVICE_KEY value found in NVS\n");
    }

    nvs_get_str(handle, AI_KEY, NULL, &len);
    if (len > 0)
    {
        gp_info->ai_key = psram_malloc(len);
        nvs_get_str(handle, AI_KEY, gp_info->ai_key, &len);
        printf("#AI_KEY: %s\n", gp_info->ai_key);
    } else {
        printf("No AI_KEY value found in NVS\n");
    }   

    nvs_get_str(handle, DEV_CTL_KEY, NULL, &len);
    if (len > 0)
    {
        gp_info->device_control_key = psram_malloc(len);
        nvs_get_str(handle, DEV_CTL_KEY, gp_info->device_control_key, &len);
        printf("#DEV_CTL_KEY: %s\n", gp_info->device_control_key);
    } else {
        printf("No DEV_CTL_KEY value found in NVS\n");
    }

    nvs_get_str(handle, SERVER_CODE, NULL, &len);
    if (len > 0)
    {
         char *value = psram_malloc(len);

        nvs_get_str(handle, SERVER_CODE, value, &len);

        if (strcmp(value, "1") == 0) { // TODO
            gp_info->server = true;
        } else {
            gp_info->server = false;  
        }
        free(value);
        printf("#SERVER: %d\n", gp_info->server);
    } else {
        gp_info->server = false;
    } 
    nvs_close(handle);
    return ESP_OK;
}


const factory_info_t *factory_info_get(void)
{
    if (gp_info == NULL && !flag) {
        return NULL;
    }
    return ( const factory_info_t *)gp_info;
}

esp_err_t factory_info_get1(factory_info_t *p_info)
{
    if( gp_info == NULL && !flag) {
        return ESP_FAIL;
    }
    memcpy(p_info, gp_info, sizeof(factory_info_t));
    return ESP_OK;
}