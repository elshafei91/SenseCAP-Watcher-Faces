#ifndef APP_SENSECRAFT_H
#define APP_SENSECRAFT_H

#include "data_defs.h"

#define SENSECRAFT_HTTPS_URL  "http://192.168.100.10:8888"

#define IMAGE_UPLOAD_TIME_INTERVAL  10

#define IMAGE_640_480_BUF_SIZE 80*1024
#define IMAGE_240_240_BUF_SIZE 50*1024
#define SCENE_ID_DEFAULT 3


#ifdef __cplusplus
extern "C" {
#endif

int app_sensecraft_init(void);

int app_sensecraft_image_upload(struct view_data_image *p_data);

int app_sensecraft_image_invoke_check(struct view_data_image_invoke *p_data);

struct view_data_image_invoke * app_sensecraft_image_invoke_get(void);

#ifdef __cplusplus
}
#endif

#endif
