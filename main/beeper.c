// /main/beeper.c
#include "driver/gpio.h"
#include "beeper.h"

// Why: set level before direction to prevent an audible glitch on transition.
void beeper_init_disable(void)
{
    gpio_reset_pin(GPIO_BEEPER);
    gpio_set_level(GPIO_BEEPER, 0);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << GPIO_BEEPER,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
    gpio_set_level(GPIO_BEEPER, 0);
}

void beeper_on(void)  { gpio_set_level(GPIO_BEEPER, 1); }
void beeper_off(void) { gpio_set_level(GPIO_BEEPER, 0); }
