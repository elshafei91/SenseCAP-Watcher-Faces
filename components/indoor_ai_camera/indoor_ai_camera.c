
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "indoor_ai_camera.h"

static const char *TAG = "BSP";

static led_strip_handle_t rgb_led_handle = NULL;
static esp_io_expander_handle_t io_expander_handle = NULL;

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
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(*ret_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(*ret_panel, true, true));
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
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = true,
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
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 200,
        .gpio_button_config = {
            .gpio_num = BSP_KNOB_BTN,
            .active_level = 0,
        },
    };
    const lvgl_port_encoder_cfg_t encoder = {
        .disp = disp,
        .encoder_a_b = &knob_cfg,
        .encoder_enter = &btn_config};
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
        .int_gpio_num = BSP_TOUCH_GPIO_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = true,
        },
    };
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CHSC6X_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(DRV_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle),
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
#if CONFIG_LVGL_INPUT_DEVICE_USE_TOUCH
        bsp_touch_indev_init(disp);
#endif
    }
    return disp;
}

esp_io_expander_handle_t bsp_io_expander_init()
{
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

    for (int address = 0x03; address <= 0x77; address++)
    { // 扫描的I2C地址从0x03到0x77
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1 | I2C_MASTER_WRITE), true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(BSP_GENERAL_I2C_NUM, cmd, 100);
        if (ret == ESP_OK)
        { // 发送给当前地址成功，表示设备的地址是当前的地址
            printf("Device found at address 0x%02X\n", address);
        }
        i2c_cmd_link_delete(cmd);
        vTaskDelay(10);
    }

    esp_io_expander_new_i2c_pca95xx_16bit(BSP_GENERAL_I2C_NUM, ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_001, &io_expander_handle);

    esp_io_expander_set_dir(io_expander_handle, IO_EXPANDER_PIN_NUM_7 | 0xFFFFFF00, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander_handle, IO_EXPANDER_PIN_NUM_7 | 0xFFFFFF00, 1);

    esp_io_expander_print_state(io_expander_handle);

    for (int address = 0x03; address <= 0x77; address++)
    { // 扫描的I2C地址从0x03到0x77
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1 | I2C_MASTER_WRITE), true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(BSP_GENERAL_I2C_NUM, cmd, 100);
        if (ret == ESP_OK)
        { // 发送给当前地址成功，表示设备的地址是当前的地址
            printf("Device found at address 0x%02X\n", address);
        }
        i2c_cmd_link_delete(cmd);
        vTaskDelay(10);
    }

    return &io_expander_handle;
}