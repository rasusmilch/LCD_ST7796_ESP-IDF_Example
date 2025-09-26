#pragma once
#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_theme_t {
    // Base/overlays
    lv_style_t btn_base;
    lv_style_t btn_pressed;

    // Variants (OFF state = default)
    lv_style_t btn_primary;
    lv_style_t btn_danger;
    lv_style_t btn_outline;
    lv_style_t btn_round;       // radius helper
    lv_style_t label_on_btn;

    // Variants (ON state = when LV_STATE_CHECKED is set)
    lv_style_t btn_primary_on;
    lv_style_t btn_danger_on;
    lv_style_t btn_outline_on;

    // Press effect
    lv_color_filter_dsc_t press_filter;
} ui_theme_t;


// Variants (bitmask)
enum {
    UI_BTN_VARIANT_PRIMARY = 1 << 0,
    UI_BTN_VARIANT_DANGER  = 1 << 1,
    UI_BTN_VARIANT_OUTLINE = 1 << 2,
    UI_BTN_VARIANT_ROUND   = 1 << 3,
    UI_BTN_VARIANT_SMALL   = 1 << 4,  // slightly smaller paddings
};

// Init once after lv_init()
void ui_theme_init(ui_theme_t *t);

// Create a styled button (no action wiring)
lv_obj_t *ui_button_create(lv_obj_t *parent, ui_theme_t *t,
                           const char *text, uint32_t variants, bool toggle);

#ifdef __cplusplus
}
#endif
