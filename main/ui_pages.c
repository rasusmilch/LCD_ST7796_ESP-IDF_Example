#include "ui_pages.h"
#include "esp_log.h"

typedef struct {
    const ui_widget_desc_t *desc;
    lv_obj_t *obj;   // created lvgl object
} _ui_item_t;

struct ui_page_handle_t {
    const ui_page_desc_t *page;
    ui_theme_t *theme;
    lv_obj_t *parent;
    uint32_t flags_now;
    _ui_item_t *items;
    uint16_t count;
};

static bool _should_enable(uint32_t flags, const ui_widget_desc_t *d)
{
    if ((flags & d->require_mask) != d->require_mask) return false;
    if ((flags & d->block_mask) != 0) return false;
    return true;
}

static lv_obj_t *_make_one(lv_obj_t *parent, ui_theme_t *t, const ui_widget_desc_t *d)
{
    lv_obj_t *o = NULL;

    switch (d->type) {
    case UIW_BUTTON: {
        o = ui_button_create(parent, t, d->text, d->variants, d->toggle);
        if (d->on_event) {
            lv_event_code_t ev = d->toggle ? LV_EVENT_VALUE_CHANGED : LV_EVENT_CLICKED;
            lv_obj_add_event_cb(o, d->on_event, ev, d->user_data);
        }
        if (d->w > 0 && d->h > 0) lv_obj_set_size(o, d->w, d->h);
        lv_obj_align(o, d->align, d->x_ofs, d->y_ofs);
        /* Apply initial toggle state if requested */
        if (d->toggle) {
            if (d->init_checked) {
                lv_obj_add_state(o, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(o, LV_STATE_CHECKED);
            }
        }
        break;
    }
    case UIW_LABEL: {
        o = lv_label_create(parent);
        if (d->text) lv_label_set_text(o, d->text);
        if (d->w > 0 && d->h > 0) lv_obj_set_size(o, d->w, d->h);
        lv_obj_align(o, d->align, d->x_ofs, d->y_ofs);
        break;
    }
    case UIW_BOX: {
        o = lv_obj_create(parent);
        // minimal styling; you can layer your theme styles if desired
        if (d->w > 0 && d->h > 0) lv_obj_set_size(o, d->w, d->h);
        lv_obj_align(o, d->align, d->x_ofs, d->y_ofs);
        break;
    }
    default:
        break;
    }

    return o;
}

static void _apply_enabled(lv_obj_t *o, bool en)
{
    if (!o) return;

    if (en) {
        // Enabled: clear disabled state, leave flags alone
        lv_obj_clear_state(o, LV_STATE_DISABLED);
    } else {
        // Disabled: add disabled state; LVGL will ignore input for it
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}


ui_page_handle_t *ui_page_build(lv_obj_t *parent, ui_theme_t *theme, const ui_page_desc_t *desc)
{
    if (!parent || !theme || !desc || desc->widget_count == 0) return NULL;

    ui_page_handle_t *ph = lv_malloc(sizeof(*ph));
    if (!ph) return NULL;
    ph->page = desc;
    ph->theme = theme;
    ph->parent = parent;
    ph->flags_now = 0;
    ph->count = desc->widget_count;
    ph->items = lv_malloc(sizeof(_ui_item_t) * ph->count);
    if (!ph->items) {
        lv_free(ph);
        return NULL;
    }

    for (uint16_t i = 0; i < ph->count; ++i) {
        ph->items[i].desc = &desc->widgets[i];
        ph->items[i].obj  = _make_one(parent, theme, ph->items[i].desc);
    }
    return ph;
}

void ui_page_set_flags(ui_page_handle_t *ph, uint32_t flags)
{
    if (!ph) return;
    ph->flags_now = flags;
}

void ui_page_apply(ui_page_handle_t *ph)
{
    if (!ph) return;
    for (uint16_t i = 0; i < ph->count; ++i) {
        bool en = _should_enable(ph->flags_now, ph->items[i].desc);
        _apply_enabled(ph->items[i].obj, en);
    }
}

void ui_page_eval_one(ui_page_handle_t *ph, uint16_t index)
{
    if (!ph || index >= ph->count) return;
    bool en = _should_enable(ph->flags_now, ph->items[index].desc);
    _apply_enabled(ph->items[index].obj, en);
}
