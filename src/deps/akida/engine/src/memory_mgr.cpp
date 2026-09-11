
#include "memory_mgr.h"

#include <cassert>
#include <cstdint>
#include "infra/int_ops.h"
#include "infra/system.h"

namespace akida {

MemoryMgr::MemoryInfo MemoryMgr::report() const {
  auto current_memory = mem_offset_ - mem_base_offset_;
  auto top_memory = mem_top_offset_ - mem_base_offset_;
  return std::make_pair(current_memory, top_memory);
}

void MemoryMgr::reset_top_usage() { mem_top_offset_ = mem_offset_; }

dma::addr MemoryMgr::alloc(size_t size, const uint32_t alignment) {
  assert(size > 0 && "Cannot alloc size 0");
  if (alignment % dma::kAlignment != 0) {
    panic(
        "Invalid memory alignment (requested %u bytes, but should be a "
        "multiple of %u bytes)",
        static_cast<uint32_t>(alignment),
        static_cast<uint32_t>(dma::kAlignment));
  }
  // base address must be aligned
  const auto aligned_mem_offset = align_up(mem_offset_, alignment);
  // check that we have enough memory left
  if (aligned_mem_offset + size > mem_bottom_offset_) {
    panic(
        "Out of memory (requested %u bytes, currently using %u bytes, "
        "available %u bytes)",
        static_cast<uint32_t>(size),
        static_cast<uint32_t>(mem_offset_ - mem_base_offset_),
        static_cast<uint32_t>(mem_bottom_offset_ - mem_offset_));
  }
  scratch_buf_.push_back({aligned_mem_offset, size});
  mem_offset_ = aligned_mem_offset + static_cast<uint32_t>(size);
  // update top memory usage if necessary
  if (mem_offset_ > mem_top_offset_) {
    mem_top_offset_ = mem_offset_;
  }
  return aligned_mem_offset;
}

void MemoryMgr::free(uint32_t addr) {
  if (scratch_buf_.empty()) {
    panic("Cannot free address %x", addr);
  }
  // reverse traverse vector, allocations probably happen in reverse order
  for (auto it = scratch_buf_.rbegin(); it != scratch_buf_.rend(); it++) {
    auto size = static_cast<uint32_t>(it->size);
    if (it->addr == addr) {
      // remove allocation. Note that erase takes a normal iterator, so we get
      // to the base iterator and point to the previous element.
      scratch_buf_.erase(it.base() - 1);
      // if this is the last item, then update the mem_offset_ by checking the
      // "highest" allocation in the list. This is necessary if the free had
      // been done in "disorder".
      if (addr + size == mem_offset_) {
        // update mem_offset to the highest allocation
        uint32_t highest_allocation = mem_base_offset_;
        for (const auto& block : scratch_buf_) {
          auto block_upper_limit =
              block.addr + static_cast<uint32_t>(block.size);
          if (block_upper_limit > highest_allocation) {
            highest_allocation = block_upper_limit;
          }
        }
        mem_offset_ = highest_allocation;
      }
      return;
    }
  }
  panic("Address %x not found: cannot free", addr);
}

void MemoryMgr::reset() {
  scratch_buf_.clear();
  mem_offset_ = mem_base_offset_;
}

}  // namespace akida
