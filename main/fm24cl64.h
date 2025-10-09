#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 64-kbit (8 KiB) FRAM with 16-bit word address.
 * Default 7-bit I2C address is 0x50 (A2..A0 = 0).
 */

typedef struct {
    i2c_master_bus_handle_t bus;   // existing I2C master bus (from i2c_new_master_bus)
    uint8_t                  i2c_addr;      // 0x50..0x57 according to A2/A1/A0
    uint32_t                 scl_speed_hz;  // e.g. 400000
} fm24cl64_config_t;

typedef struct {
    i2c_master_bus_handle_t  bus;
    i2c_master_dev_handle_t  dev;
    uint8_t                  i2c_addr;
} fm24cl64_t;

esp_err_t fm24cl64_init(const fm24cl64_config_t *cfg, fm24cl64_t *out);
esp_err_t fm24cl64_read (fm24cl64_t *dev, uint16_t addr, void *data, size_t len);
esp_err_t fm24cl64_write(fm24cl64_t *dev, uint16_t addr, const void *data, size_t len);

#ifdef __cplusplus
}
#endif
