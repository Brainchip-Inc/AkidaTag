#pragma once

#include <cassert>
#include <cstddef>

#include "akida/hardware_device.h"
#include "infra/hardware_driver.h"
#include "memory_mgr.h"

namespace akida {

inline dma::addr to_dma_addr(const void* id) {
  assert(sizeof(id) == sizeof(dma::addr));
  return static_cast<dma::addr>(reinterpret_cast<size_t>((id)));
}

bool accessible_from_akida(const void* id, const HardwareDriver& driver);

inline void free_allocated_buffer(MemoryMgr* mem_mgr, dma::addr* ptr) {
  // check if pointer was allocated
  if (*ptr) {
    mem_mgr->free(*ptr);
    // we have to set to 0 to mark we have correctly freed
    *ptr = 0;
  }
}
}  // namespace akida
