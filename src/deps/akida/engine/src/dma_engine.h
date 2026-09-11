#pragma once

#include <cstdint>

#include "akida/hardware_device.h"

namespace akida {

namespace dma {

struct Engine {
 public:
  explicit Engine(uint32_t reg_base_addr, uint32_t desc_bytes_size,
                  uint32_t alignment);

  dma::addr descriptor_base_addr;
  const uint32_t descriptor_bytes_size;
  const uint32_t reg_base_addr;
  const uint32_t addr_alignment;
};

struct Config {
  Engine engine;
};

struct Inputs {
  Engine engine;
};

struct Skip {
  Engine engine;
  dma::addr outputs_base_address{0};
};
}  // namespace dma

}  // namespace akida
