
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "indoor_ai_camera.h"

static const char *TAG = "BSP";

static led_strip_handle_t rgb_led_handle = NULL;
static esp_io_expander_handle_t io_exp_handle = NULL;

static uint16_t io_exp_val = 0;
static volatile bool io_exp_update = false;
static void io_exp_isr_handler(void* arg) { io_exp_update = true; }

uint8_t bsp_exp_io_get_level(uint16_t pin_mask)
{
    if (io_exp_update && (io_exp_handle != NULL)) {
        uint32_t pin_val = 0;
        esp_io_expander_get_level(io_exp_handle, DRV_IO_EXP_INPUT_MASK, &pin_val);
        io_exp_val = (io_exp_val & (~DRV_IO_EXP_INPUT_MASK)) | pin_val;
        io_exp_update = false;
    }
    
    pin_mask &= DRV_IO_EXP_INPUT_MASK;
    return (uint8_t)((io_exp_val & pin_mask) ? 1 : 0);
}

esp_err_t bsp_exp_io_set_level(uint16_t pin_mask, uint8_t level)
{
    esp_err_t ret = ESP_OK;
    pin_mask &= DRV_IO_EXP_OUTPUT_MASK;
    if (pin_mask ^ (io_exp_val & DRV_IO_EXP_OUTPUT_MASK)) { // Output pins changed
        ret = esp_io_expander_set_level(io_exp_handle, pin_mask, level);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set output level");
            return ret;
        }
        io_exp_val = (io_exp_val & (~pin_mask)) | (level ? pin_mask : 0);
    }
    return ret;
}

esp_err_t bsp_rgb_init()
{
    led_strip_config_t bsp_strip_config = {
        .strip_gpio_num = BSP_RGB_CTRL,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t bsp_rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&bsp_strip_config, &bsp_rmt_config, &rgb_led_handle));
    led_strip_set_pixel(rgb_led_handle, 0, 0x00, 0x00, 0x00);
    led_strip_refresh(rgb_led_handle);

    return ESP_OK;
}

esp_err_t bsp_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    esp_err_t ret = ESP_OK;
    uint32_t index = 0;

    ret |= led_strip_set_pixel(rgb_led_handle, index, r, g, b);
    ret |= led_strip_refresh(rgb_led_handle);
    return ret;
}

static esp_err_t bsp_knob_btn_init(void *param)
{
    esp_io_expander_handle_t io_exp = *((esp_io_expander_handle_t *)param);
    
    io_exp = bsp_io_expander_init();
    if (io_exp == NULL) {
        ESP_LOGE(TAG, "IO expander initialization failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static uint8_t bsp_knob_btn_get_key_value(void *param)
{
    esp_io_expander_handle_t io_exp = *((esp_io_expander_handle_t *)param);
    return bsp_exp_io_get_level(BSP_KNOB_BTN);
}

static esp_err_t bsp_knob_btn_deinit(void *param)
{
    esp_io_expander_handle_t io_exp_handle = *((esp_io_expander_handle_t *)param);
    return esp_io_expander_del(io_exp_handle);
}

static esp_err_t bsp_lcd_backlight_init()
{
    // const ledc_channel_config_t backlight_channel = {
    //     .gpio_num = BSP_LCD_GPIO_BL,
    //     .speed_mode = LEDC_LOW_SPEED_MODE,
    //     .channel = DRV_LCD_LEDC_CH,
    //     .intr_type = LEDC_INTR_DISABLE,
    //     .timer_sel = LEDC_TIMER_1,
    //     .duty = BIT(DRV_LCD_LEDC_DUTY_RES),
    //     .hpoint = 0};
    // const ledc_timer_config_t backlight_timer = {
    //     .speed_mode = LEDC_LOW_SPEED_MODE,
    //     .duty_resolution = DRV_LCD_LEDC_DUTY_RES,
    //     .timer_num = LEDC_TIMER_1,
    //     .freq_hz = 5000,
    //     .clk_cfg = LEDC_AUTO_CLK};

    // ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer));
    // ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel));

    // ESP_ERROR_CHECK(bsp_lcd_brightness_set(10));

    return ESP_OK;
}

esp_err_t bsp_lcd_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100)
    {
        brightness_percent = 100;
    }
    if (brightness_percent < 0)
    {
        brightness_percent = 0;
    }

    ESP_LOGD(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    uint32_t duty_cycle = (BIT(DRV_LCD_LEDC_DUTY_RES) * (brightness_percent)) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, DRV_LCD_LEDC_CH, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, DRV_LCD_LEDC_CH));

    return ESP_OK;
}

