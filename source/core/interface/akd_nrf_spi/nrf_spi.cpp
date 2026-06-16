#include "nrf_spi.h"
#include <akd1500/akd1500_spi_driver.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(NRF_SPI, LOG_LEVEL_DBG);

/* SPI configuration for the camera */

static struct spi_config spi_cfg_akida = {
    .frequency = 7000000U, // match Python default for stability
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs =
        {
            .gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(akd)), // akida CS pin
            .delay = 0,
        },
};

static struct spi_config spi_cfg_akida_flash = {
    .frequency = 7000000U, // match Python default for stability
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 1,
    .cs =
        {
            .gpio = SPI_CS_GPIOS_DT_SPEC_GET(
                DT_NODELABEL(akd_flash)), // akida falsh CS pin
            .delay = 0,
        },
};

namespace akida {

int ZephyrSpiDriver::init_spi() {
  this->spi_dev = DEVICE_DT_GET(DT_ALIAS(spi2));
  if (!device_is_ready(this->spi_dev)) {
    LOG_ERR("NRF_SPI: SPI device not ready\n");
    return -ENODEV;
  }

  return 0;
}

void ZephyrSpiDriver::read(uint8_t *data, size_t size) { return; }
void ZephyrSpiDriver::write(const uint8_t *data, size_t size) { return; }

void ZephyrSpiDriver::read_api(uint32_t header_size, uint8_t *data,
                               size_t size) {

  uint8_t tx_buf[size] = {0};

  struct spi_buf tx = {.buf = data, .len = size};
  struct spi_buf rx = {.buf = tx_buf, .len = size};

  struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
  struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
  int err = spi_transceive(this->spi_dev, &spi_cfg_akida, &tx_set, &rx_set);
  if (err != 0) {
    LOG_ERR("NRF_SPI: SPI transfer failed: %d\n", err);
    return;
  }
  memcpy(&data[0], &tx_buf[header_size], size - header_size);

  return;
}

void ZephyrSpiDriver::write_api(uint32_t header_size, const uint8_t *data,
                                size_t size) {

  struct spi_buf tx = {.buf = (uint8_t *)data, .len = size};
  struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
  int err = spi_write(this->spi_dev, &spi_cfg_akida, &tx_set);
  if (err != 0) {
    LOG_ERR("NRF_SPI: SPI write failed: %d", err);
    return;
  }
  return;
}

void ZephyrSpiDriver::chip_select(uint32_t slave_ID, bool active) { return; }

void ZephyrSpiDriver::spiflashwrite(uint32_t address, const uint8_t *data,
                                    size_t size) {

  struct spi_buf tx = {.buf = (uint8_t *)data, .len = size};
  struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
  int err = spi_write(this->spi_dev, &spi_cfg_akida_flash, &tx_set);
  if (err != 0) {
    LOG_ERR("NRF_SPI: SPI write failed: %d", err);
  }
  return;
}

void ZephyrSpiDriver::spiflashread(uint32_t address, uint8_t *cmd,
                                   uint8_t *data, uint32_t size) {
  size_t cmd_bytes = (size >> 16) & 0xFF;

  uint8_t rx_buf[5 + 256] = {0};

  int len = (size & 0xFFFF);
  struct spi_buf tx = {.buf = cmd, .len = len + cmd_bytes};
  struct spi_buf rx = {.buf = rx_buf, .len = len + cmd_bytes};

  struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
  struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
  int err =
      spi_transceive(this->spi_dev, &spi_cfg_akida_flash, &tx_set, &rx_set);
  if (err != 0) {
    LOG_ERR("NRF_SPI: SPI transfer failed: %d\n", err);
    return;
  }

  memcpy(&data[0], &rx_buf[cmd_bytes], len);
  return;
}

} // namespace akida