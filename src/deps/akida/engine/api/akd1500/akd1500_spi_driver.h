#pragma once

#include <cstddef>
#include <cstdint>

#include "abstract_spi_driver.h"

#include "akd1500/memory_mapping.h"
#include "infra/hardware_driver.h"

namespace akida {

class Akd1500SpiDriver : public HardwareDriver {
 public:
  explicit Akd1500SpiDriver(AbstractSpiDriver* spi_driver,
                            uint32_t akida_visible_memory_base,
                            uint32_t akida_visible_memory_size);

  const char* desc() const override { return "SPI/AKD1500"; }

  uint32_t scratch_memory() const override {
    static constexpr uint32_t kSpiMemoryBase = 0xfc800000;
    return kSpiMemoryBase;
  }

  uint32_t scratch_size() const override {
    return soc::akd1500::kMainMemorySize;
  }

  uint32_t top_level_reg() const override {
    return soc::akd1500::kTopLevelRegBase;
  }

  uint32_t akida_visible_memory() const override {
    return akida_visible_memory_base_;
  }

  uint32_t akida_visible_memory_size() const override {
    return akida_visible_memory_size_;
  }

  void read(uint32_t address, void* data, size_t size) const override;
  void write(uint32_t address, const void* data, size_t size) override;

 protected:
  AbstractSpiDriver* spi_driver_;
  uint32_t akida_visible_memory_base_;
  uint32_t akida_visible_memory_size_;
};

}  // namespace akida
