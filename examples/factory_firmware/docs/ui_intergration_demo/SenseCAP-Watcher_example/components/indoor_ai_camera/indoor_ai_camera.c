
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "indoor_ai_camera.h"

static const char *TAG = "BSP";

static led_strip_handle_t rgb_led_handle = NULL;
static esp_io_expander_handle_t io_exp_handle = NULL;

static sdmmc_card_t *card;
static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;

static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static const audio_codec_data_if_t *i2s_data_if = NULL;

static uint16_t io_exp_val = 0;
static volatile bool io_exp_update = false;
static void io_exp_isr_handler(void *arg) { io_exp_update = true; }

uint8_t bsp_exp_io_get_level(uint16_t pin_mask)
{
    if (io_exp_update && (io_exp_handle != NULL))
    {
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
    if (pin_mask ^ (io_exp_val & DRV_IO_EXP_OUTPUT_MASK))
    { // Output pins changed
        ret = esp_io_expander_set_level(io_exp_handle, pin_mask, level);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set output level");
            return ret;
        }
        io_exp_val = (io_exp_val & (~pin_mask)) | (level ? pin_mask : 0);
    }
    return ret;
}

esp_err_t bsp_spi_bus_init(void)
{
    static bool initialized = false;
    if (initialized)
    {
        return ESP_OK;
    }
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_SPI3_HOST_MOSI,
        .miso_io_num = BSP_SPI3_HOST_MISO,
        .sclk_io_num = BSP_SPI3_HOST_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    initialized = true;
    return ESP_OK;
}

esp_err_t bsp_i2c_bus_init(void)
{
    static bool initialized = false;
    if (initialized)
    {
        return ESP_OK;
    }
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_GENERAL_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = BSP_GENERAL_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = BSP_GENERAL_I2C_CLK};
    BSP_ERROR_CHECK_RETURN_ERR(i2c_param_config(BSP_GENERAL_I2C_NUM, &i2c_conf));
    BSP_ERROR_CHECK_RETURN_ERR(i2c_driver_install(BSP_GENERAL_I2C_NUM, i2c_conf.mode, 0, 0, ESP_INTR_FLAG_SHARED));
    initialized = true;
    return ESP_OK;
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

    ret |= led_strip_set_pixel(rgb_led_handle, 0, r, g, b);
    ret |= led_strip_refresh(rgb_led_handle);
    return ret;
}

uint16_t bsp_bat_get_voltage(void)
{
    static bool initialized = false;
    static adc_oneshot_unit_handle_t adc_handle;
    static adc_cali_handle_t cali_handle = NULL;
    if (!initialized)
    {
        adc_oneshot_unit_init_cfg_t init_config = { 
            .unit_id = ADC_UNIT_1,
        };
        adc_oneshot_new_unit(&init_config, &adc_handle);

        adc_oneshot_chan_cfg_t ch_config = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = BSP_BAT_ADC_ATTEN,
        };
        adc_oneshot_config_channel(adc_handle, BSP_BAT_ADC_CHAN, &ch_config);

        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = BSP_BAT_ADC_CHAN,
            .atten = BSP_BAT_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK) {
            initialized = true;
        }
    }
    if (initialized)
    {
        int raw_value = 0;
        int voltage = 0; // mV
        adc_oneshot_read(adc_handle, BSP_BAT_ADC_CHAN, &raw_value);
        adc_cali_raw_to_voltage(cali_handle, raw_value, &voltage);
        voltage = voltage * 82 / 20;
        ESP_LOGI(TAG, "voltage: %dmV", voltage);
        return (uint16_t)voltage;
    }
    return 0;
}

uint8_t bsp_bat_get_percentage(void)
{
    uint32_t voltage = 0;
    for (uint8_t i = 0; i < 10; i++) {
        voltage += bsp_bat_get_voltage();
    }
    voltage /= 10;
    int percent = (voltage - 3400) * 100 / (4200 - 3400);
    percent = (percent > 100) ? 100 : (percent < 0) ? 0 : percent;
    ESP_LOGI(TAG, "percentage: %d%%", percent);
    return (uint8_t)percent;
}

