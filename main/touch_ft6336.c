#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "touch_ft6336.h"

static const char *TAG = "touch_ft6336";

/* Simple context so callbacks can access the handle */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t tp;
} touch_ctx_t;

static touch_ctx_t s_ctx = {0};

/* ---------- LVGL read callback (v9 signature) ---------- */
static void touch_lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!s_ctx.tp) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_read_data(s_ctx.tp);

    uint16_t x = 0, y = 0;
    uint8_t points = 0;
    bool touched = esp_lcd_touch_get_coordinates(s_ctx.tp, &x, &y, NULL, &points, 1);

    if (touched && points > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* Register LVGL pointer indev for this touch */
lv_indev_t *touch_lvgl_register(esp_lcd_touch_handle_t tp)
{
    s_ctx.tp = tp;  // cache for callbacks

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_lvgl_read_cb);
    return indev;
}

/* ---------- Debug logger task (polls controller directly) ---------- */
static void touch_dbg_task(void *arg)
{
    (void)arg;
    uint16_t last_x = 0, last_y = 0;
    bool last_pressed = false;

    for (;;) {
        if (s_ctx.tp) {
            esp_lcd_touch_read_data(s_ctx.tp);
            uint16_t x = 0, y = 0;
            uint8_t n = 0;
            bool pressed = esp_lcd_touch_get_coordinates(s_ctx.tp, &x, &y, NULL, &n, 1);

            if (pressed && n > 0) {
                if (!last_pressed || x != last_x || y != last_y) {
                    ESP_LOGI("touch_dbg", "touch: (%u,%u)", (unsigned)x, (unsigned)y);
                }
                last_x = x; last_y = y;
            } else {
                if (last_pressed) ESP_LOGI("touch_dbg", "touch: released");
            }
            last_pressed = pressed && n > 0;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void touch_debug_start(esp_lcd_touch_handle_t tp, const char *tag)
{
    (void)tag;
    if (tp) s_ctx.tp = tp;
    xTaskCreatePinnedToCore(touch_dbg_task, "touch_dbg", 3*1024, NULL, 4, NULL, 0);
}

/* Compatibility wrapper to match your existing call-site */
void touch_dbg_start(lv_indev_t *indev)
{
    (void)indev;
    xTaskCreatePinnedToCore(touch_dbg_task, "touch_dbg", 3*1024, NULL, 4, NULL, 0);
}

/* ---------- Initialization (I2C bus + FT6336 via FT5x06 driver) ---------- */
esp_err_t touch_ft6336_init(const touch_ft6336_cfg_t *cfg, esp_lcd_touch_handle_t *out_tp)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");

    // INT pin: input with INTERNAL pull-up (you have external pull-ups on SDA/SCL only)
    if (cfg->int_io >= 0) {
        gpio_config_t gi = {
            .pin_bit_mask = 1ULL << cfg->int_io,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 1,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gi), TAG, "INT pin cfg failed");
    }

    // Optional reset pin
    if (cfg->rst_io >= 0) {
        gpio_config_t gr = {
            .pin_bit_mask = 1ULL << cfg->rst_io,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gr), TAG, "RST pin cfg failed");
        // active-low reset pulse
        gpio_set_level(cfg->rst_io, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->rst_io, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // I2C master bus (EXTERNAL pull-ups on SDA/SCL -> disable internal)
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .sda_io_num = cfg->i2c_sda_io,
        .scl_io_num = cfg->i2c_scl_io,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = 0},
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_ctx.i2c_bus), TAG, "i2c bus create failed");

    // Panel IO wrapper for the FT5x06/FT6336 touch over I2C
#ifdef ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
#else
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = 0x38,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .xfer_queue_depth = 0,
    };
#endif
    io_cfg.dev_addr = 0x38;  // FT6336/FT5x06 default
    io_cfg.scl_speed_hz = cfg->i2c_clk_hz ? cfg->i2c_clk_hz : 100000; // 100k first; 400k once stable

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_ctx.i2c_bus, &io_cfg, &s_ctx.io),
                        TAG, "panel io i2c failed");

    // Touch config
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = cfg->x_max,
        .y_max = cfg->y_max,
        .rst_gpio_num = cfg->rst_io,
        .int_gpio_num = cfg->int_io,
        .levels = {
            .reset = 0,     // active low reset
            .interrupt = 0, // INT goes low on touch for FT6336
        },
        .flags = {
            .swap_xy = cfg->swap_xy,
            .mirror_x = cfg->mirror_x,
            .mirror_y = cfg->mirror_y,
        },
    };

    esp_err_t err = esp_lcd_touch_new_i2c_ft5x06(s_ctx.io, &tp_cfg, &s_ctx.tp);
    ESP_RETURN_ON_ERROR(err, TAG, "FT5x06/FT6336 init failed");

    ESP_LOGI(TAG, "FT5x06/FT6336 touch ready at 0x%02X (%ux%u) swap_xy=%d mx=%d my=%d",
             io_cfg.dev_addr, (unsigned)tp_cfg.x_max, (unsigned)tp_cfg.y_max,
             (int)tp_cfg.flags.swap_xy, (int)tp_cfg.flags.mirror_x, (int)tp_cfg.flags.mirror_y);

    if (out_tp) *out_tp = s_ctx.tp;
    return ESP_OK;
}
