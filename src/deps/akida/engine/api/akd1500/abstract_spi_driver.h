#pragma once

#include <cstddef>
#include <cstdint>

namespace akida {
class AbstractSpiDriver {
 public:
  virtual ~AbstractSpiDriver() {}

  virtual void read(uint8_t* data, size_t size) = 0;
  virtual void write(const uint8_t* data, size_t size) = 0;
  virtual void chip_select(uint32_t slave_ID, bool active) = 0;
};
}  // namespace akida