esp_err_t bsp_rtc_init(void)
{
    esp_err_t ret = ESP_OK;
    ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed");
        return ret;
    }

    uint8_t data[2] = {0x00, 0x00};
    ret = i2c_master_write_to_device(
        BSP_GENERAL_I2C_NUM, 
        DRV_PCF8563_I2C_ADDR, 
        data, 
        sizeof(data), 
        DRV_PCF8563_TIMEOUT_MS / portTICK_PERIOD_MS
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RTC");
        return ret;
    }

    return ESP_OK;
}

esp_err_t bsp_rtc_get_time(struct tm *timeinfo)
{
    esp_err_t ret = ESP_OK;
    ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed");
        return ret;
    }

    uint8_t data[8] = {0};
    data[0] = DRV_RTC_REG_TIME;
    ret = i2c_master_write_read_device(
        BSP_GENERAL_I2C_NUM, 
        DRV_PCF8563_I2C_ADDR, 
        data, 1, 
        data + 1, sizeof(data) - 1, 
        DRV_PCF8563_TIMEOUT_MS / portTICK_PERIOD_MS
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time from RTC");
        return ret;
    }

    struct tm tm_data = {
       .tm_sec =  BCD2DEC(data[1] & 0x7F),
       .tm_min =  BCD2DEC(data[2] & 0x7F),
       .tm_hour = BCD2DEC(data[3] & 0x3F),
       .tm_mday = BCD2DEC(data[4] & 0x3F),
       .tm_wday = BCD2DEC(data[5] & 0x07),
       .tm_mon =  BCD2DEC(data[6] & 0x1F) - 1,
       .tm_year = BCD2DEC(data[7]) + 2000 - 1900,
    };
    *timeinfo = tm_data;

    ESP_LOGI(TAG, "Current time: %d-%d-%d %d:%d:%d", 
            timeinfo->tm_year + 1900, 
            timeinfo->tm_mon + 1, 
            timeinfo->tm_mday, 
            timeinfo->tm_hour, 
            timeinfo->tm_min, 
            timeinfo->tm_sec);

    return ESP_OK;
}

esp_err_t bsp_rtc_set_time(const struct tm *timeinfo)
{
    esp_err_t ret = ESP_OK;

    uint8_t data[8] = {0};
    data[0] = DRV_RTC_REG_TIME;
    data[1] = DEC2BCD(timeinfo->tm_sec);
    data[2] = DEC2BCD(timeinfo->tm_min);
    data[3] = DEC2BCD(timeinfo->tm_hour);
    data[4] = DEC2BCD(timeinfo->tm_mday);
    data[5] = DEC2BCD(timeinfo->tm_wday); // 0 - 6
    data[6] = DEC2BCD(timeinfo->tm_mon + 1); // 0 - 11
    data[7] = DEC2BCD(timeinfo->tm_year - 100);

    ret = i2c_master_write_to_device(
        BSP_GENERAL_I2C_NUM, 
        DRV_PCF8563_I2C_ADDR, 
        data, 
        sizeof(data), 
        DRV_PCF8563_TIMEOUT_MS / portTICK_PERIOD_MS
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set time to RTC");
        return ret;
    }

    return ESP_OK;
}

void bsp_system_pwr_off(void) {
    bsp_exp_io_set_level(BSP_PWR_SYSTEM, 0);
}

