// /main/beeper.h
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_BEEPER 39  // beeper line

// Drive beeper pin LOW and configure as output (do this at boot).
void beeper_init_disable(void);

// Convenience toggles (active-high beeper assumed).
void beeper_on(void);
void beeper_off(void);

#ifdef __cplusplus
}
#endif