static esp_err_t bsp_lcd_pannel_init(esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_ERROR(bsp_lcd_backlight_init(), TAG, "Brightness init failed");

    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_GPIO_DC,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .pclk_hz = DRV_LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = DRV_LCD_CMD_BITS,
        .lcd_param_bits = DRV_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi(DRV_LCD_SPI_NUM, &io_config, ret_io), err,
                      TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_GPIO_RST, // Shared with Touch reset
        .color_space = DRV_LCD_COLOR_SPACE,
        .bits_per_pixel = DRV_LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_gc9a01(*ret_io, &panel_config, ret_panel), err,
                      TAG, "New panel failed");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*ret_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*ret_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*ret_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(*ret_panel, DRV_LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(*ret_panel, DRV_LCD_MIRROR_X, DRV_LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*ret_panel, true));

    return ret;
err:
    if (*ret_panel)
        esp_lcd_panel_del(*ret_panel);
    if (*ret_io)
        esp_lcd_panel_io_del(*ret_io);
    spi_bus_free(DRV_LCD_SPI_NUM);
    return ret;
}

static lv_disp_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);

    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_SPI_SCLK,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = DRV_LCD_H_RES * LVGL_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    if (spi_bus_initialize(DRV_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK)
        return NULL;

    ESP_LOGD(TAG, "Initialize LCD panel");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    if (bsp_lcd_pannel_init(&panel_handle, &io_handle) != ESP_OK)
        return NULL;

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = cfg->buffer_size,
        .double_buffer = cfg->double_buffer,
        .hres = DRV_LCD_H_RES,
        .vres = DRV_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = DRV_LCD_SWAP_XY,
            .mirror_x = DRV_LCD_MIRROR_X,
            .mirror_y = DRV_LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = cfg->flags.buff_dma,
            .buff_spiram = cfg->flags.buff_spiram,
        }};

    return lvgl_port_add_disp(&disp_cfg);
}

static lv_indev_t *bsp_knob_indev_init(lv_disp_t *disp)
{
    ESP_LOGI(TAG, "Initialize knob input device");
    const static knob_config_t knob_cfg = {
        .default_direction = 0,
        .gpio_encoder_a = BSP_KNOB_A,
        .gpio_encoder_b = BSP_KNOB_B,
    };
    const static button_config_t btn_config = {
        .type = BUTTON_TYPE_CUSTOM,
        .long_press_time = 1000,
        .short_press_time = 200,
        .custom_button_config = {
            .active_level = 0,
            .button_custom_init = bsp_knob_btn_init,
            .button_custom_deinit = bsp_knob_btn_deinit,
            .button_custom_get_key_value = bsp_knob_btn_get_key_value,
            .priv = &io_exp_handle,
        },
    };
    const lvgl_port_encoder_cfg_t encoder = {
        .disp = disp,
        .encoder_a_b = &knob_cfg,
        .encoder_enter = &btn_config
    };
    return lvgl_port_add_encoder(&encoder);
}

