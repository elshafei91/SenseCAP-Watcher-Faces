#include "app_tasklist.h"
#include "cJSON.h"
// #include "audio_player.h"
#include <mbedtls/base64.h>

static const char *TAG = "tasklist";

char* tasklist_parse(char *resp)
{
    int ret = 0;
    char * result = NULL;
    int warnStatus_flag = 0;

    char text_str[128];
    memset(text_str, 0, sizeof(text_str));

    ESP_LOGI(TAG, "RESP:%s", resp);

    cJSON *json = cJSON_Parse(resp);

    cJSON *code, *data, *sceneId, *tasklist, *warnStatus, *tasklist_len; 

    code = cJSON_GetObjectItem(json, "code");
    if( code != NULL  && code->valueint != NULL) {
        ESP_LOGI(TAG, "code: %d", code->valueint);
        ret = code->valueint;
    }

    data = cJSON_GetObjectItem(json, "data");

    sceneId = cJSON_GetObjectItem(data, "sceneId");
    if( sceneId != NULL  && sceneId->valueint != NULL) {
        ESP_LOGI(TAG, "sceneId: %d", sceneId->valueint);
    }

    warnStatus = cJSON_GetObjectItem(data, "warnStatus");
    if (warnStatus != NULL &&  warnStatus->valueint != NULL) {
        ESP_LOGI(TAG, "warnStatus: %d", warnStatus->valueint);
        warnStatus_flag = warnStatus->valueint;
    }    

    tasklist = cJSON_GetObjectItem(data, "taskList");
    if (tasklist == NULL || !cJSON_IsArray(tasklist)) {
        ESP_LOGE(TAG, "tasklist is not array");
        goto end;
    }
    tasklist_len = cJSON_GetArraySize(tasklist);
    for ( int i = 0; i < tasklist_len; i++)
    {
        cJSON *item = cJSON_GetArrayItem(tasklist, i);
        if (item == NULL || !cJSON_IsObject(item)) {
            ESP_LOGE(TAG, "item is not object");
            goto end;
        }
        cJSON *hardware = cJSON_GetObjectItem(item, "hardware");
        if( hardware != NULL  && hardware->valueint != NULL) {
            ESP_LOGI(TAG, "hardware: %d", hardware->valueint);
        }
        cJSON *action = cJSON_GetObjectItem(item, "action");
        if( action != NULL  && action->valueint != NULL) {
            ESP_LOGI(TAG, "action: %d", action->valueint);
        }

        cJSON *params = cJSON_GetObjectItem(item, "params");
        if( params != NULL ) {

            switch ( hardware->valueint )
            {
                case TASK_ACTION_HW_ID_LCD:
                {
                    cJSON  *text = cJSON_GetObjectItem(params, "text");
                    if( text != NULL  && text->valuestring != NULL) {
                        ESP_LOGI(TAG, "text: %s", text->valuestring);
                        snprintf(text_str, sizeof(text_str)-1, "%s", text->valuestring);
                    }
                    break;
                }
                case TASK_ACTION_HW_ID_AUTIO_PLAY:
                {
                    cJSON  *audio = cJSON_GetObjectItem(params, "audio");
                    if( audio != NULL  && audio->valuestring != NULL) {
                       
                        size_t len  = 0;
                        int buf_len = strlen(audio->valuestring);
                        uint8_t * audio_bin = malloc(buf_len);

                        ESP_LOGI(TAG, "audio size: %d", buf_len);
                        if(audio_bin != NULL) {
                            int len  = 0;
                            int ret= mbedtls_base64_decode(audio_bin, buf_len, (size_t *)&len, (const unsigned char *) audio->valuestring, buf_len );
                            FILE *fp = fmemopen((void *)audio_bin, len, "rb");
                            if (fp) {
                                // audio_player_play(fp);
                                // vTaskDelay(pdMS_TO_TICKS(5 * 1000)); //todo
                            }
                            free(audio_bin);
                        }
                    }
                    break;
                }

                default:
                    break;
            }
            if( hardware->valueint ==1 ) {

            }
        }

    }

    if( warnStatus_flag ) {
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, text_str, sizeof(text_str), portMAX_DELAY);
    }
   
    
end:
    cJSON_Delete(json);


    return result;
}

