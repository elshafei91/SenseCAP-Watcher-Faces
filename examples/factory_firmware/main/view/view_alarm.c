#include "view_alarm.h"
#include "ui/ui.h"
#include "ui_manager/pm.h"
#include "esp_timer.h"
#include "data_defs.h"

#include <mbedtls/base64.h>
#include "esp_jpeg_dec.h"
#include "util.h"

uint8_t emoticon_disp_id = 0;
lv_obj_t *ui_alarm_indicator;
lv_anim_t a;

extern uint8_t wifi_page_id;
extern int first_use;
extern uint8_t guide_step;

static int16_t indicator_value = 0;
static lv_obj_t * ui_image = NULL;
static uint8_t task_view_current = 0;

static uint8_t *image_jpeg_buf = NULL;
static uint8_t *image_ram_buf = NULL;

static lv_img_dsc_t img_dsc = {
    .header.always_zero = 0,
    .header.w = IMG_WIDTH,
    .header.h = IMG_HEIGHT,
    .data_size = IMG_RAM_BUF_SIZE,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};

static void alarm_timer_callback(void *arg);
static const esp_timer_create_args_t alarm_timer_args = { .callback = &alarm_timer_callback, .name = "alarm_on" };
static esp_timer_handle_t alarm_timer;

static void alarm_timer_callback(void *arg)
{
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, NULL, NULL, portMAX_DELAY);
}

