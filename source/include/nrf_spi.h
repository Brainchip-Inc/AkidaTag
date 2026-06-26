#ifndef NRF_SPI_H
#define NRF_SPI_H

#include <akd1500/abstract_spi_driver.h>
#include <cstddef>
#include <cstdint>
#include <infra/hardware_driver.h>
#include <vector>
#include <zephyr/device.h>

namespace akida {

class ZephyrSpiDriver : public AbstractSpiDriver {
public:
  const struct device *spi_dev;

  ZephyrSpiDriver();

  // Manually initialize SPI
  int init_spi();

  // SPI functions with address support
  void read_api(uint32_t address, uint8_t *data, size_t size);
  void write_api(uint32_t address, const uint8_t *data, size_t size);
  void spiflashread(uint32_t address, uint8_t *cmd, uint8_t *data,
                    uint32_t size);
  void spiflashwrite(uint32_t address, const uint8_t *data, size_t size);
  void read(uint8_t *data, size_t size) override;
  void write(const uint8_t *data, size_t size) override;
  // Chip select remains unchanged
  void chip_select(uint32_t slave_ID, bool active) override;
};

} // namespace akida
void akd1500_send_custom_cmd(uint8_t opcode, const void *tx_data, size_t tx_len,
                             void *rx_data, size_t rx_len);

#endif // NRF_SPI_H
