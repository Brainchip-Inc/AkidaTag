#pragma once

// This needs to be in an extern "C" block because C++ will generate name
// mangling for nested extern declarations
extern "C" {
#include "cmsis_compiler.h"
}
#if (defined(__ARM_ARCH_7EM__) && (__ARM_ARCH_7EM__ == 1))
#include <cstdint>

#include "akd1000/memory_mapping.h"
#include "infra/hardware_driver.h"

namespace akida {

class BareMetalDriver final : public HardwareDriver {
 public:
  BareMetalDriver(uint32_t scratch_base_address, uint32_t scratch_size,
                  uint32_t akida_visible_memory_base,
                  uint32_t akida_visible_memory_size);

  void read(uint32_t address, void* data, size_t size) const override;

  void write(uint32_t address, const void* data, size_t size) override;

  const char* desc() const override;

  uint32_t scratch_memory() const override { return scratch_base_addr_; }

  uint32_t scratch_size() const override { return scratch_size_; }

  uint32_t top_level_reg() const override {
    return soc::akd1000::kTopLevelRegBase;
  }

  uint32_t akida_visible_memory() const override {
    return akida_visible_mem_base_;
  }

  uint32_t akida_visible_memory_size() const override {
    return akida_visible_mem_size_;
  }

 protected:
  uint32_t scratch_base_addr_;
  uint32_t scratch_size_;
  uint32_t akida_visible_mem_base_;
  uint32_t akida_visible_mem_size_;
};

}  // namespace akida

#else
#error This file should only be built for Cortex M4F (armv7e-m) target
#endif
