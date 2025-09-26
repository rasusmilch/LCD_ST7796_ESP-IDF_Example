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

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t tp;
} touch_ctx_t;

static touch_ctx_t s_ctx = {0};

esp_err_t touch_ft6336_init(const touch_ft6336_cfg_t *cfg, esp_lcd_touch_handle_t *out_tp)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");

    // INT pin: input with INTERNAL pull-up (only INT needs it)
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

    // Optional reset pin (active low on most FT5x06/FT6336)
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

    // I2C master bus (EXTERNAL pull-ups present -> disable internal)
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .sda_io_num = cfg->i2c_sda_io,
        .scl_io_num = cfg->i2c_scl_io,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 0,   // external pull-ups on your board
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_ctx.i2c_bus), TAG, "i2c bus create failed");

    // I2C panel IO wrapper (macro with sane defaults)
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.dev_addr = 0x38;  // FT6336/FT5x06 default address
    io_cfg.scl_speed_hz = cfg->i2c_clk_hz ? cfg->i2c_clk_hz : 100000;  // 100 kHz default

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_ctx.i2c_bus, &io_cfg, &s_ctx.io),
                        TAG, "panel io i2c failed");

    // Touch config
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = cfg->x_max,
        .y_max = cfg->y_max,
        .rst_gpio_num = cfg->rst_io,
        .int_gpio_num = cfg->int_io,
        .levels = {
            .reset = 0,       // active low reset if used
            .interrupt = 0,   // INT low when touched on FT6336
        },
        .flags = {
            .swap_xy = cfg->swap_xy,
            .mirror_x = cfg->mirror_x,
            .mirror_y = cfg->mirror_y,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(s_ctx.io, &tp_cfg, &s_ctx.tp),
                        TAG, "touch new failed");

    ESP_LOGI(TAG, "FT5x06/FT6336 touch ready at 0x%02X (%dx%d) swap_xy=%d mx=%d my=%d",
             io_cfg.dev_addr, tp_cfg.x_max, tp_cfg.y_max,
             (int)tp_cfg.flags.swap_xy, (int)tp_cfg.flags.mirror_x, (int)tp_cfg.flags.mirror_y);

    if (out_tp) *out_tp = s_ctx.tp;
    return ESP_OK;
}

static void touch_dbg_task(void *arg)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)arg;
    uint16_t last_x = 0, last_y = 0;
    bool last_pressed = false;

    for (;;) {
        esp_lcd_touch_read_data(tp);
        uint16_t x, y;
        uint8_t n = 0;
        bool pressed = esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &n, 1);
        if (pressed && n > 0) {
            if (!last_pressed || x != last_x || y != last_y) {
                ESP_LOGI("touch_dbg", "touch: (%u,%u)", (unsigned)x, (unsigned)y);
            }
            last_x = x; last_y = y;
        } else {
            if (last_pressed) ESP_LOGI("touch_dbg", "touch: released");
        }
        last_pressed = pressed && n > 0;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void touch_debug_start(esp_lcd_touch_handle_t tp, const char *tag)
{
    (void)tag;
    xTaskCreatePinnedToCore(touch_dbg_task, "touch_dbg", 3*1024, tp, 4, NULL, 0);
}
