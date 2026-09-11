#include "memory_utils.h"

#include <cstddef>

namespace akida {

bool accessible_from_akida(const void* id, const HardwareDriver& driver) {
  if ((sizeof(id) == sizeof(dma::addr)) &&
      (driver.akida_visible_memory() != 0)) {
    // we can safely cast because dma::addr and pointer types have the same size
    const auto addr32 = to_dma_addr(id);
    // if the address is in visible data range we can use it directly
    if (addr32 >= driver.akida_visible_memory() &&
        addr32 <= (driver.akida_visible_memory() +
                   driver.akida_visible_memory_size())) {
      return true;
    }
  }
  return false;
}
}  // namespace akida