esp_err_t bsp_knob_btn_init(void *param)
{
    // esp_io_expander_handle_t io_exp = *((esp_io_expander_handle_t *)param);

    esp_io_expander_handle_t io_exp = bsp_io_expander_init();
    if (io_exp == NULL)
    {
        ESP_LOGE(TAG, "IO expander initialization failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

uint8_t bsp_knob_btn_get_key_value(void *param)
{
    // esp_io_expander_handle_t io_exp = *((esp_io_expander_handle_t *)param);
    return bsp_exp_io_get_level(BSP_KNOB_BTN);
}

esp_err_t bsp_knob_btn_deinit(void *param)
{
    // esp_io_expander_handle_t io_exp_handle = *((esp_io_expander_handle_t *)param);
    return esp_io_expander_del(io_exp_handle);
}

static esp_err_t bsp_lcd_backlight_init()
{
    const ledc_channel_config_t backlight_channel = {
        .gpio_num = BSP_LCD_GPIO_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = DRV_LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_1,
        .duty = BIT(DRV_LCD_LEDC_DUTY_RES),
        .hpoint = 0
    };
    const ledc_timer_config_t backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = DRV_LCD_LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel));

    ESP_ERROR_CHECK(bsp_lcd_brightness_set(80));

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
        .trans_queue_depth = 1,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, ret_io), err,
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
    spi_bus_free(BSP_LCD_SPI_NUM);
    return ret;
}

static lv_disp_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);

    ESP_LOGD(TAG, "Initialize SPI bus");
    if (bsp_spi_bus_init() != ESP_OK)
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
        .single_edge_trigger = 1,
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
        (i2c_driver_install(BSP_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, ESP_INTR_FLAG_SHARED) != ESP_OK))
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
    BSP_ERROR_CHECK_RETURN_NULL(esp_lcd_new_panel_io_i2c(BSP_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle));
    BSP_ERROR_CHECK_RETURN_NULL(esp_lcd_touch_new_i2c_chsc6x(tp_io_handle, &tp_cfg, &touch_handle));
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
    cfg.lvgl_port_cfg.task_stack = 8192;
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
    if (io_exp_handle != NULL)
    {
        return io_exp_handle;
    }

    ESP_LOGI(TAG, "Initialize IO I2C bus");
    ESP_ERROR_CHECK(bsp_i2c_bus_init());

    esp_io_expander_new_i2c_pca95xx_16bit(BSP_GENERAL_I2C_NUM,
                                          ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_001,
                                          &io_exp_handle);

    esp_io_expander_set_dir(io_exp_handle, DRV_IO_EXP_INPUT_MASK, IO_EXPANDER_INPUT);
    esp_io_expander_set_dir(io_exp_handle, DRV_IO_EXP_OUTPUT_MASK, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_exp_handle, DRV_IO_EXP_OUTPUT_MASK, 0);
    esp_io_expander_set_level(io_exp_handle, BSP_PWR_SYSTEM, 1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    esp_io_expander_set_level(io_exp_handle, BSP_PWR_START_UP, 1);

    // esp_io_expander_print_state(io_exp_handle);

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
    gpio_install_isr_service(ESP_INTR_FLAG_SHARED);
    gpio_isr_handler_add(BSP_IO_EXPANDER_INT, io_exp_isr_handler, NULL);

    return io_exp_handle;
}

esp_err_t bsp_sdcard_init(char *mount_point, size_t max_files)
{
    ESP_ERROR_CHECK(bsp_spi_bus_init());
    bsp_io_expander_init();

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BSP_SD_SPI_NUM;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BSP_SD_SPI_CS;
    slot_config.host_id = host.slot;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = max_files,
        .allocation_unit_size = 16 * 1024};
    ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card));
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}

esp_err_t bsp_sdcard_init_default(void)
{
    return bsp_sdcard_init(DRV_BASE_PATH_SD, DRV_FS_MAX_FILES);
}

esp_err_t bsp_sdcard_deinit(char *mount_point)
{
    if (NULL == mount_point)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret_val = esp_vfs_fat_sdcard_unmount(mount_point, card);

    card = NULL;

    return ret_val;
}