static lv_indev_t *bsp_touch_indev_init(lv_disp_t *disp)
{
    /* Initilize I2C */
    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_TOUCH_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BSP_TOUCH_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BSP_TOUCH_I2C_CLK};
    if ((i2c_param_config(BSP_TOUCH_I2C_NUM, &i2c_conf) != ESP_OK) ||
        (i2c_driver_install(BSP_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, 0) != ESP_OK))
    {
        ESP_LOGE(TAG, "I2C initialization failed");
        return NULL;
    }

    /* Initialize touch HW */
    ESP_LOGI(TAG, "Initialize touch panel");
    static esp_lcd_touch_handle_t touch_handle = NULL;
    static esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = DRV_LCD_H_RES,
        .y_max = DRV_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = DRV_LCD_SWAP_XY,
            .mirror_x = !DRV_LCD_MIRROR_X,
            .mirror_y = DRV_LCD_MIRROR_Y,
        },
        .user_data = &io_exp_val,
    };
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CHSC6X_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(BSP_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle),
                        TAG, "TP IO initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_chsc6x(tp_io_handle, &tp_cfg, &touch_handle),
                        TAG, "TP initialization failed");
    const lvgl_port_touch_cfg_t touch = {
        .disp = disp,
        .handle = touch_handle,
    };
    return lvgl_port_add_touch(&touch);
}

lv_disp_t *bsp_lvgl_init(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = DRV_LCD_H_RES * LVGL_DRAW_BUFF_HEIGHT,
        .double_buffer = LVGL_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        }};
    return bsp_lvgl_init_with_cfg(&cfg);
}

lv_disp_t *bsp_lvgl_init_with_cfg(const bsp_display_cfg_t *cfg)
{
    if (lvgl_port_init(&cfg->lvgl_port_cfg) != ESP_OK)
        return NULL;
    if (bsp_lcd_backlight_init() != ESP_OK)
        return NULL;
    lv_disp_t *disp = bsp_display_lcd_init(cfg);
    if (disp != NULL)
    {
#if CONFIG_LVGL_INPUT_DEVICE_USE_KNOB
        bsp_knob_indev_init(disp);
#endif
#if CONFIG_LVGL_INPUT_DEVICE_USE_TP
        bsp_touch_indev_init(disp);
#endif
    }
    return disp;
}

esp_io_expander_handle_t bsp_io_expander_init()
{
    if (io_exp_handle != NULL) {
        return io_exp_handle;
    }
    
    ESP_LOGI(TAG, "Initialize IO I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_GENERAL_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BSP_GENERAL_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BSP_GENERAL_I2C_CLK};
    if ((i2c_param_config(BSP_GENERAL_I2C_NUM, &i2c_conf) != ESP_OK) ||
        (i2c_driver_install(BSP_GENERAL_I2C_NUM, i2c_conf.mode, 0, 0, 0) != ESP_OK))
    {
        ESP_LOGE(TAG, "I2C initialization failed");
        return NULL;
    }

    esp_io_expander_new_i2c_pca95xx_16bit(BSP_GENERAL_I2C_NUM, 
                                          ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_001, 
                                          &io_exp_handle);

    esp_io_expander_set_dir(io_exp_handle, DRV_IO_EXP_INPUT_MASK, IO_EXPANDER_INPUT);
    esp_io_expander_set_dir(io_exp_handle, DRV_IO_EXP_OUTPUT_MASK, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_exp_handle, DRV_IO_EXP_OUTPUT_MASK, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    esp_io_expander_set_level(io_exp_handle, DRV_IO_EXP_OUTPUT_MASK, 1);

    esp_io_expander_print_state(io_exp_handle);

    uint32_t pin_val = 0;
    esp_io_expander_get_level(io_exp_handle, DRV_IO_EXP_INPUT_MASK, &pin_val);
    io_exp_val = DRV_IO_EXP_OUTPUT_MASK | (uint16_t)pin_val;
    ESP_LOGI(TAG, "IO expander initialized: %x", io_exp_val);

    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_IO_EXPANDER_INT),
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    gpio_set_intr_type(BSP_IO_EXPANDER_INT, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BSP_IO_EXPANDER_INT, io_exp_isr_handler, NULL);

    return &io_exp_handle;
}