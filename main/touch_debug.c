#include "touch_debug.h"
#include "esp_log.h"

static const char *TAG = "touch_dbg";

static lv_indev_t *s_indev;
static lv_obj_t   *s_dot;
static lv_obj_t   *s_lbl;
static bool        s_log_uart;
static bool        s_show_label;
static lv_point_t  s_last_pt = { .x = -1, .y = -1 };
static lv_indev_state_t s_last_state = LV_INDEV_STATE_RELEASED;

static void touch_tmr_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if(!s_indev) return;

    lv_point_t p;
    lv_indev_get_point(s_indev, &p);                   // v9: returns void
    lv_indev_state_t st = lv_indev_get_state(s_indev); // get pressed/released

    bool pressed = (st == LV_INDEV_STATE_PRESSED);

    if(pressed) {
        lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        // center the dot on the point
        lv_coord_t dx = lv_obj_get_width(s_dot)  / 2;
        lv_coord_t dy = lv_obj_get_height(s_dot) / 2;
        lv_obj_set_pos(s_dot, p.x - dx, p.y - dy);

        if(s_show_label && s_lbl) {
            static char buf[32];
            lv_snprintf(buf, sizeof(buf), "(%d,%d)", (int)p.x, (int)p.y);
            lv_label_set_text(s_lbl, buf);
        }
    } else {
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    }

    // Log only on change to avoid spam
    if(s_log_uart && (pressed != (s_last_state == LV_INDEV_STATE_PRESSED) ||
                      p.x != s_last_pt.x || p.y != s_last_pt.y)) {
        if(pressed) ESP_LOGD(TAG, "touch: %d,%d", (int)p.x, (int)p.y);
        else        ESP_LOGD(TAG, "touch: released");
    }

    s_last_pt = p;
    s_last_state = st;
}

void touch_debug_overlay_create(lv_indev_t *indev, bool log_uart, bool show_label)
{
    s_indev = indev;
    s_log_uart = log_uart;
    s_show_label = show_label;

    lv_obj_t *layer = lv_layer_top();

    // Red dot cursor
    s_dot = lv_obj_create(layer);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, 12, 12);
    lv_obj_set_style_bg_color(s_dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_outline_width(s_dot, 1, 0);
    lv_obj_set_style_outline_color(s_dot, lv_color_white(), 0);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN); // hidden until pressed

    // Optional coordinate label
    if(s_show_label) {
        s_lbl = lv_label_create(layer);
        lv_label_set_text(s_lbl, "(---,---)");
        lv_obj_set_style_text_color(s_lbl, lv_palette_main(LV_PALETTE_YELLOW), 0);
        lv_obj_set_style_bg_opa(s_lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(s_lbl, 2, 2);
    }

    // Poll at ~30 Hz
    lv_timer_create(touch_tmr_cb, 33, NULL);
}
