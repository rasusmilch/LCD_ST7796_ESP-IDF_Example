#pragma once
#include "lvgl.h"
#include <stdint.h>
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif



// Your preferred callback signature
typedef void (*ui_btn_handler_t)(lv_event_t *e, void *user_data);

// Convenience: build + wire a handler with your signature.
// If toggle==true, event will be LV_EVENT_VALUE_CHANGED; otherwise LV_EVENT_CLICKED.
lv_obj_t *ui_button_on(lv_obj_t *parent, ui_theme_t *t,
                       const char *text, uint32_t variants, bool toggle,
                       ui_btn_handler_t fn, void *user_data);

#ifdef __cplusplus
}
#endif
