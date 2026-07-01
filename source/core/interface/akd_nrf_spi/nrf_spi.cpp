#include "nrf_spi.h"
#include <akd1500/akd1500_spi_driver.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(NRF_SPI, LOG_LEVEL_DBG);

/* Target AKD1500 host SPI clock once the chip is up (Kconfig; DK stays slow). */
#ifndef CONFIG_AKD_SPI_FREQ_HZ
#define CONFIG_AKD_SPI_FREQ_HZ 1400000
#endif

/* Conservative clock used for the very first AKD1500 accesses during bring-up.
 * Safe even if the AKD1500 PLL has not yet locked (SPI_S core clock still on the
 * slow bypass, per datasheet section 3.7.1). akd_spi_set_frequency() raises the
 * data path to CONFIG_AKD_SPI_FREQ_HZ once the device is confirmed alive. */
#define AKD_SPI_BRINGUP_FREQ_HZ 1400000U

/* Range the runtime setter accepts. Capped at 8 MHz because Akida sits on
 * SPIM2, whose hardware max is 8 MHz (SPIM0-3). Note: HW testing showed AKD1500
 * model programming (the DMA-config handshake) times out at 8 MHz even though
 * reads/SRAM/inference tolerate it, so the reliable operating max is ~4 MHz. */
#define AKD_SPI_MAX_FREQ_HZ 8000000U
#define AKD_SPI_MIN_FREQ_HZ 1000000U

/* nRF5340 SERIAL/SPIM2 only generates these discrete SCK rates; the driver
 * rounds a requested frequency DOWN to the nearest one. Mirror that so we can
 * report the clock the hardware will actually run at. */
static uint32_t akd_spi_effective_hz(uint32_t req) {
  static const uint32_t steps[] = {8000000U, 4000000U, 2000000U, 1000000U,
                                    500000U,  250000U,  125000U};
  for (size_t i = 0; i < ARRAY_SIZE(steps); i++) {
    if (req >= steps[i]) {
      return steps[i];
    }
  }
  return steps[ARRAY_SIZE(steps) - 1];
}

namespace akida {
ZephyrSpiDriver::ZephyrSpiDriver() {
  int ret = init_spi();
  if (ret != 0) {
    LOG_ERR("Failed to initialize SPI in constructor! Error: %d", ret);
  } else {
    LOG_INF("SPI initialized in ZephyrSpiDriver constructor.");
  }
}
} // namespace akida

/* SPI configuration for the camera */

static struct spi_config spi_cfg_akida = {
    .frequency = AKD_SPI_BRINGUP_FREQ_HZ, // raised after bring-up (data path)
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs =
        {
            .gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(akd)), // akida CS pin
            .delay = 0,
        },
};

static struct spi_config spi_cfg_akida_flash = {
    .frequency = AKD_SPI_BRINGUP_FREQ_HZ, // raised after bring-up (data path)
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 1,
    .cs =
        {
            .gpio = SPI_CS_GPIOS_DT_SPEC_GET(
                DT_NODELABEL(akd_flash)), // akida falsh CS pin
            .delay = 0,
        },
};

/* Set the AKD1500 host SPI (data path) clock. spi_cfg_akida is passed by pointer
 * to every spi_transceive()/spi_write(), so the new rate takes effect on the
 * next transaction with no re-init. The nRF SPIM rounds the request down to its
 * nearest supported divider (SPIM2 caps at 8 MHz; SPIM4 at 32 MHz). Change this
 * while KWS is stopped to avoid racing an in-flight inference transfer. */
extern "C" int akd_spi_set_frequency(uint32_t hz) {
  if (hz < AKD_SPI_MIN_FREQ_HZ || hz > AKD_SPI_MAX_FREQ_HZ) {
    LOG_ERR("SPI freq %u Hz out of range [%u, %u] (SPIM2 max)", hz,
            AKD_SPI_MIN_FREQ_HZ, AKD_SPI_MAX_FREQ_HZ);
    return -EINVAL;
  }
  spi_cfg_akida.frequency = hz;
  uint32_t eff = akd_spi_effective_hz(hz);
  if (eff != hz) {
    LOG_INF("AKD1500 host SPI: requested %u Hz -> %u Hz actual (SPIM2 step)", hz,
            eff);
  } else {
    LOG_INF("AKD1500 host SPI frequency set to %u Hz", eff);
  }
  return 0;
}

/* Returns the actual SCK the SPIM will run at (requested value rounded down to
 * the nearest SPIM2 step), not the raw requested value. */
extern "C" uint32_t akd_spi_get_frequency(void) {
  return akd_spi_effective_hz(spi_cfg_akida.frequency);
}

namespace akida {

int ZephyrSpiDriver::init_spi() {
  /* Resolve the Akida SPI bus via the board `akd-spi` alias (SPIM2 on both
   * boards today). The alias keeps the door open to remap to SPIM4 later once
   * the AKD1500 SPI_S core clock is raised to allow >8 MHz host clocks. */
  this->spi_dev = DEVICE_DT_GET(DT_ALIAS(akd_spi));
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