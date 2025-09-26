// /main/main.c — Minimal LVGL v9 example on ESP32-S3 + ST7796 (SPI) without PSRAM
// Board: ESP32S3-DEVKIT-C-N8
// Panel: ST7796 480x320 (landscape), SPI
// Touch: (optional) FT6336U via I2C — NOT enabled in this minimal example
//
// This file replaces the raw color-bar demo with a clean LVGL setup that has
// proven-safe DMA usage and no PSRAM dependency. It reuses the same pins you had.
//
// Toolchain: ESP-IDF v5.3+ (tested on v5.5) with managed components:
//   lvgl/lvgl ^9, espressif/esp_lcd_st7796, espressif/esp_lvgl_port (not used here),
//   but we talk to esp_lcd directly for explicit control.
//
// Build:
//   idf.py set-target esp32s3
//   idf.py build flash monitor
//
// If you previously saw "garbage" or core panics, the usual culprits are:
//   - lvgl buffers not in DMA-capable internal RAM
//   - flush callback not waiting for the SPI DMA to finish before reusing buffers
//   - wrong MADCTL (swap_xy/mirror) causing out-of-bounds writes
//   - SPI clock too high on long wires; start at 20 MHz or lower
//
// This example handles those points explicitly.

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"

#include "lvgl.h"

#include "beeper.h"   // keeps GPIO39 low so your buzzer doesn't chirp on boot

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

// -------------------- Pins (from your working bring-up) --------------------
#define PIN_LCD_RST   15
#define PIN_LCD_CS    16
#define PIN_LCD_DC    10
#define PIN_SPI_MOSI  11
#define PIN_SPI_SCK   12
#define PIN_SPI_MISO  13
#define PIN_LCD_BL    14

// Optional FT6336U touch (not used in this minimal file)
// #define PIN_I2C_SDA   1
// #define PIN_I2C_SCL   2
// #define PIN_TOUCH_INT 40
// #define PIN_TOUCH_RST 21

// -------------------- Panel / SPI timing --------------------
#define LCD_HOST            SPI2_HOST
#define LCD_SPI_CLOCK_HZ    (20 * 1000 * 1000)   // start conservative; raise after it's stable

// Pick ONE:
#define ORIENTATION_PORTRAIT
// #define ORIENTATION_LANDSCAPE

#if defined(ORIENTATION_PORTRAIT)
  // Panel: native portrait (no swap)
  const bool swap_xy  = false;
  const bool mirror_x = true;   // flip if text is mirrored; try false if needed
  const bool mirror_y = false;

  // LVGL geometry = 320x480, no rotation
  #undef  LCD_H_RES
  #undef  LCD_V_RES
  #define LCD_H_RES 320
  #define LCD_V_RES 480
  const lv_display_rotation_t lv_rot = LV_DISPLAY_ROTATION_0; // or _180 if upside down

#elif defined(ORIENTATION_LANDSCAPE)
  // Panel: landscape via MV bit (swap XY)
  const bool swap_xy  = true;
  const bool mirror_x = false;   // adjust these two to get text upright
  const bool mirror_y = false;

  // LVGL geometry = 480x320, no rotation
  #undef  LCD_H_RES
  #undef  LCD_V_RES
  #define LCD_H_RES 480
  #define LCD_V_RES 320
  const lv_display_rotation_t lv_rot = LV_DISPLAY_ROTATION_0; // or _180 if needed
#endif


static const char *TAG = "lvgl_st7796_min";

#define LVGL_TASK_STACK   (8 * 1024)
#define LVGL_TASK_PRIO    2
#define LVGL_TASK_CORE    1   // run LVGL on CPU1

static void lvgl_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(10);  // 100 Hz handler, plenty for LVGL
    TickType_t last = xTaskGetTickCount();
    while (true) {
        lv_timer_handler();
        vTaskDelayUntil(&last, period);
    }
}

// -------------------- Backlight simple ON --------------------
static void backlight_on(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_LCD_BL, 1);
}

// -------------------- LVGL tick based on esp_timer --------------------
static uint32_t lv_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL); // ms
}

// -------------------- LCD / LVGL glue --------------------
typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    SemaphoreHandle_t color_done_sem;
    lv_display_t *disp;
} lcd_ctx_t;

static lcd_ctx_t s_lcd = {0};

static bool on_color_trans_done_cb(esp_lcd_panel_io_handle_t io,
                                   esp_lcd_panel_io_event_data_t *edata,
                                   void *user_ctx)
{
    BaseType_t hp_task_woken = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    xSemaphoreGiveFromISR(sem, &hp_task_woken);
    return hp_task_woken == pdTRUE;
}

// Use lv_draw_sw_rgb565_swap() instead
static inline void swap16_bytes_inplace(uint8_t *buf, size_t len_bytes)
{
    // swap bytes within each 16-bit halfword efficiently
    uint32_t *p32 = (uint32_t *)buf;
    size_t n32 = len_bytes >> 2;

    for (size_t i = 0; i < n32; i++) {
        uint32_t v = p32[i];
        p32[i] = ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
    }
    if (len_bytes & 2) { // leftover one 16-bit pixel
        uint8_t *p = buf + (n32 << 2);
        uint8_t t = p[0]; p[0] = p[1]; p[1] = t;
    }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // esp_lcd expects end coords to be EXCLUSIVE
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;

    ESP_LOGD(TAG, "flush (%d,%d)-(%d,%d)", x1, y1, x2-1, y2-1);

    lv_draw_sw_rgb565_swap(px_map, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));

    // Start DMA transfer of this area
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_lcd.panel, x1, y1, x2, y2, px_map));

    // Wait for the DMA to finish before we tell LVGL it can reuse the buffer
    xSemaphoreTake(s_lcd.color_done_sem, portMAX_DELAY);
    lv_display_flush_ready(disp);
}

