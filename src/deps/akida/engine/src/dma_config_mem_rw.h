#pragma once

#include <cstdint>

#include "dma_engine.h"
#include "engine/dma.h"
#include "external_mem_mgr.h"

namespace akida::dma {
void dma_config_write(const dma::w32* buffer, size_t buf_size,
                      const dma::Config& dma_config,
                      ExternalMemoryMgr* external_mem, HardwareDriver* driver,
                      bool wait_for_completion);

void dma_config_read(dma::w32* buffer, uint32_t nb_words,
                     const uint32_t* header, const dma::Config& dma_config,
                     MemoryMgr* mem_mgr, HardwareDriver* driver,
                     bool wait_for_completion);
}  // namespace akida::dma