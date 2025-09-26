#pragma once
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create a tiny red cursor overlay + optional "(x,y)" label.
 *  - indev: your LVGL pointer indev (from touch_ft6336_init)
 *  - log_uart: print coords to UART only when they change
 *  - show_label: draw "(x,y)" in the top-left
 */
void touch_debug_overlay_create(lv_indev_t *indev, bool log_uart, bool show_label);

#ifdef __cplusplus
}
#endif
