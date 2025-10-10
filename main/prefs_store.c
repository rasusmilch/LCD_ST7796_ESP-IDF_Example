#include "prefs_store.h"
#include <string.h>
#include "esp_check.h"    // for ESP_RETURN_ON_ERROR()

esp_err_t prefs_store_init(prefs_store_t *ps,
                           fm24cl64_t *fram,
                           uint16_t start, uint16_t area_size,
                           uint16_t version, uint16_t payload_size)
{
    if (!ps) return ESP_ERR_INVALID_ARG;
    memset(ps, 0, sizeof(*ps));
    ps->version = version;
    ps->payload_size = payload_size;
    // WL payload includes version field at the front
    ESP_RETURN_ON_ERROR(
        fram_wl_mount(&ps->wl, fram, start, area_size, (uint16_t)(2 + payload_size)),
        "prefs", "wl mount failed"
    );
    return ESP_OK;
}

esp_err_t prefs_store_load(prefs_store_t *ps, void *payload_out, bool *has) {
    if (!ps || !payload_out) return ESP_ERR_INVALID_ARG;
    if (has) *has = false;

    uint8_t *buf = (uint8_t *)malloc(2 + ps->payload_size);
    if (!buf) return ESP_ERR_NO_MEM;

    bool got = false;
    esp_err_t err = fram_wl_get_latest(&ps->wl, buf, &got);
    if (err != ESP_OK) { free(buf); return err; }

    if (got) {
        uint16_t v = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        if (v == ps->version) {
            memcpy(payload_out, buf + 2, ps->payload_size);
            if (has) *has = true;
        }
        // else version mismatch – caller should apply defaults
    }

    free(buf);
    return ESP_OK;
}

esp_err_t prefs_store_save(prefs_store_t *ps, const void *payload_in) {
    if (!ps || !payload_in) return ESP_ERR_INVALID_ARG;

    uint8_t *buf = (uint8_t *)malloc(2 + ps->payload_size);
    if (!buf) return ESP_ERR_NO_MEM;
    buf[0] = (uint8_t)(ps->version & 0xFF);
    buf[1] = (uint8_t)((ps->version >> 8) & 0xFF);
    memcpy(buf + 2, payload_in, ps->payload_size);

    esp_err_t err = fram_wl_put(&ps->wl, buf);
    free(buf);
    return err;
}
