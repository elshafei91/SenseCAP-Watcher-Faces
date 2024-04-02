#pragma once

#include "esp_event_base.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif


ESP_EVENT_DECLARE_BASE(VIEW_EVENT_BASE);
extern esp_event_loop_handle_t view_event_handle;

ESP_EVENT_DECLARE_BASE(CTRL_EVENT_BASE);
extern esp_event_loop_handle_t ctrl_event_handle;

#ifdef __cplusplus
}
#endif