static void set_angle(void *obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

static void alarm_timer_start(int s)
{
    if (esp_timer_is_active(alarm_timer))
    {
        esp_timer_stop(alarm_timer);
    }
    ESP_ERROR_CHECK(esp_timer_start_once(alarm_timer, (uint64_t)s * 1000000));
}

static void alarm_timer_stop()
{
    if (esp_timer_is_active(alarm_timer))
    {
        esp_timer_stop(alarm_timer);
    }
}

int view_alarm_init(lv_obj_t *ui_screen)
{
    image_jpeg_buf = psram_malloc(IMG_JPEG_BUF_SIZE);
    assert(image_jpeg_buf);

    //must be 16 byte aligned
    image_ram_buf = heap_caps_aligned_alloc(16, IMG_RAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    assert(image_ram_buf);

    ui_alarm_indicator = lv_arc_create(ui_screen);
    lv_obj_set_width(ui_alarm_indicator, 412);
    lv_obj_set_height(ui_alarm_indicator, 412);
    lv_obj_set_align(ui_alarm_indicator, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_value(ui_alarm_indicator, 0);
    lv_arc_set_rotation(ui_alarm_indicator, 270);
    lv_arc_set_bg_angles(ui_alarm_indicator, 0, 360);

    lv_obj_set_style_arc_color(ui_alarm_indicator, lv_color_hex(0x4040FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_alarm_indicator, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_alarm_indicator, 40, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_img_src(ui_alarm_indicator, &ui_img_gradient_png, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_alarm_indicator, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_alarm_indicator, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    ui_image = lv_img_create(ui_viewlivp2);
    lv_obj_set_align(ui_image, LV_ALIGN_CENTER);

    ESP_ERROR_CHECK(esp_timer_create(&alarm_timer_args, &alarm_timer));

    return 0;
}

static int esp_jpeg_decoder_one_picture(uint8_t *input_buf, int len, uint8_t *output_buf)
{
    esp_err_t ret = ESP_OK;
    jpeg_dec_config_t config = { .output_type = JPEG_RAW_TYPE_RGB565_BE, .rotate = JPEG_ROTATE_0D };
    jpeg_dec_handle_t jpeg_dec = NULL;
    jpeg_dec = jpeg_dec_open(&config);

    if (jpeg_dec == NULL) {
        return ESP_FAIL;
    }

    jpeg_dec_io_t *jpeg_io = heap_caps_malloc(sizeof(jpeg_dec_io_t), MALLOC_CAP_SPIRAM);
    if (jpeg_io == NULL) {
        jpeg_dec_close(jpeg_dec);
        return ESP_FAIL;
    }
    memset(jpeg_io, 0, sizeof(jpeg_dec_io_t));

    jpeg_dec_header_info_t *out_info = heap_caps_aligned_alloc(16, sizeof(jpeg_dec_header_info_t), MALLOC_CAP_SPIRAM);
    if (out_info == NULL) {
        heap_caps_free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return ESP_FAIL;
    }
    memset(out_info, 0, sizeof(jpeg_dec_header_info_t));

    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;
    ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
    if (ret < 0) {
        goto _exit;
    }

    jpeg_io->outbuf = output_buf;
    int inbuf_consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = input_buf + inbuf_consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;

    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    if (ret < 0) {
        goto _exit;
    }

_exit:
    jpeg_dec_close(jpeg_dec);
    heap_caps_free(out_info);
    heap_caps_free(jpeg_io);
    return ret;
}


int view_alarm_on(struct tf_module_local_alarm_info *alarm_st)
{
    if((!first_use) && (guide_step != 3)){return 0;}
    if((lv_scr_act() != ui_Page_ViewAva) && (lv_scr_act() != ui_Page_ViewLive)){return 0;}
    // for switch avatar emoticon
    emoticon_disp_id = 1;
    // send focused event to call function
    lv_event_send(ui_Page_ViewAva, LV_EVENT_SCREEN_LOADED, NULL);
    alarm_timer_start(alarm_st->duration);

    // turn the page to view live
    if ((lv_scr_act() != ui_Page_ViewLive)){
        task_view_current = 0;
        lv_pm_open_page(g_main, &group_page_view, PM_ADD_OBJS_TO_GROUP, &ui_Page_ViewLive, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_ViewLive_screen_init);
        lv_group_focus_obj(ui_Page_ViewLive);
    }else
    {
        task_view_current = 1;
    }
    // clear alarm text
    lv_label_set_text(ui_viewtext, "");
    // record the page when the alarm is triggered
    // text display
    if (alarm_st->is_show_text)
    {
        struct tf_data_buf *alarm_text = &alarm_st->text;
        if (alarm_text->p_buf != NULL)
        {
            lv_label_set_text(ui_viewtext, (const char *)alarm_text->p_buf);
            tf_data_buf_free(alarm_text);
        }
    }
    // image display 
    if (alarm_st->is_show_img)
    {
        struct tf_data_image *alarm_img = &alarm_st->img;
        if (alarm_img->p_buf != NULL) {

            int ret = 0; 
            size_t output_len = 0;       
            ret = mbedtls_base64_decode(image_jpeg_buf, IMG_JPEG_BUF_SIZE, &output_len, alarm_img->p_buf, alarm_img->len);
            if (ret != 0 || output_len == 0)
            {
                ESP_LOGE("view", "Failed to decode base64: %d", ret);
                return ret;
            }

            ret = esp_jpeg_decoder_one_picture(image_jpeg_buf, output_len, image_ram_buf);
            if (ret != ESP_OK) {
                ESP_LOGE("view", "Failed to decode jpeg: %d", ret);
                return ret;
            }

            img_dsc.data = image_ram_buf;
            lv_img_set_src(ui_image, &img_dsc);
            lv_obj_clear_flag(ui_image, LV_OBJ_FLAG_HIDDEN);
            tf_data_image_free(&alarm_st->img);
        }
    }
    // initial indicator and state
    if(lv_scr_act() == ui_Page_ViewLive){
        indicator_value = 0;
        lv_arc_set_value(ui_alarm_indicator, indicator_value);
        lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_viewlivp2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(ui_image);
        lv_obj_move_foreground(ui_viewlivp2);

        // alarm indicator animation start
        lv_anim_init(&a);
        lv_anim_set_var(&a, ui_alarm_indicator);
        lv_anim_set_exec_cb(&a, set_angle);
        lv_anim_set_time(&a, (alarm_st->duration) * 1000);
        lv_anim_set_values(&a, 10, 100);
        lv_anim_start(&a);
    }

    return ESP_OK;
}

void view_alarm_off(uint8_t task_down)
{    
    alarm_timer_stop();
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_viewlivp2, LV_OBJ_FLAG_HIDDEN);
    // for switch avatar emoticon
    emoticon_disp_id = 0;
    if((lv_scr_act() != ui_Page_ViewAva) && (lv_scr_act() != ui_Page_ViewLive)){return ;}
    lv_event_send(ui_Page_ViewAva, LV_EVENT_SCREEN_LOADED, NULL);

    // if the page is avatar when the alarm is triggered, turn the page back when the alarm is off
    if(task_view_current == 0 && task_down == 0)
    {
        _ui_screen_change(&ui_Page_ViewAva, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_ViewAva_screen_init);
        lv_group_focus_obj(ui_Page_ViewAva);
    }
}