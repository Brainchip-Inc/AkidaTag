#pragma once
#include <cstddef>
#include <map>

#include "infra/hardware_driver.h"

#include "memory_mgr.h"

namespace akida {

class ExternalMemoryMgr {
 public:
  explicit ExternalMemoryMgr(MemoryMgr* mgr, HardwareDriver* driver)
      : mem_mgr_(mgr), driver_(driver) {}

  using AllocId = const void*;
  // Track a local address that will also be on the device.
  // It copies data on device if they are not accessible from akida (if they are
  // not in HardwareDriver akida_visible_memory range)
  dma::addr track_and_put_on_device_if_required(AllocId id, size_t byte_size);

  // release (untrack) data from device (if data was copied on it, memory is
  // freed)
  void release(AllocId id);

  // get on device address from id
  dma::addr tracked(AllocId id) const;

  // Untrack all memory, freeing it if they were copied on device, to restore
  // initial state
  void reset();

  // alloc extra memory
  dma::w32* alloc_extra_mem(uint8_t size) {
    if (extra_mem_.capacity() < extra_mem_capacity_) {
      extra_mem_.reserve(extra_mem_capacity_);
    }
    if (size == 0 || extra_mem_.size() + size > extra_mem_capacity_) {
      panic("Can't allocate %p bytes from external memory",
            size * sizeof(dma::w32));
    }
    extra_mem_.resize(extra_mem_.size() + size);
    return &extra_mem_[extra_mem_.size() - size];
  }

  // release extra memory
  void release_extra_mem(uint8_t size) {
    if (size > extra_mem_.size()) {
      panic("Can't release %p bytes from external memory",
            size * sizeof(dma::w32));
    }
    extra_mem_.resize(extra_mem_.size() - size);
  }

  // clear extra memory
  void clear_extra_mem() { extra_mem_.clear(); }

 private:
  // memory manager
  MemoryMgr* mem_mgr_;
  // hardware driver
  HardwareDriver* driver_;
  // allocation ledger, a map of id:addresss
  std::map<AllocId, uint32_t> alloc_ledger_;
  // TODO: This is a temporary solution to track dma config data files which are
  // created in the engine. It has been added because dma config files of skip
  // dma external memory are created in engines, so it needs to be tracked. But
  // for the final solution, these config files will be created directly in the
  // program, with default address set to 0, then the engine will update this
  // value. Remove that when the final solution is implemented.
  //  Extra memory (used to track external data)
  std::vector<dma::w32> extra_mem_{};
  // Enough capacity to track extra DMA config files of 30 skip connections
  static constexpr auto extra_mem_capacity_ =
      (dma::kSkipDmaStoreNbExtraDescriptors +
       dma::kSkipDmaLoadNbExtraDescriptors) *
      3 /*32-bit words per config DMA file*/ *
      30 /*number of skip connection.*/;
};

}  // namespace akida
