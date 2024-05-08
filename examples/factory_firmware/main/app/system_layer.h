#ifndef SYSTEM_LAYER_H
#define SYSTEM_LAYER_H


enum {
    UI_CALLER,
    AT_CMD_CALLER
}caller_id_t;

enum {
    
}

int caller_reg();       //register caller
int caller_dereg();     //deregister caller


#endif // SYSTEM_LAYER_H