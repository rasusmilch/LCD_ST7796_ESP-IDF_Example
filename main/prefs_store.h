#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "fram_wl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Store versioned preferences as:
 *   data := [uint16_t version][user payload bytes...]
 * CRC32 and wear-leveling are handled by fram_wl (CRC covers version+payload).
 */

typedef struct {
    fram_wl_t  wl;
    uint16_t   version;
    uint16_t   payload_size; // size of your user prefs struct
} prefs_store_t;

/* Initialize preferences area (reuses a fram_wl ring).
 * area_size should hold several slots (>= 4 is nice).
 */
esp_err_t prefs_store_init(prefs_store_t *ps,
                           fm24cl64_t *fram,
                           uint16_t start, uint16_t area_size,
                           uint16_t version, uint16_t payload_size);

/* Load latest preferences:
 * - returns ESP_OK and *has=true if a valid record found AND version matches.
 * - if version mismatch or not found -> *has=false (caller should apply defaults).
 */
esp_err_t prefs_store_load(prefs_store_t *ps, void *payload_out, bool *has);

/* Save preferences (writes version + payload into WL).
 */
esp_err_t prefs_store_save(prefs_store_t *ps, const void *payload_in);

#ifdef __cplusplus
}
#endif
