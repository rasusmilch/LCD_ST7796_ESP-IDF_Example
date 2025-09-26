#pragma once
#include "esp_err.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int i2c_sda_io;       // I2C SDA GPIO (external pull-up present)
    int i2c_scl_io;       // I2C SCL GPIO (external pull-up present)
    int int_io;           // Touch interrupt GPIO (needs INTERNAL pull-up)
    int rst_io;           // Optional: reset GPIO, -1 if not used
    uint16_t x_max;       // Panel native width
    uint16_t y_max;       // Panel native height
    bool swap_xy;         // Orientation
    bool mirror_x;
    bool mirror_y;
    uint32_t i2c_clk_hz;  // I2C frequency (e.g., 100000 or 400000)
} touch_ft6336_cfg_t;

esp_err_t touch_ft6336_init(const touch_ft6336_cfg_t *cfg, esp_lcd_touch_handle_t *out_tp);
void touch_debug_start(esp_lcd_touch_handle_t tp, const char *tag);

#ifdef __cplusplus
}
#endif
