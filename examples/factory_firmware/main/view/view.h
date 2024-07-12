#ifndef VIEW_H
#define VIEW_H

#include "data_defs.h"
#include "lvgl.h"
#include "ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the waiting timer.
 * 
 * If the timer is already active, stop it first. Then start the waiting timer with a period of 1 second (1000000 microseconds).
 */
void wait_timer_start();

/**
 * @brief Initialize the view system.
 * 
 * Initialize the display, UI components, timers, and event handlers.
 * 
 * @return int Returns 0 on success.
 */
int view_init(void);

/**
 * @brief Render the screen in black.
 * 
 * Lock the LVGL port, perform the operation to render the screen in black, and then unlock the LVGL port.
 */
void view_render_black(void);

#ifdef __cplusplus
}
#endif

#endif
