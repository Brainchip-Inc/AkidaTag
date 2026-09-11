#pragma once

#include <cassert>
#include <cstdint>

namespace akida {

// FNP2 DMA address register: used to configure FNP2 address in DDR. This
// address is an offset after top level register address.
constexpr uint32_t FNP2_DDR_CONF_REG_BASE = 0x10;

static inline uint32_t fnp2_memory_conf(const uint32_t top_level_reg_base,
                                        uint8_t np_id) {
  // Only 4 FNP2 are supported
  assert(np_id <= 3);
  return static_cast<uint32_t>(top_level_reg_base + FNP2_DDR_CONF_REG_BASE +
                               (np_id * sizeof(uint32_t)));
}

}  // namespace akida