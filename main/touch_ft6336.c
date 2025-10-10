/* ========= touch_ft6336.c : SHARED I2C + LVGL glue ========= */

#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "lvgl.h"
#include "touch_ft6336.h"

static const char *TAG = "touch_ft6336";

/* ========= LVGL glue (v9) ========= */

typedef struct {
    esp_lcd_touch_handle_t tp;
    uint16_t x_max, y_max;
} touch_lvg_ctx_t;

static touch_lvg_ctx_t s_tctx = {0};

/* -------- init touch on an EXISTING I2C bus -------- */
esp_err_t touch_ft6336_init_on_bus(i2c_master_bus_handle_t bus,
                                   const touch_ft6336_cfg_t *cfg,
                                   esp_lcd_touch_handle_t *out_tp)
{
    ESP_RETURN_ON_FALSE(bus && cfg && out_tp, ESP_ERR_INVALID_ARG, TAG, "bad args");

    /* INT needs INTERNAL pull-up (you said SDA/SCL have externals) */
    if (cfg->int_io >= 0) {
        gpio_config_t gi = {
            .pin_bit_mask = 1ULL << cfg->int_io,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = 1,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gi), TAG, "INT cfg failed");
    }

    /* Optional RST pulse (active low for FT6336) */
    if (cfg->rst_io >= 0) {
        gpio_config_t gr = {
            .pin_bit_mask = 1ULL << cfg->rst_io,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_RETURN_ON_ERROR(gpio_config(&gr), TAG, "RST cfg failed");
        gpio_set_level(cfg->rst_io, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->rst_io, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* I2C panel-IO wrapper (v2 API) */
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = 0x38,                 /* FT6336 default */
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .scl_speed_hz = cfg->i2c_clk_hz ? cfg->i2c_clk_hz : 100000,
        .flags = { .disable_control_phase = 0 },
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "panel io i2c");

    /* Touch driver config */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = cfg->x_max,
        .y_max = cfg->y_max,
        .rst_gpio_num = cfg->rst_io,
        .int_gpio_num = cfg->int_io,
        .levels = {
            .reset = 0,       /* active low reset */
            .interrupt = 0,   /* INT low when touched */
        },
        .flags = {
            .swap_xy = cfg->swap_xy,
            .mirror_x = cfg->mirror_x,
            .mirror_y = cfg->mirror_y,
        },
    };

    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &tp_cfg, &tp), TAG, "ft5x06 init");

    /* remember limits for clamping */
    s_tctx.tp = tp;
    s_tctx.x_max = cfg->x_max;
    s_tctx.y_max = cfg->y_max;

    *out_tp = tp;

    ESP_LOGI(TAG, "FT6336 ready (0x%02X) %ux%u, swap_xy=%d mx=%d my=%d",
             io_cfg.dev_addr, tp_cfg.x_max, tp_cfg.y_max,
             (int)tp_cfg.flags.swap_xy, (int)tp_cfg.flags.mirror_x, (int)tp_cfg.flags.mirror_y);

    return ESP_OK;
}

/* -------- Minimal LVGL indev registration (no wrappers needed) -------- */

/* LVGL v9 read callback */
static void lv_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!s_tctx.tp) { data->state = LV_INDEV_STATE_RELEASED; return; }

    esp_lcd_touch_read_data(s_tctx.tp);

    uint16_t x = 0, y = 0; uint8_t n = 0;
    bool pressed = esp_lcd_touch_get_coordinates(s_tctx.tp, &x, &y, NULL, &n, 1);

    if (pressed && n) {
        if (x >= s_tctx.x_max) x = s_tctx.x_max - 1;
        if (y >= s_tctx.y_max) y = s_tctx.y_max - 1;
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* Register the touch as an LVGL input device (v9 way) */
lv_indev_t *touch_lvgl_register(esp_lcd_touch_handle_t tp)
{
    s_tctx.tp = tp;

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lv_touch_read_cb);
    return indev;
}

/* -------- Optional: console debug task (does NOT touch LVGL) -------- */
static void touch_dbg_task(void *arg)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)arg;
    uint16_t last_x = 0, last_y = 0; bool last_pressed = false;

    while (1) {
        esp_lcd_touch_read_data(tp);
        uint16_t x = 0, y = 0; uint8_t n = 0;
        bool pressed = esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &n, 1);

        if (pressed && n) {
            if (!last_pressed || x != last_x || y != last_y) {
                ESP_LOGI("touch_dbg", "touch: (%u,%u)", (unsigned)x, (unsigned)y);
            }
            last_x = x; last_y = y;
        } else if (last_pressed) {
            ESP_LOGI("touch_dbg", "touch: released");
        }
        last_pressed = pressed && n;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void touch_debug_start(esp_lcd_touch_handle_t tp, const char *tag)
{
    (void)tag;
    xTaskCreatePinnedToCore(touch_dbg_task, "touch_dbg", 3 * 1024, tp, 4, NULL, 0);
}
