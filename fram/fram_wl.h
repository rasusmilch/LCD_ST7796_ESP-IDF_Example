#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "fm24cl64.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A circular array of slots, each:
 * [ 1 byte mark ][ 4 bytes seq ][ payload ... ][ 4 bytes CRC32 ]
 * - mark = 0xFF => free; 0xFE => valid (commit byte written last)
 * - seq increments on each save; the highest valid seq is the latest
 * - CRC32 covers [seq + payload]
 */

typedef struct {
    fm24cl64_t  *fram;
    uint16_t     start;       // FRAM offset of WL area
    uint16_t     area_size;   // size of WL area (bytes)
    uint16_t     slot_size;   // 1 + 4 + payload + 4
    uint16_t     payload_size;
    uint16_t     slot_count;
    uint16_t     cur_idx;     // index of latest valid slot or 0xFFFF if none
    uint32_t     cur_seq;
} fram_wl_t;

esp_err_t fram_wl_mount(fram_wl_t *wl, fm24cl64_t *fram,
                        uint16_t start, uint16_t area_size, uint16_t payload_size);

esp_err_t fram_wl_format(fram_wl_t *wl);

// Load latest payload; returns ESP_OK and sets *has=true if found
esp_err_t fram_wl_get_latest(fram_wl_t *wl, void *payload_out, bool *has);

// Save a new payload version (advances the ring)
esp_err_t fram_wl_put(fram_wl_t *wl, const void *payload_in);

#ifdef __cplusplus
}
#endif
