#include "akd1000/bare_metal_driver.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "infra/registers_common.h"

#include "akd1000/registers_soc.h"

namespace akida {

constexpr uint32_t regs_offset = 0xFCC00000u;

BareMetalDriver::BareMetalDriver(uint32_t scratch_base_address,
                                 uint32_t scratch_size,
                                 uint32_t akida_visible_memory_base,
                                 uint32_t akida_visible_memory_size)
    : scratch_base_addr_(scratch_base_address),
      scratch_size_(scratch_size),
      akida_visible_mem_base_(akida_visible_memory_base),
      akida_visible_mem_size_(akida_visible_memory_size) {}

void BareMetalDriver::read(uint32_t address, void* data, size_t size) const {
  memcpy(data, reinterpret_cast<void*>(address), size);
}

void BareMetalDriver::write(uint32_t address, const void* data, size_t size) {
  memcpy(reinterpret_cast<void*>(address), data, size);
}

const char* BareMetalDriver::desc() const {
  static char version_str[32];
  auto reg = read32(REG_CHIP_INFO);
  auto version = get_field(reg, REG_CHIP_VERSION);
  snprintf(version_str, sizeof(version_str), "Embedded/NSoC_v%ld", version);
  return version_str;
}

}  // namespace akida
