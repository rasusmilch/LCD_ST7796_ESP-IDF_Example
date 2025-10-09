#pragma once
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"   // <-- add this so lv_indev_t is visible

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int i2c_sda_io;
    int i2c_scl_io;
    int int_io;
    int rst_io;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    uint32_t i2c_clk_hz;
} touch_ft6336_cfg_t;

esp_err_t touch_ft6336_init_on_bus(i2c_master_bus_handle_t bus,
                                   const touch_ft6336_cfg_t *cfg,
                                   esp_lcd_touch_handle_t *out_tp);

/* NEW: register the touch with LVGL, returns lv_indev_t* */
lv_indev_t *touch_lvgl_register(esp_lcd_touch_handle_t tp);

/* Keep your old helper (polls the controller directly and logs coords) */
void touch_debug_start(esp_lcd_touch_handle_t tp, const char *tag);

/* NEW: compatibility wrapper so existing main.c builds:
   accepts an lv_indev_t*, but just uses the internal TP handle */
void touch_dbg_start(lv_indev_t *indev);

#ifdef __cplusplus
}
#endif
