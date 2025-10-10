#pragma once
#include "lvgl.h"
#include "ui_theme.h"
#include <stdbool.h>
#include <stdint.h>

// App-level flags you can gate widgets on (add more as needed)
enum {
  UIF_POWER = 1u << 0,    // power must be ON
  UIF_ADVANCED = 1u << 1, // advanced mode enabled
  UIF_LOCKED = 1u << 2,   // example: safety lock active
                          // ...
};

// Optional “groups” a widget can belong to (for bulk operations, etc.)
enum {
  UGRP_DEFAULT = 1u << 0,
  UGRP_SYSTEM = 1u << 1,
  UGRP_DEBUG = 1u << 2,
  // ...
};

// Widget types (add more if you like)
typedef enum {
  UIW_BUTTON,
  UIW_LABEL,
  UIW_BOX,
  UIW_SPACER
} ui_widget_type_t;

typedef struct {
  ui_widget_type_t type;

  // Common geometry
  lv_align_t align;
  lv_coord_t x_ofs;
  lv_coord_t y_ofs;
  lv_coord_t w;
  lv_coord_t h;

  // Common text (used by buttons/labels)
  const char *text;

  // Button-only styling/behavior
  uint32_t variants; // UI_BTN_VARIANT_* bitmask
  bool toggle;       // checkable button
  bool init_checked; // initial state for toggle buttons (true = starts checked)

  // Native LVGL callback
  lv_event_cb_t on_event;
  void *user_data;

  // Enable/disable logic
  uint32_t require_mask; // all of these flags must be present
  uint32_t block_mask;   // none of these flags may be present

  // Optional grouping for later bulk ops
  uint32_t groups_mask;
} ui_widget_desc_t;

typedef struct {
  const char *name;
  const ui_widget_desc_t *widgets;
  uint16_t widget_count;
} ui_page_desc_t;

// Opaque runtime handles
typedef struct ui_page_handle_t ui_page_handle_t;

// Build a page (creates widgets as lv_objs). Returns a handle you can update
// later.
ui_page_handle_t *ui_page_build(lv_obj_t *parent, ui_theme_t *theme,
                                const ui_page_desc_t *desc);

// Set and apply the current app flags -> enables/disables widgets accordingly
void ui_page_set_flags(ui_page_handle_t *ph, uint32_t flags);
void ui_page_apply(ui_page_handle_t *ph);

// Quick helper if you need to re-eval a single widget externally
void ui_page_eval_one(ui_page_handle_t *ph, uint16_t index);