// -------------------- Minimal LVGL UI: RGB bars + white box --------------------
static void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0); 

    // Three horizontal bars
    int third = LCD_V_RES / 3;

    lv_obj_t *bar_r = lv_obj_create(scr);
    lv_obj_remove_style_all(bar_r);
    lv_obj_set_size(bar_r, LCD_H_RES, third);
    lv_obj_set_style_bg_color(bar_r, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(bar_r, LV_OPA_COVER, 0);
    lv_obj_set_pos(bar_r, 0, 0);

    lv_obj_t *bar_g = lv_obj_create(scr);
    lv_obj_remove_style_all(bar_g);
    lv_obj_set_size(bar_g, LCD_H_RES, third);
    lv_obj_set_style_bg_color(bar_g, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_bg_opa(bar_g, LV_OPA_COVER, 0);
    lv_obj_set_pos(bar_g, 0, third);

    lv_obj_t *bar_b = lv_obj_create(scr);
    lv_obj_remove_style_all(bar_b);
    lv_obj_set_size(bar_b, LCD_H_RES, LCD_V_RES - 2*third);
    lv_obj_set_style_bg_color(bar_b, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(bar_b, LV_OPA_COVER, 0);
    lv_obj_set_pos(bar_b, 0, 2*third);

    // Centered white rectangle (same dimensions as your working demo)
    int bw = LCD_H_RES / 2;
    int bh = LCD_V_RES / 2;
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, bw, bh);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_center(box);

    // Label to prove text rendering is sane
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "LVGL OK");
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_center(lbl);
}

// -------------------- app_main --------------------
void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_DEBUG);   // so ESP_LOGD is visible

    ESP_LOGI(TAG, "Init (ESP-IDF + LVGL v9), ST7796 %dx%d, SPI=%u Hz", LCD_H_RES, LCD_V_RES, LCD_SPI_CLOCK_HZ);
    beeper_init_disable();   // keep GPIO39 low at boot

    // 1) SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SPI_SCK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * 2 + 16,  // 40 lines double-buffered; safe margin
        .flags = SPICOMMON_BUSFLAG_MASTER
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2) Panel IO (command/data over SPI) with a DMA-done ISR -> semaphore
    s_lcd.color_done_sem = xSemaphoreCreateBinary();
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_SPI_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done_cb,
        .user_ctx = s_lcd.color_done_sem,
        .flags = {
            .dc_low_on_data = false,     // ST77xx use D/C high for data
            .octal_mode = 0,
            // .lsb_first = 0,       // (default) keep MSB first on SPI
        }
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_lcd.io));

    // 3) Vendor panel instance
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,  // ST7796 is BGR by default
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,        // <— FIX: high byte first on the bus
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(s_lcd.io, &panel_cfg, &s_lcd.panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd.panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd.panel));

    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_lcd.panel, swap_xy));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_lcd.panel, mirror_x, mirror_y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd.panel, true));


    // 4) LVGL init + display driver
    lv_init();
    lv_tick_set_cb(lv_tick_cb);

    // Create LVGL display
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_default(disp);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    // Make LVGL produce RGB565 in big-endian byte order for the panel
    lv_display_set_rotation(disp, lv_rot);  // 0 or 180 ONLY

    // Allocate 2 DMA-capable draw buffers in internal RAM (no PSRAM)
    const uint32_t buf_lines = 40;
    size_t bpp = lv_color_format_get_size(LV_COLOR_FORMAT_RGB565);
    size_t buf_size_bytes = (size_t)LCD_H_RES * buf_lines * bpp;  // 480*lines in landscape
    void *buf1 = heap_caps_malloc(buf_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    lv_display_set_buffers(disp, buf1, buf2, buf_size_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    // 5) Simple UI
    /* Optional: clear to black once (native geometry) */
    static uint16_t black[LCD_H_RES * 20] = {0};
    for (int y = 0; y < LCD_V_RES; y += 20) {
        esp_lcd_panel_draw_bitmap(s_lcd.panel, 0, y, LCD_H_RES, y + 20, black);
    }


    /* Now enable BL once the frame memory isn’t white */
    backlight_on();

    ESP_LOGI(TAG, "UI create");
    ui_create();


    // immediate kick so we don't wait for the first tick
    // lv_obj_invalidate(lv_screen_active());
    // lv_timer_handler();                  // one synchronous pass

    // Force an immediate refresh now (this should print the flush line instantly)
    // lv_refr_now(disp);

    // Start LVGL on CPU1 and free CPU0 for idle (and Wi-Fi/ISR work)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK,
                            NULL, LVGL_TASK_PRIO, NULL, LVGL_TASK_CORE);

    // app_main no longer needs to sit in a loop; let CPU0 idle feed the WDT
    vTaskDelete(NULL);

}
