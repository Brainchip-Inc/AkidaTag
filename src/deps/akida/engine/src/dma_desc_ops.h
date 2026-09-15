#pragma once
#include <cstdint>
#include <vector>
#include "dma_engine.h"
#include "memory_mgr.h"

namespace akida {
namespace dma {

using Descriptor = std::vector<uint32_t>;

Descriptor format_config_desc(bool direction, uint32_t input_addr,
                              uint32_t output_addr, uint32_t buf_sz);
// constants used for config formatting
inline constexpr bool kDescConfigDirectionWrite = true;
inline constexpr bool kDescConfigDirectionRead = false;

Descriptor format_event_desc(uint32_t job_id, uint32_t input_addr,
                             uint32_t output_addr, uint32_t buf_sz,
                             uint32_t learning_class = 0);

Descriptor format_hrc_desc(uint32_t job_id, uint32_t input_addr,
                           uint32_t output_addr, uint32_t row_bytesize,
                           uint32_t height, uint32_t next_row_offset,
                           uint32_t window_row_bytesize, uint32_t window_height,
                           uint32_t overlap_bytesize, uint32_t y_offset,
                           uint32_t x_offset, uint32_t learning_class = 0);

// Max number of events passed to the DMA engine
uint32_t max_dma_events();

void alloc_dma_descriptors(dma::Engine* dma, MemoryMgr* mem_mgr,
                           uint32_t num_descriptors);

}  // namespace dma
}  // namespace akida
