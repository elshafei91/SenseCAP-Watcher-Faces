#ifndef SYSTEM_LAYER_H
#define SYSTEM_LAYER_H




enum {
    UI_CALLER,
    AT_CMD_CALLER,
    BLE_CALLER
};

int caller_reg();       //register caller
int caller_dereg();     //deregister caller

void system_layer_init();

#endif // SYSTEM_LAYER_H