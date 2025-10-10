#include "fm24cl64.h"
#include <string.h>

esp_err_t fm24cl64_init(const fm24cl64_config_t *cfg, fm24cl64_t *out) {
    if (!cfg || !out || !cfg->bus) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = cfg->i2c_addr ? cfg->i2c_addr : 0x50,
        .scl_speed_hz    = cfg->scl_speed_hz ? cfg->scl_speed_hz : 100000
    };
    out->bus = cfg->bus;
    out->i2c_addr = dev_cfg.device_address;

    return i2c_master_bus_add_device(cfg->bus, &dev_cfg, &out->dev);
}

esp_err_t fm24cl64_init_on_bus(i2c_master_bus_handle_t bus,
                               uint8_t i2c_addr,
                               fm24cl64_t *out)
{
    fm24cl64_config_t cfg = {
        .bus         = bus,
        .i2c_addr    = i2c_addr,
        .scl_speed_hz = 100000,   // or whatever you prefer
    };
    return fm24cl64_init(&cfg, out);
}

static inline esp_err_t write_addr_then_rx(fm24cl64_t *dev, uint16_t addr,
                                           void *rx, size_t rxlen, int timeout_ms)
{
    uint8_t a[2] = { (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF) };
    return i2c_master_transmit_receive(dev->dev, a, 2, rx, rxlen, timeout_ms);
}

static inline esp_err_t tx_addr_then_tx_data(fm24cl64_t *dev, uint16_t addr,
                                             const uint8_t *data, size_t len, int timeout_ms)
{
    // Chunk to keep stack small and transactions reasonable
    uint8_t buf[2 + 128];
    while (len) {
        size_t n = len > 128 ? 128 : len;
        buf[0] = (uint8_t)(addr >> 8);
        buf[1] = (uint8_t)(addr & 0xFF);
        memcpy(&buf[2], data, n);
        esp_err_t err = i2c_master_transmit(dev->dev, buf, 2 + n, timeout_ms);
        if (err != ESP_OK) return err;
        addr += n;
        data += n;
        len  -= n;
    }
    return ESP_OK;
}

esp_err_t fm24cl64_read(fm24cl64_t *dev, uint16_t addr, void *data, size_t len) {
    if (!dev || !data) return ESP_ERR_INVALID_ARG;
    if ((uint32_t)addr + len > 8192) return ESP_ERR_INVALID_SIZE;
    return write_addr_then_rx(dev, addr, data, len, 50);
}

esp_err_t fm24cl64_write(fm24cl64_t *dev, uint16_t addr, const void *data, size_t len) {
    if (!dev || !data) return ESP_ERR_INVALID_ARG;
    if ((uint32_t)addr + len > 8192) return ESP_ERR_INVALID_SIZE;
    // FRAM has no write latency/erase cycles. Just transmit bytes.
    return tx_addr_then_tx_data(dev, addr, (const uint8_t *)data, len, 50);
}
