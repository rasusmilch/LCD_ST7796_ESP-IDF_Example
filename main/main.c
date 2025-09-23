// /main/main.c
// ESP-IDF v5.5 — ESP32-S3 DevKitC-N8, ST7796 SPI bring-up (NO LVGL).
// Horizontal RGB bars + centered white box. DMA completion sync. MADCTL fixes Y scan.

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "beeper.h"

static const char *TAG = "st7796_no_lvgl";

// Pins
#define PIN_LCD_RST   15
#define PIN_LCD_CS    16
#define PIN_LCD_DC    10
#define PIN_SPI_MOSI  11
#define PIN_SPI_SCK   12
#define PIN_SPI_MISO  13
#define PIN_LCD_BL    14

// SPI/LCD
#define LCD_HOST             SPI2_HOST
#define LCD_SPI_CLOCK_HZ     (10 * 1000 * 1000)
#define LCD_W                320       // portrait columns
#define LCD_H                480       // portrait rows
#define CHUNK_LINES          40

// ST7796 regs/bits
#define REG_MADCTL           0x36
#define REG_COLMOD           0x3A
#define REG_CASET            0x2A
#define REG_PASET            0x2B
#define REG_RAMWR            0x2C
#define MADCTL_MY            0x80
#define MADCTL_MX            0x40
#define MADCTL_MV            0x20
#define MADCTL_BGR           0x08

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void backlight_on(void) {
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));
    ledc_channel_config_t ccfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0, .gpio_num = PIN_LCD_BL,
        .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_0, .duty = (1 << 13) - 1, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
}

static esp_err_t wr_param(esp_lcd_panel_io_handle_t io, uint8_t reg, const void *data, size_t len) {
    return esp_lcd_panel_io_tx_param(io, reg, data, len);
}

// ---- DMA completion sync ----
static SemaphoreHandle_t s_color_done_sem = NULL;
static bool on_color_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata,
                          void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &hp);
    return hp == pdTRUE; // yield if needed
}
static void wait_color_done(void) { xSemaphoreTake(s_color_done_sem, portMAX_DELAY); }

// ---- Raw helpers ----
static void set_window(esp_lcd_panel_io_handle_t io,
                       uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t ca[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t pa[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    ESP_ERROR_CHECK(wr_param(io, REG_CASET, ca, 4));
    ESP_ERROR_CHECK(wr_param(io, REG_PASET, pa, 4));
}
static void push_color(esp_lcd_panel_io_handle_t io, const void *data, size_t bytes)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io, REG_RAMWR, data, bytes));
    wait_color_done(); // must finish before reusing the buffer
}

// Pack one solid RGB565 color into a byte buffer as HI,LO
static void pack_solid_color(uint8_t *dst, int pixels, uint16_t c)
{
    uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)(c & 0xFF);
    for (int i = 0; i < pixels; ++i) { *dst++ = hi; *dst++ = lo; }
}

void app_main(void) {
    ESP_LOGI(TAG, "ST7796 bring-up (portrait %dx%d, SPI=%u Hz)", LCD_W, LCD_H, LCD_SPI_CLOCK_HZ);
    beeper_init_disable();
    backlight_on();

    // SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SPI_SCK,
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_W * CHUNK_LINES * 2 + 16,
        .flags = SPICOMMON_BUSFLAG_MASTER
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Panel IO + DMA-done callback
    s_color_done_sem = xSemaphoreCreateBinary();
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_SPI_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 1,      // single in-flight
        .on_color_trans_done = NULL, // register via callback API
        .user_ctx = NULL,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_done };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io, &cbs, (void*)s_color_done_sem));

    // ST7796 panel (RGB element order)
    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    // Pixel format + orientation: 16bpp, portrait, **flip Y** (MY) to correct bar order.
    uint8_t colmod = 0x55;                 // 16bpp
    uint8_t madctl = MADCTL_MY;            // vertical mirror only (top-left origin for our y=0)
    ESP_ERROR_CHECK(wr_param(io, REG_COLMOD, &colmod, 1));
    ESP_ERROR_CHECK(wr_param(io, REG_MADCTL, &madctl, 1));

    // ---- Test 1: horizontal bars (top=RED, mid=GREEN, bottom=BLUE) ----
    static uint8_t full_chunk[LCD_W * CHUNK_LINES * 2]; // bytes
    const uint16_t RED   = rgb565(255, 0,   0);
    const uint16_t GREEN = rgb565(0,   255, 0);
    const uint16_t BLUE  = rgb565(0,   0,  255);
    const uint16_t WHITE = rgb565(255, 255, 255);

    int third = LCD_H / 3;
    struct { int y0, y1; uint16_t c; } segs[3] = {
        { 0,          third - 1,   RED   },
        { third,      third*2 - 1, GREEN },
        { third*2,    LCD_H - 1,   BLUE  },
    };

    for (int s = 0; s < 3; ++s) {
        for (int y = segs[s].y0; y <= segs[s].y1; ) {
            int lines = segs[s].y1 - y + 1;
            if (lines > CHUNK_LINES) lines = CHUNK_LINES;
            pack_solid_color(full_chunk, LCD_W * lines, segs[s].c);
            set_window(io, 0, y, LCD_W - 1, y + lines - 1);   // inclusive
            push_color(io, full_chunk, (size_t)LCD_W * lines * 2);
            y += lines;
        }
    }

    // ---- Test 2: centered white box (no overwrite) ----
    int bw = LCD_W / 2,  bh = LCD_H / 6;
    int bx = (LCD_W - bw) / 2, by = (LCD_H - bh) / 2;
    static uint8_t box_chunk[(LCD_W / 2) * CHUNK_LINES * 2];
    for (int y = by; y < by + bh; ) {
        int lines = (by + bh) - y;
        if (lines > CHUNK_LINES) lines = CHUNK_LINES;
        pack_solid_color(box_chunk, bw * lines, WHITE);
        set_window(io, bx, y, bx + bw - 1, y + lines - 1);
        push_color(io, box_chunk, (size_t)bw * lines * 2);
        y += lines;
    }

    ESP_LOGI(TAG, "Done. Expect TOP: red, MIDDLE: green, BOTTOM: blue, white box centered.");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