esp_err_t bsp_sdcard_deinit_default(void)
{
    return bsp_sdcard_deinit(DRV_BASE_PATH_SD);
}

esp_err_t bsp_spiffs_init(char *mount_point, size_t max_files)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = "storage",
        .max_files = max_files,
        .format_if_mount_failed = false,
    };
    esp_vfs_spiffs_register(&conf);

    size_t total = 0, used = 0;
    esp_err_t ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ret_val;
}

esp_err_t bsp_spiffs_init_default(void)
{
    return bsp_spiffs_init(DRV_BASE_PATH_FLASH, DRV_FS_MAX_FILES);
}

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    esp_err_t ret = ESP_FAIL;
    if (i2s_tx_chan && i2s_rx_chan)
    {
        /* Audio was initialized before */
        return ESP_OK;
    }

    /* Setup I2S peripheral */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    chan_cfg.intr_priority = 4;
    BSP_ERROR_CHECK_RETURN_ERR(i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan));

    /* Setup I2S channels */
    i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(DRV_AUDIO_SAMPLE_RATE);
    i2s_std_config_t *p_i2s_cfg = &std_cfg_default;
    if (i2s_config != NULL) {
        memcpy(p_i2s_cfg, i2s_config, sizeof(i2s_std_config_t));
    }

    if (i2s_tx_chan != NULL)
    {
        ESP_GOTO_ON_ERROR(i2s_channel_init_std_mode(i2s_tx_chan, p_i2s_cfg), err, TAG, "I2S channel initialization failed");
        ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_tx_chan), err, TAG, "I2S enabling failed");
    }
    if (i2s_rx_chan != NULL)
    {
        p_i2s_cfg->slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
        ESP_GOTO_ON_ERROR(i2s_channel_init_std_mode(i2s_rx_chan, p_i2s_cfg), err, TAG, "I2S channel initialization failed");
        ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_rx_chan), err, TAG, "I2S enabling failed");
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BSP_AUDIO_I2S_NUM,
        .rx_handle = i2s_rx_chan,
        .tx_handle = i2s_tx_chan,
    };
    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    BSP_NULL_CHECK_GOTO(i2s_data_if, err);

    return ESP_OK;

err:
    if (i2s_tx_chan)
    {
        i2s_del_channel(i2s_tx_chan);
    }
    if (i2s_rx_chan)
    {
        i2s_del_channel(i2s_rx_chan);
    }

    return ret;
}

const audio_codec_data_if_t *bsp_audio_get_codec_itf(void)
{
    return i2s_data_if;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    const audio_codec_data_if_t *i2s_data_if = bsp_audio_get_codec_itf();
    if (i2s_data_if == NULL)
    {
        /* Initilize I2C */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_i2c_bus_init());
        /* Configure I2S peripheral and Power Amplifier */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_audio_init(NULL));
        i2s_data_if = bsp_audio_get_codec_itf();
    }
    assert(i2s_data_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_GENERAL_I2C_NUM,
        .addr = DRV_ES8311_I2C_ADDR,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    BSP_NULL_CHECK(i2c_ctrl_if, NULL);

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    BSP_NULL_CHECK(es8311_dev, NULL);

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = i2s_data_if,
    };
    return esp_codec_dev_new(&codec_dev_cfg);
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    const audio_codec_data_if_t *i2s_data_if = bsp_audio_get_codec_itf();
    if (i2s_data_if == NULL)
    {
        /* Initilize I2C */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_i2c_bus_init());
        /* Configure I2S peripheral and Power Amplifier */
        BSP_ERROR_CHECK_RETURN_NULL(bsp_audio_init(NULL));
        i2s_data_if = bsp_audio_get_codec_itf();
    }
    assert(i2s_data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_GENERAL_I2C_NUM,
        .addr = DRV_ES7243_I2C_ADDR,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    BSP_NULL_CHECK(i2c_ctrl_if, NULL);

    es7243_codec_cfg_t es7243_cfg = {
        .ctrl_if = i2c_ctrl_if,
    };
    const audio_codec_if_t *es7243_dev = es7243_codec_new(&es7243_cfg);
    BSP_NULL_CHECK(es7243_dev, NULL);

    esp_codec_dev_cfg_t codec_es7243_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es7243_dev,
        .data_if = i2s_data_if,
    };
    return esp_codec_dev_new(&codec_es7243_dev_cfg);
}

