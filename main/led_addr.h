#pragma once
#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize a WS2812/FZ2812-compatible strip.
 * @param gpio       Data pin (e.g., GPIO_NUM_36)
 * @param led_count  Number of LEDs on the strip
 * @param use_dma    true to enable RMT DMA (usually unnecessary for short strips)
 */
esp_err_t led_addr_init(gpio_num_t gpio, uint16_t led_count, bool use_dma);

/** Set one LED to RGB (0..255). Call led_addr_show() after batching changes. */
esp_err_t led_addr_set_rgb(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/** Fill entire strip to one color and refresh. */
esp_err_t led_addr_fill(uint8_t r, uint8_t g, uint8_t b);

/** Push buffered pixel data to the strip. */
esp_err_t led_addr_show(void);

/** Clear (turn off) all LEDs and refresh. */
esp_err_t led_addr_clear(void);

/** Simple sanity test pattern (R/G/B repeating). */
void led_addr_selftest(void);

/** Free resources. */
void led_addr_deinit(void);

#ifdef __cplusplus
}
#endif
