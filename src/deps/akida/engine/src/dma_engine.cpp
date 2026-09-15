#include "dma_engine.h"
#include "dma_engine_ops.h"

#include <cassert>

#include "engine/dma.h"
#include "infra/hardware_driver.h"
#include "infra/int_ops.h"
#include "infra/registers_common.h"
#include "infra/system.h"

#include "dma_desc_ops.h"
#include "registers_dma_engine.h"

namespace akida {
namespace dma {

static void mask_interrupts(HardwareDriver* driver, uint32_t reg_base_addr,
                            bool is_multi_pass);

Engine::Engine(uint32_t reg_base, uint32_t desc_bytes_size, uint32_t alignment)
    : descriptor_base_addr(0),
      descriptor_bytes_size(desc_bytes_size),
      reg_base_addr(reg_base),
      addr_alignment(alignment) {
  assert(descriptor_bytes_size % 32 == 0 &&
         "descriptor_bytes_size should be multiple of 32 (HW unit is 32 "
         "bytes/256 bits)");
}

static void reset_engine(HardwareDriver* driver, uint32_t reg_base_addr) {
  uint32_t reg = 0;
  // perform a soft reset
  set_field(&reg, DMA_CTRL_SOFT_RESET, 1);
  driver->write32(reg_base_addr + DMA_CTRL_REG, reg);
}

void configure_descriptors_buffer(HardwareDriver* driver, const Engine& dma,
                                  uint32_t num_descriptors) {
  assert(dma.descriptor_base_addr != 0);

  // update base address register
  driver->write32(dma.reg_base_addr + DMA_CONT_ADDR_REG,
                  dma.descriptor_base_addr);

  // update container size register
  uint32_t reg = 0;
  // Set container size (constant in 32 bytes (256 bits) unit)
  set_field(&reg, DMA_DESC_CONT_SIZE, dma.descriptor_bytes_size / 32);
  // Set maximum number of descriptors. Note this corresponds to the maximum
  // value the descriptor index can take, so that will go from 0 to
  // num_descriptors -1.
  set_field(&reg, DMA_MAX_DESC_CONTS, num_descriptors - 1);
  driver->write32(dma.reg_base_addr + DMA_CONT_SIZE_REG, reg);
}

static bool wait_for_interrupt_ext(HardwareDriver* driver, const Engine& dma,
                                   const RegDetail& flag) {
  // This is a manual polling with a timeout of few ms
  constexpr int64_t kTimeout = 5000;
  auto start = time_ms();

  // Busy loop until timeout is reached or interrupt is generated.
  while (true) {
    // check outbound interrupt
    if (check_for_interrupt(driver, dma, flag)) {
      // interrupt received, clear status and interrupt
      clear_interrupts(driver, dma);
      return true;
    }

    auto end = time_ms();
    if ((end - start) > kTimeout) {
      return false;
    }
  }
}

static void init_config_dma_multipass(HardwareDriver* driver, const Config& dma,
                                      const ProgramInfo& program_info) {
  const uint32_t& reg_base_addr = dma.engine.reg_base_addr;
  const uint32_t num_descriptors =
      program_info.number_of_program_descriptors_required();
  const uint32_t num_extra_descs =
      program_info.number_of_extra_program_descriptors_required();
  const uint32_t total_num_descs = num_descriptors + num_extra_descs;

  // Set extra descriptors
  uint32_t reg = 0;
  set_field(&reg, DMA_LAST_EXTRA_DESCRIPTOR, total_num_descs - 1);
  // at init, learn is disabled so extra descriptor is disabled too
  set_field(&reg, DMA_EXTRA_DESC_ENABLE, 0);
  driver->write32(reg_base_addr + DMA_EXTRA_DESC_CTRL_REG, reg);

  // Set outbound container size to 2 (as inbound)
  reg = 0;
  set_field(&reg, DMA_REPLAY_MAX_OB_DESC_BUFFERS, 2);
  driver->write32(reg_base_addr + DMA_REPLAY_MAX_OB_BUFFERS_REG, reg);
}

static void configure_output_header(HardwareDriver* driver,
                                    const dma::addr reg_base_addr,
                                    bool enable) {
  const auto reg_addr = reg_base_addr + DMA_CTRL_REG;
  uint32_t reg = driver->read32(reg_addr);
  set_field(&reg, DMA_CTRL_WR_INFO_EN, enable ? 1 : 0);
  set_field(&reg, DMA_CTRL_WR_INFO_HDR, 1);
  constexpr uint32_t header_byte_size = 32;
  set_field(&reg, DMA_CTRL_WR_INFO_HDR_SZ, header_byte_size);
  driver->write32(reg_addr, reg);
}

void init_config_dma(HardwareDriver* driver, const Config& dma,
                     const ProgramInfo& program_info) {
  // soft reset engine
  reset_engine(driver, dma.engine.reg_base_addr);

  // set descriptors buffer & number of descriptors
  configure_descriptors_buffer(
      driver, dma.engine,
      program_info.number_of_program_descriptors_required());

  const bool is_multipass = program_info.number_of_passes() > 1;

  // mask interrupts
  mask_interrupts(driver, dma.engine.reg_base_addr, is_multipass);

  // Enable/disable replay mode by setting max desc burst mode
  uint32_t reg = 0;
  set_field(&reg, DMA_REPLAY_MAX_DESC_BURST_MODE, is_multipass ? 1 : 0);
  driver->write32(dma.engine.reg_base_addr + DMA_REPLAY_BUF_CTRL_REG, reg);

  // Set number of descriptors per burst
  reg = 0;
  set_field(&reg, DMA_REPLAY_MAX_DESC_BURST_VALUE,
            is_multipass ? program_info.number_of_descriptors_per_pass() : 0);
  driver->write32(dma.engine.reg_base_addr + DMA_REPLAY_BURST_VAL_REG, reg);

  // Set delay start
  reg = 0;
  set_field(&reg, DMA_DESC_START_DELAY, is_multipass ? 0x60 : 0);
  driver->write32(dma.engine.reg_base_addr + DMA_DESC_START_DELAYS_REG, reg);

  // Disable output header
  configure_output_header(driver, dma.engine.reg_base_addr, false);

  // enable clock count for DMA config
  toggle_buffer_timer(driver, dma.engine, true);

  if (is_multipass) {
    // in multipass we have extra configuration to set
    init_config_dma_multipass(driver, dma, program_info);
  }
}

void toggle_engine(HardwareDriver* driver, uint32_t reg_base_addr,
                   bool enabled) {
  auto reg = driver->read32(reg_base_addr + DMA_CTRL_REG);
  // Set control register to run
  set_field(&reg, DMA_CTRL_RUN, enabled ? 1 : 0);
  set_field(&reg, DMA_CTRL_INT_EN, 1);
  driver->write32(reg_base_addr + DMA_CTRL_REG, reg);
}

void set_output_buffer_clear(HardwareDriver* driver, const Inputs& dma,
                             uint32_t clear_size) {
  const uint32_t& reg_base_addr = dma.engine.reg_base_addr;
  // Configure the output buffer clearing size
  dma::w32 reg = 0;
  // The output buffer clear size is expressed in 32-bit words
  set_field(&reg, DMA_OB_PLD_CLR_SIZE, div_round_up(clear_size, 4));
  set_field(&reg, DMA_OB_PLD_CLR_EN, clear_size > 0 ? 1 : 0);
  driver->write32(reg_base_addr + DMA_OB_PLD_CLEAR_SIZE_REG, reg);
}

void prepare_engine_multi_pass(HardwareDriver* driver, const Inputs& dma,
                               dma::addr hw_desc_addr,
                               dma::addr hw_payload_addr, uint32_t num_loops) {
  const uint32_t& reg_base_addr = dma.engine.reg_base_addr;
  // Leave the controller disabled before setting the registers.
  toggle_engine(driver, reg_base_addr, false);

  // mask interrupts
  mask_interrupts(driver, reg_base_addr, true);

  // Enable replay mode by setting hw generated descriptors mode, dynamic size,
  // set replay timer
  // TODO: consider disabling this as small power enhancement.
  uint32_t reg = 0;
  set_field(&reg, DMA_REPLAY_MAX_DESC_BURST_MODE, 1);
  set_field(&reg, DMA_REPLAY_HW_OB_ADDR_GEN_MODE, 1);
  set_field(&reg, DMA_REPLAY_HW_OB_ADDR_DYN_MODE, 1);
  set_field(&reg, DMA_REPLAY_BUFFER_MODE, 1);
  set_field(&reg, DMA_REPLAY_TIMER_MODE, 0);
  driver->write32(reg_base_addr + DMA_REPLAY_BUF_CTRL_REG, reg);

  // Set number of loops
  reg = 0;
  set_field(&reg, DMA_REPLAY_LOOPS, num_loops);
  // DMA_REPLAY_LOOPS_LAYER_PR is set with same value as num_loops, used for
  // layer partial reconfig but mandatory to make it work nevertheless.
  set_field(&reg, DMA_REPLAY_LOOPS_LAYER_PR, num_loops);
  driver->write32(reg_base_addr + DMA_REPLAY_BURST_VAL_REG, reg);

  // Address of the main buffer space for the HW generated Descriptors, (only
  // one is going to be used)
  uint32_t addr_main_desc = hw_desc_addr;
  // scratch descriptors container can be set at the same address than main desc
  uint32_t addr_scratch_desc = addr_main_desc;
  // scratch payload base address has 1 as size, it only contains header (i.e.
  // size) because DMA controller does not know that partial reconfiguration is
  // using internal buffer.
  uint32_t addr_scratch_payload_base_addr = hw_payload_addr;
  driver->write32(reg_base_addr + DMA_REPLAY_DESC_MAIN_BUF_ADDR_REG,
                  addr_main_desc);
  driver->write32(reg_base_addr + DMA_REPLAY_DESC_SCRATCH_BUF_ADDR_REG,
                  addr_scratch_desc);
  driver->write32(reg_base_addr + DMA_REPLAY_OB_EVENT_SCRATCH_ADDR_REG,
                  addr_scratch_payload_base_addr);
}

dma::addr enqueue_descriptor(HardwareDriver* driver, const Engine& dma,
                             const dma::Descriptor& descriptor) {
  // get number of descriptors
  auto reg = driver->read32(dma.reg_base_addr + DMA_CONT_SIZE_REG);
  const auto num_descriptors = get_field(reg, DMA_MAX_DESC_CONTS);

  // get last descriptor id
  const auto desc_container_reg_addr = dma.reg_base_addr + DMA_DESC_CONT_REG;
  reg = driver->read32(desc_container_reg_addr);
  auto last_descriptor_id = get_field(reg, DMA_LAST_DESC_CONT);

  // increment last_descriptor_id and make it loop between [0; num_descriptors]
  // we cannot use modulo operator because after reset the field will 0xFF, so
  // the next value should be 0 but 0xFF modulo num_descriptors could lead to
  // non zero value
  ++last_descriptor_id;
  if (last_descriptor_id > num_descriptors) {
    last_descriptor_id = 0;
  }
  // calculate the address where we have to write the descriptor
  auto last_descriptor_addr = dma.descriptor_base_addr +
                              (dma.descriptor_bytes_size * last_descriptor_id);
  // copy descriptor in scratch buffer
  driver->write(last_descriptor_addr, descriptor.data(),
                descriptor.size() * sizeof(Descriptor::value_type));

  // then write the incremented value in field DMA_LAST_DESC_CONT.
  // DMA will then process descriptors from DMA_DESC_CONT_REG to
  // DMA_LAST_DESC_CONT
  set_field(&reg, DMA_LAST_DESC_CONT, last_descriptor_id);
  driver->write32(desc_container_reg_addr, reg);

  return last_descriptor_addr;
}

void process(HardwareDriver* driver, const Config& dma,
             const dma::Descriptor& descriptor, bool wait_for_completion) {
  // enqueue descriptor
  enqueue_descriptor(driver, dma.engine, descriptor);
  if (wait_for_completion) {
    wait_config_dma_descriptor_complete(driver, dma);
  }
}

uint16_t get_last_job_id_processed(HardwareDriver* driver, const Inputs& dma) {
  auto reg = driver->read32(dma.engine.reg_base_addr + DMA_DESC_STATUS_REG);
  // JOB_ID is 16 bits, so we can cast to uint16_t
  static_assert(DMA_JOB_ID.nb_bits == sizeof(uint16_t) * 8,
                "DMA_JOB_ID field should be 16 bits");
  return static_cast<uint16_t>(get_field(reg, DMA_JOB_ID));
}

void toggle_buffer_timer(HardwareDriver* driver, const Engine& dma,
                         bool enabled) {
  auto reg = driver->read32(dma.reg_base_addr + DMA_IB_BUF_MON_CTRL_REG);
  set_field(&reg, DMA_BUF_TIMER_EN, enabled ? 0x1 : 0x0);
  driver->write32(dma.reg_base_addr + DMA_IB_BUF_MON_CTRL_REG, reg);
}

uint32_t read_buffer_timer(HardwareDriver* driver, const Engine& dma) {
  return driver->read32(dma.reg_base_addr + DMA_BUFFER_TIMER_VALUE_REG);
}

bool is_buffer_timer_enabled(const HardwareDriver& driver, const Inputs& dma) {
  auto reg = driver.read32(dma.engine.reg_base_addr + DMA_IB_BUF_MON_CTRL_REG);
  auto enabled = get_field(reg, DMA_BUF_TIMER_EN);
  return enabled != 0;
}

void toggle_pipeline(HardwareDriver* driver, const Inputs& dma, bool enabled) {
  auto reg = driver->read32(dma.engine.reg_base_addr + DMA_IB_BUF_MON_CTRL_REG);
  set_field(&reg, DMA_BUF_END_SELECT, enabled ? 1 : 0);
  // Note: The settings supported are:
  // 0x0: DMA Outbound Buffer End -> No pipelining
  // 0x1: DMA Inbound Buffer End -> Full pipelining
  // 0x2: External Buffer End (from other DMA) -> Used only for debugging HRC:
  // No HRC pipeling, Mesh pipelining
  // Value 0x2 should probably not be used.

  driver->write32(dma.engine.reg_base_addr + DMA_IB_BUF_MON_CTRL_REG, reg);
}

static void mask_interrupts(HardwareDriver* driver, uint32_t reg_base_addr,
                            bool is_multipass) {
  auto reg = driver->read32(reg_base_addr + DMA_IB_BUF_MON_CTRL_REG);

  // Mask all interrupts except OB when single pass, or Descriptor Burst when
  // multipass
  set_field(&reg, DMA_BUFFER_END_MASK_OB_END, is_multipass ? 1 : 0);
  set_field(&reg, DMA_BUFFER_END_MASK_IB_END, 1);
  set_field(&reg, DMA_BUFFER_END_MASK_EXT_DMA_END, 1);
  set_field(&reg, DMA_BUFFER_END_MASK_DESC_BURST_END, is_multipass ? 0 : 1);

  driver->write32(reg_base_addr + DMA_IB_BUF_MON_CTRL_REG, reg);
}

bool check_for_interrupt(HardwareDriver* driver, const Engine& dma,
                         const RegDetail& flag) {
  uint32_t reg = driver->read32(dma.reg_base_addr + DMA_BUF_MON_STATUS_REG);
  return get_field(reg, flag) != 0;
}

void clear_interrupts(HardwareDriver* driver, const Engine& dma) {
  auto reg = driver->read32(dma.reg_base_addr + DMA_IB_BUF_MON_CTRL_REG);
  set_field(&reg, DMA_STATUS_CLEAR, 1);
  driver->write32(dma.reg_base_addr + DMA_IB_BUF_MON_CTRL_REG, reg);
}

void toggle_extra_descriptors(HardwareDriver* driver, const Config& dma,
                              bool enable) {
  const auto reg_addr = dma.engine.reg_base_addr + DMA_EXTRA_DESC_CTRL_REG;
  auto reg = driver->read32(reg_addr);
  set_field(&reg, DMA_EXTRA_DESC_ENABLE, enable ? 1 : 0);
  driver->write32(reg_addr, reg);
}

void init_default_dma(HardwareDriver* driver, const Engine& dma,
                      uint32_t number_of_descriptors, bool output_header) {
  // soft reset
  reset_engine(driver, dma.reg_base_addr);

  // set descriptor base address & number of descriptors
  configure_descriptors_buffer(driver, dma, number_of_descriptors);

  // configure output header (enabled by default)
  configure_output_header(driver, dma.reg_base_addr, output_header);

  // mask interrupts
  mask_interrupts(driver, dma.reg_base_addr, false);

  // toggle engine off by default (must be called to enable interrupts)
  toggle_engine(driver, dma.reg_base_addr, false);
}

void enable_config_dma_multipass(HardwareDriver* driver, const Config& dma) {
  // Set last valid to a value > max descriptors, e.g +1 to loop forever. Note
  // that the value cannot exceed 0xff (maximum value for descriptors id).
  constexpr uint32_t cur_desc_default_val = 0xff;
  uint32_t reg = driver->read32(dma.engine.reg_base_addr + DMA_DESC_CONT_REG);
  uint32_t last_valid_descriptor = get_field(reg, DMA_LAST_DESC_CONT) + 1;
  assert(last_valid_descriptor < cur_desc_default_val);
  set_field(&reg, DMA_LAST_DESC_CONT, last_valid_descriptor);
  set_field(&reg, DMA_CUR_DESC_CONT, cur_desc_default_val);
  driver->write32(dma.engine.reg_base_addr + DMA_DESC_CONT_REG, reg);

  // turn DMA on
  toggle_engine(driver, dma.engine.reg_base_addr, true);

  // When turning config dma on after it has been configured for multipass,
  // it will start configuring NPs with 1st pass, so we need to wait for it
  // to complete
  if (!wait_for_interrupt_ext(driver, dma.engine,
                              DMA_BUFFER_END_INTS_DESC_BURST_DONE)) {
    panic("Timed out while dma was configuring NPs for multipass");
  }
}

void wait_config_dma_descriptor_complete(HardwareDriver* driver,
                                         const Config& dma) {
  // turn dma on
  toggle_engine(driver, dma.engine.reg_base_addr, true);
  // then wait for interrupt
  if (!wait_for_interrupt_ext(driver, dma.engine, DMA_BUFFER_END_INTS_OB)) {
    panic("Timed out while processing dma configuration request");
  }
  // turn dma off
  toggle_engine(driver, dma.engine.reg_base_addr, false);
}

void enqueue_extra_descriptor(HardwareDriver* driver, const Config& dma,
                              const Descriptor& descriptor) {
  // get number of descriptors
  auto reg = driver->read32(dma.engine.reg_base_addr + DMA_CONT_SIZE_REG);
  const auto num_descriptors = get_field(reg, DMA_MAX_DESC_CONTS) + 1;

  // Extra descriptor will be stored after all "standard" descriptors
  auto extra_descriptor_addr =
      dma.engine.descriptor_base_addr +
      (dma.engine.descriptor_bytes_size * num_descriptors);

  // copy descriptor in scratch buffer
  driver->write(extra_descriptor_addr, descriptor.data(),
                descriptor.size() * sizeof(Descriptor::value_type));
}

}  // namespace dma
}  // namespace akida
