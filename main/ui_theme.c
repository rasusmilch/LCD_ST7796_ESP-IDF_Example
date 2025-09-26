#include "ui_theme.h"

static lv_color_t press_darken(const lv_color_filter_dsc_t *dsc, lv_color_t c, lv_opa_t opa)
{
    LV_UNUSED(dsc);
    return lv_color_darken(c, opa);
}

void ui_theme_init(ui_theme_t *t)
{
    lv_style_init(&t->btn_base);
    lv_style_set_radius(&t->btn_base, 10);
    lv_style_set_bg_opa(&t->btn_base, LV_OPA_COVER);
    lv_style_set_pad_all(&t->btn_base, 10);
    lv_style_set_border_width(&t->btn_base, 2);
    lv_style_set_border_opa(&t->btn_base, LV_OPA_20);

    // Pressed overlay (darken everything a bit)
    lv_color_filter_dsc_init(&t->press_filter, press_darken);
    lv_style_init(&t->btn_pressed);
    lv_style_set_color_filter_dsc(&t->btn_pressed, &t->press_filter);
    lv_style_set_color_filter_opa(&t->btn_pressed, LV_OPA_20);

    // Primary OFF
    lv_style_init(&t->btn_primary);
    lv_style_set_bg_color(&t->btn_primary, lv_palette_lighten(LV_PALETTE_BLUE, 4));
    lv_style_set_bg_grad_color(&t->btn_primary, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_bg_grad_dir(&t->btn_primary, LV_GRAD_DIR_VER);
    lv_style_set_border_color(&t->btn_primary, lv_palette_darken(LV_PALETTE_BLUE, 2));

    // Primary ON (checked)
    lv_style_init(&t->btn_primary_on);
    lv_style_set_bg_color(&t->btn_primary_on, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_bg_grad_color(&t->btn_primary_on, lv_palette_darken(LV_PALETTE_BLUE, 1));
    lv_style_set_border_color(&t->btn_primary_on, lv_palette_darken(LV_PALETTE_BLUE, 3));

    // Danger OFF
    lv_style_init(&t->btn_danger);
    lv_style_set_bg_color(&t->btn_danger, lv_palette_lighten(LV_PALETTE_RED, 4));
    lv_style_set_bg_grad_color(&t->btn_danger, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_bg_grad_dir(&t->btn_danger, LV_GRAD_DIR_VER);
    lv_style_set_border_color(&t->btn_danger, lv_palette_darken(LV_PALETTE_RED, 2));

    // Danger ON (checked)
    lv_style_init(&t->btn_danger_on);
    lv_style_set_bg_color(&t->btn_danger_on, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_bg_grad_color(&t->btn_danger_on, lv_palette_darken(LV_PALETTE_RED, 1));
    lv_style_set_border_color(&t->btn_danger_on, lv_palette_darken(LV_PALETTE_RED, 3));

    // Outline OFF
    lv_style_init(&t->btn_outline);
    lv_style_set_bg_opa(&t->btn_outline, LV_OPA_TRANSP);
    lv_style_set_border_color(&t->btn_outline, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_text_color(&t->btn_outline, lv_palette_main(LV_PALETTE_GREY));

    // Outline ON (checked) — fill it to show “active”
    lv_style_init(&t->btn_outline_on);
    lv_style_set_bg_opa(&t->btn_outline_on, LV_OPA_COVER);
    lv_style_set_bg_color(&t->btn_outline_on, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_text_color(&t->btn_outline_on, lv_color_white());

    // Round helper
    lv_style_init(&t->btn_round);
    lv_style_set_radius(&t->btn_round, LV_RADIUS_CIRCLE);

    // Label style
    lv_style_init(&t->label_on_btn);
    lv_style_set_text_color(&t->label_on_btn, lv_color_black());
}


lv_obj_t *ui_button_create(lv_obj_t *parent, ui_theme_t *t,
                           const char *text, uint32_t variants, bool toggle)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);

    // Base + pressed overlay
    lv_obj_add_style(btn, &t->btn_base, 0);
    lv_obj_add_style(btn, &t->btn_pressed, LV_STATE_PRESSED);

    // Variants (OFF state)
    if (variants & UI_BTN_VARIANT_PRIMARY) lv_obj_add_style(btn, &t->btn_primary, 0);
    if (variants & UI_BTN_VARIANT_DANGER)  lv_obj_add_style(btn, &t->btn_danger,  0);
    if (variants & UI_BTN_VARIANT_OUTLINE) lv_obj_add_style(btn, &t->btn_outline, 0);
    if (variants & UI_BTN_VARIANT_ROUND)   lv_obj_add_style(btn, &t->btn_round,   0);

    // Variants (ON state) — these are only active when LV_STATE_CHECKED is set
    if (variants & UI_BTN_VARIANT_PRIMARY) lv_obj_add_style(btn, &t->btn_primary_on, LV_STATE_CHECKED);
    if (variants & UI_BTN_VARIANT_DANGER)  lv_obj_add_style(btn, &t->btn_danger_on,  LV_STATE_CHECKED);
    if (variants & UI_BTN_VARIANT_OUTLINE) lv_obj_add_style(btn, &t->btn_outline_on, LV_STATE_CHECKED);

    if (toggle) lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);

    // Label
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_add_style(label, &t->label_on_btn, 0);
    lv_label_set_text(label, text ? text : "Button");
    lv_obj_center(label);

    return btn;
}

