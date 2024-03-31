#ifndef APP_TASKLIST_H
#define APP_TASKLIST_H

#include "config.h"
#include "data_defs.h"


#ifdef __cplusplus
extern "C" {
#endif

#define TASK_ACTION_HW_ID_LCD    0        //显示
#define TASK_ACTION_HW_ID_AUTIO_PLAY  1   // 语音播放
#define TASK_ACTION_HW_ID_RECORD    2     //录音
#define TASK_ACTION_HW_ID_PHOTOGRAPH   3  // 拍照


void tasklist_init(void);

char* tasklist_parse(char *resp);

int tasklist_image_get (char ** pp_img);

#ifdef __cplusplus
}
#endif

#endif
