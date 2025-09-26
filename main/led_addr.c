#include "led_addr.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_log.h"

static const char *TAG = "led_addr";

static led_strip_handle_t s_strip = NULL;
static uint16_t s_count = 0;

esp_err_t led_addr_init(gpio_num_t gpio, uint16_t led_count, bool use_dma)
{
    if (s_strip) return ESP_OK;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = (int)gpio,
        .max_leds         = led_count,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,   // WS2812/FZ2812 are GRB on the wire
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src          = RMT_CLK_SRC_DEFAULT,
        .resolution_hz    = 10 * 1000 * 1000,       // 10 MHz base = standard WS2812 timing
        .mem_block_symbols = 0,                      // 0 = auto (enough for short strips)
        .flags.with_dma   = use_dma,                 // false is fine for 22 LEDs
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %d", err);
        return err;
    }

    s_count = led_count;
    ESP_ERROR_CHECK(led_strip_clear(s_strip));      // all off
    ESP_LOGI(TAG, "Initialized strip on GPIO %d, %u LEDs", (int)gpio, (unsigned)led_count);
    return ESP_OK;
}

esp_err_t led_addr_set_rgb(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    if (index >= s_count) return ESP_ERR_INVALID_ARG;
    return led_strip_set_pixel(s_strip, index, r, g, b);
}

esp_err_t led_addr_show(void)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    return led_strip_refresh(s_strip);
}

esp_err_t led_addr_fill(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    for (uint16_t i = 0; i < s_count; i++) {
        esp_err_t err = led_strip_set_pixel(s_strip, i, r, g, b);
        if (err != ESP_OK) return err;
    }
    return led_addr_show();
}

esp_err_t led_addr_clear(void)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    return led_strip_clear(s_strip);
}

void led_addr_selftest(void)
{
    if (!s_strip) return;
    // R/G/B repeating along the strip
    for (uint16_t i = 0; i < s_count; i++) {
        uint8_t r = (i % 3) == 0 ? 255 : 0;
        uint8_t g = (i % 3) == 1 ? 255 : 0;
        uint8_t b = (i % 3) == 2 ? 255 : 0;
        led_strip_set_pixel(s_strip, i, r, g, b);
    }
    led_strip_refresh(s_strip);
}

void led_addr_deinit(void)
{
    if (s_strip) {
        led_strip_clear(s_strip);
        led_strip_del(s_strip);
        s_strip = NULL;
        s_count = 0;
        ESP_LOGI(TAG, "Strip deinitialized");
    }
}
