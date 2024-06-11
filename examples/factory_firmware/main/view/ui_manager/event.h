#ifndef EVENT_H
#define EVENT_H

#include "ui/ui.h"

// taskflow error msg event
void taskerrc_cb(lv_event_t *e);

// Wi-Fi config status
void waitForWifi();
void waitForBinding();
void waitForAddDev();
void bindFinish();
void wifiConnectFailed();

// view alarm panel event
void ui_event_alarm_panel(lv_event_t * e);

#endif