esp_err_t bsp_i2s_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_read(record_dev_handle, audio_buffer, len);
    *bytes_read = len;
#if CONFIG_BSP_AUDIO_MIC_VALUE_GAIN > 0
    uint16_t *buffer = (uint16_t *)audio_buffer;
    for (size_t i = 0; i < len / 2; i++) {
        buffer[i] = buffer[i] << CONFIG_BSP_AUDIO_MIC_VALUE_GAIN;
    }
#endif
    return ret;
}

esp_err_t bsp_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    *bytes_written = len;
    return ret;
}

esp_err_t bsp_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };

    if (play_dev_handle)
    {
        ret = esp_codec_dev_close(play_dev_handle);
    }
    if (record_dev_handle)
    {
        ret |= esp_codec_dev_close(record_dev_handle);
        ret |= esp_codec_dev_set_in_gain(record_dev_handle, DRV_AUDIO_MIC_GAIN);
    }

    if (play_dev_handle)
    {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }
    if (record_dev_handle)
    {
        fs.channel = 2;
        fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        ret |= esp_codec_dev_open(record_dev_handle, &fs);
    }
    return ret;
}

esp_err_t bsp_codec_volume_set(int volume, int *volume_set)
{
    esp_err_t ret = ESP_OK;
    float v = volume;
    ret = esp_codec_dev_set_out_vol(play_dev_handle, (int)v);
    return ret;
}

esp_err_t bsp_codec_mute_set(bool enable)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_set_out_mute(play_dev_handle, enable);
    return ret;
}

esp_err_t bsp_codec_dev_stop(void)
{
    esp_err_t ret = ESP_OK;

    if (play_dev_handle)
    {
        ret = esp_codec_dev_close(play_dev_handle);
    }

    if (record_dev_handle)
    {
        ret = esp_codec_dev_close(record_dev_handle);
    }
    return ret;
}

esp_err_t bsp_codec_dev_resume(void)
{
    return bsp_codec_set_fs(DRV_AUDIO_SAMPLE_RATE, DRV_AUDIO_SAMPLE_BITS, DRV_AUDIO_CHANNELS);
}

esp_err_t bsp_codec_init(void)
{
    play_dev_handle = bsp_audio_codec_speaker_init();
    assert((play_dev_handle) && "play_dev_handle not initialized");

    record_dev_handle = bsp_audio_codec_microphone_init();
    assert((record_dev_handle) && "record_dev_handle not initialized");

    bsp_codec_set_fs(DRV_AUDIO_SAMPLE_RATE, DRV_AUDIO_SAMPLE_BITS, DRV_AUDIO_CHANNELS);
    return ESP_OK;
}

esp_err_t bsp_get_feed_data(bool is_get_raw_channel, int16_t *buffer, int buffer_len)
{
    esp_err_t ret = ESP_OK;

    ret = esp_codec_dev_read(record_dev_handle, (void *)buffer, buffer_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data from codec device");
    }
    // int audio_chunksize = buffer_len / (sizeof(int16_t) * DRV_AUDIO_I2S_CHANNEL);
    // if (!is_get_raw_channel)
    // {
    //     for (int i = 0; i < audio_chunksize; i++)
    //     {
    //         buffer[i] = buffer[i] << 2;
    //     }
    // }
    return ret;
}

int bsp_get_feed_channel(void)
{
    return DRV_AUDIO_I2S_CHANNEL;
}
