#pragma once
#include <cstddef>
#include <utility>
#include <vector>

#include "akida/hardware_device.h"
#include "engine/dma.h"

namespace akida {

class MemoryMgr {
 public:
  struct Allocation {
    dma::addr addr;
    size_t size;
  };

  explicit MemoryMgr(const dma::addr base, const dma::addr size)
      : mem_base_offset_(base),
        mem_offset_(base),
        mem_top_offset_(base),
        mem_bottom_offset_(base + size) {}

  // This will give DDR memory (e.g.: to use for FNP2 filters).
  dma::addr alloc(size_t byte_size, const uint32_t alignment = dma::kAlignment);

  // This will mark previously allocated memory as free
  void free(dma::addr addr);

  // Return the memory used currently in the device
  using MemoryInfo = std::pair<uint32_t, uint32_t>;
  MemoryInfo report() const;

  // reset the top usage to the current usage
  void reset_top_usage();

  // Free all memory allocations
  void reset();

 private:
  const dma::addr mem_base_offset_;
  dma::addr mem_offset_;
  dma::addr mem_top_offset_;
  const dma::addr mem_bottom_offset_;
  std::vector<Allocation> scratch_buf_;
};

}  // namespace akida
