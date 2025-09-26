#include "ui_actions.h"

typedef struct {
    ui_btn_handler_t fn;
    void *user_data;
} _ui_cb_wrap_t;

static void _btn_trampoline(lv_event_t *e)
{
    _ui_cb_wrap_t *w = (_ui_cb_wrap_t *)lv_event_get_user_data(e);
    if (w && w->fn) w->fn(e, w->user_data);
}

static void _btn_free_on_delete(lv_event_t *e)
{
    _ui_cb_wrap_t *w = (_ui_cb_wrap_t *)lv_event_get_user_data(e);
    if (w) lv_free(w);
}

lv_obj_t *ui_button_on(lv_obj_t *parent, ui_theme_t *t,
                       const char *text, uint32_t variants, bool toggle,
                       ui_btn_handler_t fn, void *user_data)
{
    lv_obj_t *btn = ui_button_create(parent, t, text, variants, toggle);

    // Wrap your function pointer + payload into one user_data block
    _ui_cb_wrap_t *w = (_ui_cb_wrap_t *)lv_malloc(sizeof(*w));
    w->fn = fn;
    w->user_data = user_data;

    // For toggles, styles flip via LV_STATE_CHECKED and we listen to VALUE_CHANGED.
    lv_event_code_t code = toggle ? LV_EVENT_VALUE_CHANGED : LV_EVENT_CLICKED;

    lv_obj_add_event_cb(btn, _btn_trampoline, code, w);
    // Ensure wrapper is freed when the button is deleted
    lv_obj_add_event_cb(btn, _btn_free_on_delete, LV_EVENT_DELETE, w);

    return btn;
}
