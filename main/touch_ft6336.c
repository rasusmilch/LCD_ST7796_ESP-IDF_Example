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

// add near top of touch_ft6336.c
typedef struct {
    uint16_t x_max, y_max;
} touch_bounds_t;

static touch_bounds_t s_bounds = {320, 480};   // default; set real values in init

/* Simple context so callbacks can access the handle */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t tp;
} touch_ctx_t;

static touch_ctx_t s_ctx = {0};

// LVGL read callback
static void touch_lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static uint16_t x, y;
    uint8_t n = 0;

    esp_lcd_touch_read_data(s_ctx.tp);
    bool pressed = esp_lcd_touch_get_coordinates(s_ctx.tp, &x, &y, NULL, &n, 1);

    if (!pressed || n == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // guard obviously bad readings
    if (x >= s_bounds.x_max || y >= s_bounds.y_max) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
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

static void touch_dbg_task(void *arg)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)arg;   // <- use the handle we were given
    uint16_t last_x = 0, last_y = 0;
    bool last_pressed = false;

    for (;;) {
        if (tp) {
            esp_lcd_touch_read_data(tp);
            uint16_t x = 0, y = 0; uint8_t n = 0;
            bool pressed = esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &n, 1);
            if (pressed && n > 0) {
                if (!last_pressed || x != last_x || y != last_y) {
                    ESP_LOGI("touch_dbg", "touch: (%u,%u)", (unsigned)x, (unsigned)y);
                }
                last_x = x; last_y = y;
            } else if (last_pressed) {
                ESP_LOGI("touch_dbg", "touch: released");
            }
            last_pressed = pressed && n > 0;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/* Compatibility wrapper to match your existing call-site */
void touch_debug_start(esp_lcd_touch_handle_t tp, const char *tag)
{
    (void)tag;
    // Intentionally no task: LVGL indev is the single reader now.
    xTaskCreatePinnedToCore(touch_dbg_task, "touch_dbg", 3*1024, tp, 4, NULL, 0);
}

/* ---------- Initialization (I2C bus + FT6336 via FT5x06 driver) ---------- */
esp_err_t touch_ft6336_init_on_bus(i2c_master_bus_handle_t bus,
                                   const touch_ft6336_cfg_t *cfg,
                                   esp_lcd_touch_handle_t *out_tp)
{
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "bus is NULL");
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");

    // INT needs internal pull-up (you have external pull-ups only on I2C)
    if (cfg->int_io >= 0) {
        gpio_config_t gi = {
            .pin_bit_mask = 1ULL << cfg->int_io,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 1,  // internal pull-up
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gi), TAG, "INT pin cfg failed");
    }

    if (cfg->rst_io >= 0) {
        gpio_config_t gr = {
            .pin_bit_mask = 1ULL << cfg->rst_io,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gr), TAG, "RST pin cfg failed");
        gpio_set_level(cfg->rst_io, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->rst_io, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // I2C panel IO for FT6336/FT5x06 on the shared bus
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = 0x38,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .scl_speed_hz = cfg->i2c_clk_hz ? cfg->i2c_clk_hz : 400000,
        .flags = { .disable_control_phase = 0 },
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "panel io i2c failed");

    // Touch config
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = cfg->x_max,
        .y_max = cfg->y_max,
        .rst_gpio_num = cfg->rst_io,
        .int_gpio_num = cfg->int_io,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = cfg->swap_xy, .mirror_x = cfg->mirror_x, .mirror_y = cfg->mirror_y },
    };

    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &tp_cfg, &tp), TAG, "touch new failed");

    // Cache for callbacks / wrappers
    s_ctx.tp = tp;
    s_bounds.x_max = cfg->x_max;
    s_bounds.y_max = cfg->y_max;

    if (out_tp) *out_tp = tp;
    
    ESP_LOGI(TAG, "FT6336 ready (0x38) %dx%d, swap_xy=%d mx=%d my=%d",
             tp_cfg.x_max, tp_cfg.y_max,
             (int)tp_cfg.flags.swap_xy, (int)tp_cfg.flags.mirror_x, (int)tp_cfg.flags.mirror_y);
    return ESP_OK;
}

