#include "fram_wl.h"
#include <string.h>
#include <stdlib.h>       // for malloc/free used below
#include "esp_check.h"    // for ESP_RETURN_ON_ERROR()

static uint32_t crc32_le(const void *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
    }
    return ~crc;
}

static inline uint16_t slot_addr(const fram_wl_t *wl, uint16_t idx) {
    return wl->start + idx * wl->slot_size;
}

esp_err_t fram_wl_mount(fram_wl_t *wl, fm24cl64_t *fram,
                        uint16_t start, uint16_t area_size, uint16_t payload_size)
{
    if (!wl || !fram) return ESP_ERR_INVALID_ARG;
    memset(wl, 0, sizeof(*wl));
    wl->fram = fram;
    wl->start = start;
    wl->area_size = area_size;
    wl->payload_size = payload_size;

    // Overhead: mark(1) + seq(4) + crc(4)
    wl->slot_size  = (uint16_t)(1 + 4 + payload_size + 4);
    if (wl->slot_size == 0 || area_size < wl->slot_size) return ESP_ERR_INVALID_SIZE;
    wl->slot_count = (uint16_t)(area_size / wl->slot_size);
    if (wl->slot_count == 0) return ESP_ERR_INVALID_SIZE;

    wl->cur_idx = 0xFFFF;
    wl->cur_seq = 0;

    // Scan all slots to find the highest valid seq
    uint8_t hdr[1 + 4]; // mark + seq
    uint8_t buf[256];   // payload chunk (we’ll read whole payload in pieces if large)
    for (uint16_t i = 0; i < wl->slot_count; i++) {
        uint16_t a = slot_addr(wl, i);
        esp_err_t err = fm24cl64_read(wl->fram, a, hdr, sizeof(hdr));
        if (err != ESP_OK) return err;
        if (hdr[0] != 0xFE) continue; // not committed

        uint32_t seq = (uint32_t)hdr[1] | ((uint32_t)hdr[2] << 8)
                     | ((uint32_t)hdr[3] << 16) | ((uint32_t)hdr[4] << 24);

        // Read CRC
        uint32_t crc_stored = 0;
        err = fm24cl64_read(wl->fram, a + 1 + 4 + wl->payload_size, &crc_stored, sizeof(crc_stored));
        if (err != ESP_OK) return err;

        // Compute CRC over seq + payload
        uint32_t crc = 0xFFFFFFFFu; // incremental
        crc = ~crc;
        // build a tiny tmp for seq in LE
        uint8_t seq_le[4] = { hdr[1], hdr[2], hdr[3], hdr[4] };
        crc = ~crc;
        crc = crc32_le(seq_le, 4);

        size_t rem = wl->payload_size;
        uint16_t off = 0;
        uint32_t crc_running = 0xFFFFFFFFu;
        // include seq first
        crc_running = 0xFFFFFFFFu;
        crc_running = ~crc_running;
        crc_running = crc32_le(seq_le, 4);

        while (rem) {
            size_t n = rem > sizeof(buf) ? sizeof(buf) : rem;
            err = fm24cl64_read(wl->fram, a + 1 + 4 + off, buf, n);
            if (err != ESP_OK) return err;
            crc_running = crc32_le(buf, n) ^ (crc_running ? 0 : 0); // chained via reinit each call
            // Since crc32_le returns a full CRC (reinit each call), do simple accumulate:
            // easier: recompute over seq+payload in one buffer if payload <= 256
            // but keep it simple: we’ll rebuild a small linear method below.
            // To avoid confusion, recompute plainly after loop:
            rem = 0; // fallback compute below
        }

        // Plain recompute over seq+payload (single pass)
        uint8_t *tmp = (uint8_t *)malloc(4 + wl->payload_size);
        if (!tmp) return ESP_ERR_NO_MEM;
        memcpy(tmp, seq_le, 4);
        err = fm24cl64_read(wl->fram, a + 1 + 4, tmp + 4, wl->payload_size);
        if (err != ESP_OK) { free(tmp); return err; }
        uint32_t crc_calc = crc32_le(tmp, 4 + wl->payload_size);
        free(tmp);

        if (crc_calc != crc_stored) continue; // corrupted

        if (wl->cur_idx == 0xFFFF || (int32_t)(seq - wl->cur_seq) > 0) {
            wl->cur_idx = i;
            wl->cur_seq = seq;
        }
    }

    return ESP_OK;
}

esp_err_t fram_wl_format(fram_wl_t *wl) {
    if (!wl || !wl->fram) return ESP_ERR_INVALID_ARG;

    // Just set all mark bytes to 0xFF (free)
    for (uint16_t i = 0; i < wl->slot_count; i++) {
        uint8_t ff = 0xFF;
        esp_err_t err = fm24cl64_write(wl->fram, slot_addr(wl, i), &ff, 1);
        if (err != ESP_OK) return err;
    }
    wl->cur_idx = 0xFFFF;
    wl->cur_seq = 0;
    return ESP_OK;
}

esp_err_t fram_wl_get_latest(fram_wl_t *wl, void *payload_out, bool *has) {
    if (!wl || !payload_out) return ESP_ERR_INVALID_ARG;
    if (has) *has = false;
    if (wl->cur_idx == 0xFFFF) return ESP_OK;

    uint16_t a = slot_addr(wl, wl->cur_idx);
    // payload starts at +1+4
    esp_err_t err = fm24cl64_read(wl->fram, a + 1 + 4, payload_out, wl->payload_size);
    if (err != ESP_OK) return err;
    if (has) *has = true;
    return ESP_OK;
}

esp_err_t fram_wl_put(fram_wl_t *wl, const void *payload_in) {
    if (!wl || !payload_in) return ESP_ERR_INVALID_ARG;

    uint16_t next_idx = (wl->cur_idx == 0xFFFF) ? 0 : (uint16_t)((wl->cur_idx + 1) % wl->slot_count);
    uint16_t a = slot_addr(wl, next_idx);
    uint32_t seq = wl->cur_seq + 1;

    // Write seq + payload + CRC first (mark remains 0xFF)
    uint8_t seq_le[4] = { (uint8_t)(seq & 0xFF), (uint8_t)((seq >> 8) & 0xFF),
                          (uint8_t)((seq >> 16) & 0xFF), (uint8_t)((seq >> 24) & 0xFF) };
    // payload region
    ESP_RETURN_ON_ERROR(fm24cl64_write(wl->fram, a + 1 + 0, seq_le, 4), "fram_wl", "seq write failed");
    ESP_RETURN_ON_ERROR(fm24cl64_write(wl->fram, a + 1 + 4, payload_in, wl->payload_size), "fram_wl", "payload write failed");

    // compute and store CRC over [seq + payload]
    uint8_t *tmp = (uint8_t *)malloc(4 + wl->payload_size);
    if (!tmp) return ESP_ERR_NO_MEM;
    memcpy(tmp, seq_le, 4);
    memcpy(tmp + 4, payload_in, wl->payload_size);
    uint32_t crc = crc32_le(tmp, 4 + wl->payload_size);
    free(tmp);

    ESP_RETURN_ON_ERROR(fm24cl64_write(wl->fram, a + 1 + 4 + wl->payload_size, &crc, 4), "fram_wl", "crc write failed");

    // Finally set mark to 0xFE (commit)
    uint8_t mark = 0xFE;
    ESP_RETURN_ON_ERROR(fm24cl64_write(wl->fram, a + 0, &mark, 1), "fram_wl", "mark write failed");

    wl->cur_idx = next_idx;
    wl->cur_seq = seq;
    return ESP_OK;
}
