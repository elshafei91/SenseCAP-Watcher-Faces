#ifndef APP_SENSECRAFT_H
#define APP_SENSECRAFT_H

#include "config.h"
#include "view_data.h"

#define SENSECRAFT_HTTPS_URL  "http://192.168.8.105:8888"

#define IMAGE_UPLOAD_TIME_INTERVAL  10

#define IMAGE_640_480_BUF_SIZE 80*1024
#define IMAGE_240_240_BUF_SIZE 50*1024
#define SCENE_ID_DEFAULT 1


#ifdef __cplusplus
extern "C" {
#endif

int app_sensecraft_init(void);

#ifdef __cplusplus
}
#endif

#endif
