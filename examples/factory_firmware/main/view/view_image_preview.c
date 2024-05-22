#include "esp_log.h"
#include <mbedtls/base64.h>

#include "view_image_preview.h"

#define IMG_WIDTH            412
#define IMG_HEIGHT           412
#define IMG_412_412_BUF_SIZE 30 * 1024

#define IMAGE_INVOKED_BOXES 10

#define RECTANGLE_COLOR lv_palette_main(LV_PALETTE_RED)

#define IMAGE_INVOKED_BOXES_DISPLAY_ENABLE

static lv_img_dsc_t img_dsc = {
    .header.always_zero = 0,
    .header.w = IMG_WIDTH,
    .header.h = IMG_HEIGHT,
    .data_size = 0,
    .header.cf = LV_IMG_CF_RAW_ALPHA,
    .data = NULL,
};

static lv_obj_t *ui_model_name = NULL;
static lv_obj_t *ui_image = NULL;
static lv_obj_t *ui_rectangle[IMAGE_INVOKED_BOXES];
static lv_obj_t *ui_class_name[IMAGE_INVOKED_BOXES];
static uint8_t *image_buf;
static lv_color_t cls_color[20];

static void classes_color_init()
{
    cls_color[0] = lv_color_hex(0x38761d); // class gesture 1
    cls_color[1] = lv_color_hex(0x89cff0); // class gesture 2
    cls_color[2] = lv_color_hex(0x351c75); // class gesture 3
    cls_color[3] = lv_color_hex(0x53868b); // class pet
    cls_color[4] = lv_color_hex(0x1e90ff); // class human
    // cls_color[5] = lv_color_hex(0x00a86b); // class gesture 2
    // cls_color[6] = lv_color_hex(0xfcc200);
    // cls_color[7] = lv_color_hex(0x4b0082);
    // cls_color[8] = lv_color_hex(0x36648b);
    // cls_color[9] = lv_color_hex(0xffc40c);

    // cls_color[10] = lv_color_hex(0x444444);
    // cls_color[11] = lv_color_hex(0xbe29ec);
    // cls_color[12] = lv_color_hex(0xb68fa9);
    // cls_color[13] = lv_color_hex(0xa2c4c9);
    // cls_color[14] = lv_color_hex(0xadff2f);
    // cls_color[15] = lv_color_hex(0x7f1734);
    // cls_color[16] = lv_color_hex(0xf7c2c2);
    // cls_color[17] = lv_color_hex(0xd0e4e4);
    // cls_color[18] = lv_color_hex(0x98f5ff);
    // cls_color[19] = lv_color_hex(0xaaf0d1);
}

int view_image_preview_init(lv_obj_t *ui_screen)
{
    image_buf = malloc(IMG_412_412_BUF_SIZE);
    assert(image_buf);

    ui_image = lv_img_create(ui_screen);
    lv_obj_set_align(ui_image, LV_ALIGN_CENTER);

    ui_model_name = lv_label_create(ui_image);
    lv_obj_set_width(ui_model_name, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_model_name, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_model_name, 0);
    lv_obj_set_y(ui_model_name, 75);
    lv_obj_set_align(ui_model_name, LV_ALIGN_CENTER);
    // lv_label_set_text(ui_model_name, "Person Detection");
    // lv_obj_set_style_text_color(ui_model_name, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT );
    lv_obj_set_style_bg_color(ui_model_name, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_model_name, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_model_name, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);

#ifdef IMAGE_INVOKED_BOXES_DISPLAY_ENABLE
    for (size_t i = 0; i < IMAGE_INVOKED_BOXES; i++)
    {
        ui_rectangle[i] = lv_obj_create(ui_screen);
        lv_obj_add_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN);

        ui_class_name[i] = lv_label_create(ui_screen);
        lv_obj_set_width(ui_class_name[i], LV_SIZE_CONTENT);  /// 1
        lv_obj_set_height(ui_class_name[i], LV_SIZE_CONTENT); /// 1
        lv_obj_set_style_text_font(ui_class_name[i], &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
    }
#endif

    classes_color_init();
    return 0;
}

int view_image_preview_flush(struct tf_module_ai_camera_preview_info *p_info)
{
    int ret = 0;
    size_t output_len = 0;

    if (ui_image == NULL)
    {
        return -1;
    }

    ret = mbedtls_base64_decode(image_buf, IMG_412_412_BUF_SIZE, &output_len, p_info->img.p_buf, p_info->img.len);
    if (ret != 0)
    {
        ESP_LOGI("", "mbedtls_base64_decode failed: %d", ret);
        return ret;
    }

    img_dsc.data_size = output_len;
    img_dsc.data = image_buf;
    lv_img_set_src(ui_image, &img_dsc);

    if (!p_info->inference.is_valid)
    {
        return -1;
    }

    switch (p_info->inference.type)
    {
        case AI_CAMERA_INFERENCE_TYPE_BOX:
            for (size_t i = 0; i < IMAGE_INVOKED_BOXES; i++)
            {
                if (i < p_info->inference.cnt)
                {
                    int x = 0;
                    int y = 0;
                    int w = 0;
                    int h = 0;
                    sscma_client_box_t *p_box = (sscma_client_box_t *)p_info->inference.p_data;
                    x = p_box[i].x;
                    y = p_box[i].y;
                    w = p_box[i].w;
                    h = p_box[i].h;

                    x = x - w / 2;
                    y = y - h / 2;

                    if (x < 0)
                    {
                        x = 0;
                    }

                    if (y < 0)
                    {
                        y = 0;
                    }

                    char *p_class_name = p_info->inference.classes[p_box[i].target];
                    lv_color_t color = cls_color[p_box[i].target];

                    lv_obj_set_pos(ui_rectangle[i], x, y);
                    lv_obj_set_size(ui_rectangle[i], w, h);
                    lv_obj_set_style_border_color(ui_rectangle[i], color, 0);
                    lv_obj_set_style_border_width(ui_rectangle[i], 4, 0);
                    lv_obj_set_style_bg_opa(ui_rectangle[i], LV_OPA_TRANSP, 0);
                    lv_obj_clear_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN);

                    // name
                    char buf1[32];
                    lv_snprintf(buf1, sizeof(buf1), "%s:%d", p_class_name, p_box[i].score);

                    lv_obj_set_pos(ui_class_name[i], x, (y - 10) < 0 ? 0 : (y - 10));
                    lv_label_set_text(ui_class_name[i], buf1);
                    lv_obj_set_style_bg_color(ui_class_name[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(ui_class_name[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_clear_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
                }
                else
                {
                    lv_obj_add_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            break;

        case AI_CAMERA_INFERENCE_TYPE_CLASS:
            // char buf1[32];
            // char *p_class_name = p_info->inference.classes[p_box[i].target];
            // lv_snprintf(buf1, sizeof(buf1), "%s", p_class_name);
            // lv_label_set_text(ui_class_name[i], buf1);
            // lv_obj_set_style_bg_color(ui_class_name[i], cls_color[], LV_PART_MAIN | LV_STATE_DEFAULT);
            break;

        default:
            break;
    }

    return 0;
}
