/*
 * @Author: QingWind6 993628705@qq.com
 * @Date: 2024-07-11 15:00:47
 * @LastEditors: QingWind6 993628705@qq.com
 * @LastEditTime: 2024-07-12 18:11:44
 * @FilePath: \SenseCAP-Watcher\examples\factory_firmware\main\view\ui_manager\event.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef EVENT_H
#define EVENT_H

#include "ui/ui.h"

extern lv_obj_t *ui_Left1;
extern lv_obj_t *ui_Left2;
extern lv_obj_t *ui_Left3;
extern lv_obj_t *ui_Left4;
extern lv_obj_t *ui_Left5;
extern lv_obj_t *ui_Left6;
extern lv_obj_t *ui_Left7;
extern lv_obj_t *ui_Left8;
extern lv_obj_t *ui_Right1;
extern lv_obj_t *ui_Right2;
extern lv_obj_t *ui_Right3;
extern lv_obj_t *ui_Right4;
extern lv_obj_t *ui_Right5;
extern lv_obj_t *ui_Right6;
extern lv_obj_t *ui_Right7;
extern lv_obj_t *ui_Right8;

/**
 * @brief Callback function for handling the task flow error message event.
 * 
 * This function hides the task error UI element and posts a task flow stop event.
 * 
 * @param e Pointer to the event structure.
 */
void taskerrc_cb(lv_event_t *e);

// Wi-Fi config status
void waitForWifi();
void waitForBinding();
void waitForAddDev();
void bindFinish();
void wifiConnectFailed();

/**
 * @brief Event handler for the alarm panel UI.
 * 
 * This function handles various events for the alarm panel, such as button clicks and focus changes.
 * 
 * @param e Pointer to the event structure.
 */
void ui_event_alarm_panel(lv_event_t * e);

/**
 * @brief Event handler for the emoji OTA (Over-The-Air) update.
 * 
 * This function handles the click event for the emoji update confirmation button.
 * 
 * @param e Pointer to the event structure.
 */
void ui_event_emoticonok(lv_event_t * e);

/**
 * @brief Initialize view information.
 * 
 * This function sets up the initial states and properties for various UI elements.
 */
void viewInfoInit();

/**
 * @brief Obtain view information.
 * 
 * This function retrieves and sets up various settings and information for the UI elements.
 */
void view_info_obtain();

/**
 * @brief Start a timer for the emoji animation.
 * 
 * This function initializes and starts a timer to handle the animation of emojis based on the specified emoji type.
 * 
 * @param emoji_type The type of emoji animation to be displayed.
 */
void emoji_timer(uint8_t emoji_type);

enum
{
    SCREEN_VIRTUAL, // display emoticon on virtual page
    SCREEN_AVATAR,  // display emoticon on avatar page
    SCREEN_GUIDE,    // display emoticon on guide page
    SCREEN_STANDBY  // display emoticon on standby page
};

enum
{
    EMOJI_GREETING,
    EMOJI_DETECTING,
    EMOJI_DETECTED,
    EMOJI_SPEAKING,
    EMOJI_LISTENING,
    EMOJI_ANALYZING,
    EMOJI_STANDBY,
    EMOJI_STOP
};

#endif