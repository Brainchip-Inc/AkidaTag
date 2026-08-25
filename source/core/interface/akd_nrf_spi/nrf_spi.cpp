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

/* nrfx HAL — used only to (a) read back the SPIM4 FREQUENCY register so the
 * requested clock can be VERIFIED on-target, and (b) set IFTIMING.RXDELAY at
 * runtime for the MISO-sample sweep. Akida is on SPIM4 (nRF5340 high-speed). */
#include <nrf.h>
#include <hal/nrf_spim.h>

/* Resolve the SPIM register base the Zephyr driver uses for the akida bus
 * (node label spi4) — matches DT_REG_ADDR(SPIM(idx)) in spi_nrfx_spim.c, so it
 * is correct regardless of the secure/non-secure (_S/_NS) address alias. */
#define AKD_SPIM_REG ((NRF_SPIM_Type *)DT_REG_ADDR(DT_NODELABEL(spi4)))

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

/* Range the runtime setter accepts. Akida now sits on SPIM4, whose hardware max
 * is 32 MHz (needs HFCLK div1 / 128 MHz core, which the app already runs at).
 * The true reliable max is bounded by the AKD1500 SPI_S core clock (¼-rule,
 * datasheet §3.7.1): ~6 MHz while the core is on the 25 MHz bypass, ~66 MHz once
 * the core is switched to the 400 MHz PLL (see akd_pll_on). */
#define AKD_SPI_MAX_FREQ_HZ 32000000U
#define AKD_SPI_MIN_FREQ_HZ 1000000U

/* nRF5340 SPIM4 generates these discrete SCK rates; the Zephyr driver rounds a
 * requested frequency DOWN to the nearest one (get_nrf_spim_frequency). Mirror
 * that so we can report the clock the hardware will actually run at. 32/16 MHz
 * are SPIM4-only; the driver silently caps to 16 MHz unless HFCLK is div1. */
static uint32_t akd_spi_effective_hz(uint32_t req) {
  static const uint32_t steps[] = {32000000U, 16000000U, 8000000U,
                                    4000000U,  2000000U,  1000000U,
                                    500000U,   250000U,   125000U};
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

/* AKD1500 SPI-slave data-path config.
 *
 * IMPORTANT (Zephyr config-pointer cache): spi_nrfx_spim.c configure() early-
 * returns when spi_context_configured(ctx, cfg) sees the SAME spi_config* as the
 * previous transfer (it compares by POINTER, not by value). So mutating
 * .frequency in place and re-passing the same struct is a NO-OP — the SCK never
 * changes until some other path (a flash access with a different pointer) forces
 * a reconfigure. That silent no-op invalidated earlier freq experiments.
 *
 * Fix: keep TWO identical config structs and alternate which one we hand to
 * spi_transceive() whenever the frequency/mode changes. The pointer identity
 * flips, so the driver re-runs configure() and actually re-writes FREQUENCY
 * (and re-applies the DT rx-delay). We reuse Zephyr's own prescaler + 128 MHz
 * validation this way. Steady-state (no freq change) keeps the same pointer, so
 * there is no per-transfer reconfigure churn. */
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

static struct spi_config spi_cfg_akida_mirror = {
    .frequency = AKD_SPI_BRINGUP_FREQ_HZ,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs =
        {
            .gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(akd)),
            .delay = 0,
        },
};

static struct spi_config *p_akida_cfg = &spi_cfg_akida;
static bool akida_cfg_dirty = false;

/* Return the config pointer to hand to spi_transceive(). If a freq/mode change
 * is pending, flip to the other struct first so the driver re-configures. The
 * flip happens inside the transfer path, so it is always immediately followed by
 * a transaction — we never double-flip back onto the cached pointer. */
static struct spi_config *akida_active_cfg(void) {
  if (akida_cfg_dirty) {
    p_akida_cfg = (p_akida_cfg == &spi_cfg_akida) ? &spi_cfg_akida_mirror
                                                  : &spi_cfg_akida;
    akida_cfg_dirty = false;
  }
  return p_akida_cfg;
}

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
    LOG_ERR("SPI freq %u Hz out of range [%u, %u]", hz, AKD_SPI_MIN_FREQ_HZ,
            AKD_SPI_MAX_FREQ_HZ);
    return -EINVAL;
  }
  /* Update BOTH structs and flag dirty so the next transfer flips the pointer
   * and forces the driver to re-apply FREQUENCY (see akida_active_cfg). */
  spi_cfg_akida.frequency = hz;
  spi_cfg_akida_mirror.frequency = hz;
  akida_cfg_dirty = true;
  uint32_t eff = akd_spi_effective_hz(hz);
  if (eff != hz) {
    LOG_INF("AKD1500 host SPI: requested %u Hz -> %u Hz actual (SPIM4 step)", hz,
            eff);
  } else {
    LOG_INF("AKD1500 host SPI frequency set to %u Hz", eff);
  }
  return 0;
}

/* Returns the actual SCK the SPIM will run at (requested value rounded down to
 * the nearest SPIM4 step), not the raw requested value. */
extern "C" uint32_t akd_spi_get_frequency(void) {
  return akd_spi_effective_hz(spi_cfg_akida.frequency);
}

/* Read back the live SPIM4 FREQUENCY register so the requested clock can be
 * VERIFIED on-target (defeats any lingering "the SCK never actually changed"
 * doubt). Raw register value: M32=0x14000000, M16=0x0A000000, M8=0x80000000,
 * M4=0x40000000, M2=0x20000000, M1=0x10000000. */
extern "C" uint32_t akd_spi_read_freq_reg(void) {
  return AKD_SPIM_REG->FREQUENCY;
}

/* Set IFTIMING.RXDELAY (MISO sample delay, 0-7 x 64 MHz cycles) directly on the
 * SPIM4 HAL. A Zephyr reconfigure (freq/mode change) resets this to the DT
 * value (0), so apply it AFTER the frequency has settled. */
extern "C" void akd_spi_apply_rxdelay(uint32_t rxdelay) {
  if (rxdelay > 7) {
    rxdelay = 7;
  }
  nrf_spim_iftiming_set(AKD_SPIM_REG, rxdelay);
  LOG_INF("AKD1500 host SPI rx-delay set to %u", rxdelay);
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
  int err = spi_transceive(this->spi_dev, akida_active_cfg(), &tx_set, &rx_set);
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
  int err = spi_write(this->spi_dev, akida_active_cfg(), &tx_set);
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