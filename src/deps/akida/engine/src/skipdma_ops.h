#pragma once
#include <cstdint>
#include "akida/np.h"
#include "dma_engine.h"
#include "external_mem_mgr.h"
#include "memory_mgr.h"

namespace akida::skipdma {
// allocate memory for descriptors
void program_store_channel_desc_buff_addr(
    dma::addr skipdma_descriptor_base_addr, const dma::Config& dma_config,
    const ProgramInfo::SkipDmaInfoTrack& skipdma,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count);
void program_load_channel_desc_buff_addr(
    dma::addr skipdma_descriptor_base_addr, const dma::Config& dma_config,
    const ProgramInfo::SkipDmaInfoTrack& skipdma,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count);
// allocate memory for outbound
dma::addr program_store_channel_ob_buff_addr(
    const uint8_t max_outbound, const dma::Config& dma_config,
    const ProgramInfo::SkipDmaInfoTrack& skipdma, MemoryMgr* mem_mgr,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count);
// allocate memory for outbound
void program_store_channel_rst_ctrl(
    const dma::Config& dma_config, const ProgramInfo::SkipDmaInfoTrack& skipdma,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count);
// Enable OB buffer clear mode
void store_channel_enable_pld_clr(const dma::Config& dma_config,
                                  const ProgramInfo::SkipDmaInfoTrack& skipdma,
                                  ExternalMemoryMgr* ext_mem_mgr,
                                  HardwareDriver* driver, bool is_single_pass,
                                  uint32_t* desc_count);
// program descriptors container size
uint8_t program_store_channel_cont_size(
    const dma::Config& dma_config, const ProgramInfo::SkipDmaInfoTrack& skipdma,
    MemoryMgr* mem_mgr, ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver,
    const bool is_pipeline, const size_t batch_size, bool is_single_pass,
    uint32_t* desc_count);
void program_load_channel_cont_size(
    const dma::Config& dma_config, const ProgramInfo::SkipDmaInfoTrack& skipdma,
    MemoryMgr* mem_mgr, ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver,
    const bool is_pipeline, const size_t batch_size, bool is_single_pass,
    uint32_t* desc_count);

// skip dma external memory programming
uint32_t program_ext_mem(const ProgramInfo& current_program_info,
                         const dma::Config& dma_config, MemoryMgr* mem_mgr,
                         ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver,
                         size_t effective_batch_size, bool is_pipeline,
                         uint32_t pass_idx, bool is_single_pass,
                         std::map<uint8_t, dma::Skip>* skipdma_ext_mem_ptr);
}  // namespace akida::skipdma
