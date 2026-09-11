#include "skipdma_ops.h"

#include <cstddef>

#include "dma_config_mem_rw.h"
#include "dma_desc_format.h"
#include "dma_desc_ops.h"
#include "engine/dma_config_ops.h"
#include "engine/registers_skipdma.h"
#include "memory_utils.h"
#include "registers_dma_engine.h"

namespace akida::skipdma {

static dma::w32 read_reg(const hw::Ident& skipmda_id, const uint16_t dest_addr,
                         const dma::Config& dma_config, MemoryMgr* mem_mgr,
                         HardwareDriver* driver, bool is_single_pass,
                         uint32_t* desc_count) {
  // read the size of ob (config dma data file format)
  dma::w32 header[dma::kConfigNpHeaderWordSize]{0};
  dma::format_config_header(&header[0], skipmda_id,
                            dma::Target::SkipDmaRegisters, 1, dest_addr, false);
  // returned value
  dma::w32 reg{};
  // read register
  dma::dma_config_read(&reg, 1, &header[0], dma_config, mem_mgr, driver,
                       is_single_pass);
  if (!is_single_pass) {
    (*desc_count)++;
  }
  return reg;
}

static void write_reg(const dma::w32 value, const hw::Ident& skipmda_id,
                      const uint16_t dest_addr, const dma::Config& dma_config,
                      ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver,
                      bool is_single_pass, uint32_t* desc_count) {
  // config dma configuration data file
  constexpr uint8_t payload_size = 1U;
  constexpr uint8_t buff_size = dma::kConfigNpHeaderWordSize + payload_size;
  auto* buffer = ext_mem_mgr->alloc_extra_mem(buff_size);
  auto& header = *buffer;
  auto& payload = *(buffer + buff_size - 1);
  payload = value;
  dma::format_config_header(&header, skipmda_id, dma::Target::SkipDmaRegisters,
                            payload_size, dest_addr, false);
  dma::dma_config_write(buffer, buff_size, dma_config, ext_mem_mgr, driver,
                        is_single_pass);
  if (is_single_pass) {
    ext_mem_mgr->release_extra_mem(buff_size);
  } else {
    (*desc_count)++;
  }
}

// This function compute the external memory buffer size giving the batch_size
// and the skip connection length
static uint8_t compute_cont_size(const bool used_for_tnp_b,
                                 const bool is_pipeline,
                                 const size_t batch_size,
                                 const uint8_t skip_length) {
  if (used_for_tnp_b) {
    return skip_length;
  }
  if (is_pipeline) {
    return static_cast<uint8_t>(
        std::min(static_cast<size_t>(skip_length + 3), batch_size + 1));
  }
  return 2;
}

uint8_t program_store_channel_cont_size(
    const dma::Config& dma_config, const ProgramInfo::SkipDmaInfoTrack& skipdma,
    MemoryMgr* mem_mgr, ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver,
    const bool is_pipeline, const size_t batch_size, bool is_single_pass,
    uint32_t* desc_count) {
  const auto id = skipdma.info.ident;
  const auto channel_idx = skipdma.info.ident.channel_idx;
  // write Replay Maximum Outbound (store channel)
  // This value is set according to the documentation
  // Akida_skipdma_spec_v3p1.docx.
  const auto max_outbound = compute_cont_size(
      skipdma.used_for_tnp_b, is_pipeline, batch_size, skipdma.skip_length);
  if (is_pipeline) {
    auto dest_addr = static_cast<uint16_t>(
        skipdma::REPLAY_MAX_OB_DESC_BUFF_REG(channel_idx));
    auto reg = read_reg(id, dest_addr, dma_config, mem_mgr, driver,
                        is_single_pass, desc_count);
    set_field(&reg, skipdma::MAX_OB_DESC_BUFF, max_outbound);
    write_reg(reg, id, dest_addr, dma_config, ext_mem_mgr, driver,
              is_single_pass, desc_count);
  }
  return max_outbound;
}

void program_load_channel_cont_size(
    const dma::Config& dma_config, const ProgramInfo::SkipDmaInfoTrack& skipdma,
    MemoryMgr* mem_mgr, ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver,
    const bool is_pipeline, const size_t batch_size, bool is_single_pass,
    uint32_t* desc_count) {
  const auto id = skipdma.info.ident;
  const auto channel_idx = skipdma.info.ident.channel_idx;
  // write Replay Maximum Outbound (store channel)
  // This value is set according to the documentation
  // Akida_skipdma_spec_v3p1.docx.
  const auto max_outbound = compute_cont_size(
      skipdma.used_for_tnp_b, is_pipeline, batch_size, skipdma.skip_length);
  if (is_pipeline) {
    // write Container Size Register (load channel)
    const auto dest_addr =
        static_cast<uint16_t>(skipdma::CONT_SIZE_REG(channel_idx));
    auto reg = read_reg(id, dest_addr, dma_config, mem_mgr, driver,
                        is_single_pass, desc_count);
    set_field(&reg, skipdma::MAX_DESC_CONT, max_outbound - 1);
    write_reg(reg, id, dest_addr, dma_config, ext_mem_mgr, driver,
              is_single_pass, desc_count);
  }
}

void program_store_channel_desc_buff_addr(
    dma::addr skipdma_descriptor_base_addr, const dma::Config& dma_config,
    const ProgramInfo::SkipDmaInfoTrack& skipdma,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count) {
  const auto id = skipdma.info.ident;
  const auto channel_idx = skipdma.info.ident.channel_idx;
  // Replay OB Event Buffer Address Register (store channel)
  auto dest_addr =
      static_cast<uint16_t>(skipdma::REPLAY_DESC_BUFF_ADDR_REG(channel_idx));
  write_reg(skipdma_descriptor_base_addr, id, dest_addr, dma_config,
            ext_mem_mgr, driver, is_single_pass, desc_count);
}

void program_load_channel_desc_buff_addr(
    dma::addr skipdma_descriptor_base_addr, const dma::Config& dma_config,
    const ProgramInfo::SkipDmaInfoTrack& skipdma,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count) {
  const auto id = skipdma.info.ident;
  const auto channel_idx = skipdma.info.ident.channel_idx;
  // Container Address Register (load channel)
  const auto dest_addr =
      static_cast<uint16_t>(skipdma::CONT_ADDR_REG(channel_idx));
  write_reg(skipdma_descriptor_base_addr, id, dest_addr, dma_config,
            ext_mem_mgr, driver, is_single_pass, desc_count);
}

dma::addr program_store_channel_ob_buff_addr(
    const uint8_t max_outbound, const dma::Config& dma_config,
    const ProgramInfo::SkipDmaInfoTrack& skipdma, MemoryMgr* mem_mgr,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count) {
  // read the size of ob (config dma data file format)
  const auto id = skipdma.info.ident;
  const auto channel_idx = skipdma.info.ident.channel_idx;
  //  set ob byte size. In register the size is in 32-bit, it should be multiply
  //  by 4, to get the size in 8-bit
  constexpr uint32_t header_size = 8;
  const auto ob_size = (skipdma.ob_32b_size + header_size) * 4;
  size_t ob_mem_size = static_cast<size_t>(ob_size) * max_outbound;
  auto ob_mem = mem_mgr->alloc(ob_mem_size);
  // now write ob memory address in skip dma register (store channel)
  const auto dest_addr = static_cast<uint16_t>(
      skipdma::REPLAY_OB_EVENT_BUFF_ADDR_REG(channel_idx));
  write_reg(ob_mem, id, dest_addr, dma_config, ext_mem_mgr, driver,
            is_single_pass, desc_count);
  return ob_mem;
}

void program_store_channel_rst_ctrl(
    const dma::Config& dma_config, const ProgramInfo::SkipDmaInfoTrack& skipdma,
    ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver, bool is_single_pass,
    uint32_t* desc_count) {
  // read the size of ob (config dma data file format)
  const auto id = skipdma.info.ident;
  const auto channel_idx = skipdma.info.ident.channel_idx;
  // Reset skipDMA core logic
  const auto dest_addr =
      static_cast<uint16_t>(skipdma::DMA_RST_CTRL_REG(channel_idx));
  uint32_t reg{};
  set_field(&reg, skipdma::DMA_OB_RST, 0x1);
  write_reg(reg, id, dest_addr, dma_config, ext_mem_mgr, driver, is_single_pass,
            desc_count);
}

void store_channel_enable_pld_clr(const dma::Config& dma_config,
                                  const ProgramInfo::SkipDmaInfoTrack& skipdma,
                                  ExternalMemoryMgr* ext_mem_mgr,
                                  HardwareDriver* driver, bool is_single_pass,
                                  uint32_t* desc_count) {
  // read ob pld clear size register
  const auto id = skipdma.info.ident;
  const auto channel_idx = skipdma.info.ident.channel_idx;
  auto dest_addr =
      static_cast<uint16_t>(skipdma::DMA_OB_PLD_CLR_REG(channel_idx));
  dma::w32 reg{};
  // enable pld clear
  set_field(&reg, skipdma::OB_PLD_CLR_SIZE, skipdma.ob_32b_size);
  set_field(&reg, skipdma::OB_PLD_CLR_EN, 1);
  write_reg(reg, id, dest_addr, dma_config, ext_mem_mgr, driver, is_single_pass,
            desc_count);
}

// skip dma external memory programming
uint32_t program_ext_mem(const ProgramInfo& current_program_info,
                         const dma::Config& dma_config, MemoryMgr* mem_mgr,
                         ExternalMemoryMgr* ext_mem_mgr, HardwareDriver* driver,
                         size_t effective_batch_size, bool is_pipeline,
                         uint32_t pass_idx, bool is_single_pass,
                         std::map<uint8_t, dma::Skip>* skipdma_ext_mem_ptr) {
  uint32_t desc_count = 0;
  auto& skipdma_ext_mem = *skipdma_ext_mem_ptr;
  // get skipdmas to program
  const auto skipdma_store_tracks =
      current_program_info.skipdma_store_track(pass_idx);
  const auto skipdma_load_tracks =
      current_program_info.skipdma_load_track(pass_idx);
  // First program skip dma store channels
  for (const auto& store_track : skipdma_store_tracks) {
    //------------- allocate skipdma desc buffer memory
    const uint8_t max_outbound = skipdma::program_store_channel_cont_size(
        dma_config, store_track, mem_mgr, ext_mem_mgr, driver, is_pipeline,
        effective_batch_size, is_single_pass, &desc_count);
    // memory for descriptor must be 16B aligned
    auto skipdma_mem_info = dma::Skip{
        dma::Engine(skip_dma_reg_base(driver->top_level_reg()),
                    dma::skip::SKIPDMA_DESC_CONT_SIZE, dma::kSkipDmaAlignment)};
    alloc_dma_descriptors(&skipdma_mem_info.engine, mem_mgr, max_outbound);
    // write descriptor Address (configuration data file)
    // NB: should be done before PLD_CLR_EN
    skipdma::program_store_channel_desc_buff_addr(
        skipdma_mem_info.engine.descriptor_base_addr, dma_config, store_track,
        ext_mem_mgr, driver, is_single_pass, &desc_count);
    //------------- allocate external memory for OB event
    free_allocated_buffer(mem_mgr, &skipdma_mem_info.outputs_base_address);
    skipdma_mem_info.outputs_base_address =
        skipdma::program_store_channel_ob_buff_addr(
            max_outbound, dma_config, store_track, mem_mgr, ext_mem_mgr, driver,
            is_single_pass, &desc_count);
    //------------- Reset Skip DMA Control
    program_store_channel_rst_ctrl(dma_config, store_track, ext_mem_mgr, driver,
                                   is_single_pass, &desc_count);
    //------------- enable PLD clear
    skipdma::store_channel_enable_pld_clr(dma_config, store_track, ext_mem_mgr,
                                          driver, is_single_pass, &desc_count);
    bool has_been_inserted =
        skipdma_ext_mem.insert({store_track.skip_connect_id, skipdma_mem_info})
            .second;
    if (!has_been_inserted) {
      panic("Cannot insert, 2 tracks have the same skip connect id.");
    }
  }
  // secondly program skip dma load channels
  for (const auto& load_track : skipdma_load_tracks) {
    //------------- allocate skipdma desc buffer memory
    skipdma::program_load_channel_cont_size(
        dma_config, load_track, mem_mgr, ext_mem_mgr, driver, is_pipeline,
        effective_batch_size, is_single_pass, &desc_count);
    // Retrieve external memory corresponding to the skip DMA store engine. If
    // the memory is not allocated raise an exception.
    if (skipdma_ext_mem.find(load_track.skip_connect_id) !=
        skipdma_ext_mem.end()) {
      const auto& skipdma_mem_info =
          skipdma_ext_mem.at(load_track.skip_connect_id);
      skipdma::program_load_channel_desc_buff_addr(
          skipdma_mem_info.engine.descriptor_base_addr, dma_config, load_track,
          ext_mem_mgr, driver, is_single_pass, &desc_count);
    } else {
      panic(
          "An error occured when programming skip DMA load engine: col %, row "
          "%, id %, channel %",
          load_track.info.ident.col, load_track.info.ident.row,
          load_track.info.ident.id, load_track.info.ident.channel_idx);
    }
  }
  return desc_count;
}
}  // namespace akida::skipdma
