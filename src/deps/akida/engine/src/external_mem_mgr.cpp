#include "external_mem_mgr.h"

#include <cstdint>

#include "infra/system.h"

#include "memory_mgr.h"
#include "memory_utils.h"

namespace akida {

dma::addr ExternalMemoryMgr::track_and_put_on_device_if_required(
    AllocId id, size_t byte_size) {
  // prevent allocating if ledger already contains an entry
  if (alloc_ledger_.find(id) != alloc_ledger_.end()) {
    panic("Tracked allocation ID %p already taken", id);
  }

  // get address
  dma::addr addr;

  // alloc memory if we need to, and copy data to it
  if (accessible_from_akida(id, *driver_)) {
    // we can safely cast because dma::addr and AllocId types have the same
    // size
    addr = to_dma_addr(id);
  } else {
    addr = mem_mgr_->alloc(byte_size);
    driver_->write(addr, id, byte_size);
  }

  // record in ledger
  alloc_ledger_[id] = addr;
  return addr;
}

void ExternalMemoryMgr::release(AllocId id) {
  auto addr = tracked(id);
  if (!accessible_from_akida(id, *driver_)) {
    mem_mgr_->free(addr);
  }
  alloc_ledger_.erase(id);
}

uint32_t ExternalMemoryMgr::tracked(AllocId id) const {
  auto entry = alloc_ledger_.find(id);
  // check if item is not in ledger
  if (entry == alloc_ledger_.end()) {
    panic("Tracked allocation ID %p not found", id);
  }
  auto& addr = entry->second;
  return addr;
}

void ExternalMemoryMgr::reset() {
  // free all elements, in reverse order
  for (auto iter = alloc_ledger_.rbegin(); iter != alloc_ledger_.rend();
       ++iter) {
    // free only memory that has been allocated by us
    if (!accessible_from_akida(iter->first, *driver_)) {
      mem_mgr_->free(iter->second);
    }
  }
  // clear up map
  alloc_ledger_.clear();
  // clear extra memory
  clear_extra_mem();
}

}  // namespace akida
