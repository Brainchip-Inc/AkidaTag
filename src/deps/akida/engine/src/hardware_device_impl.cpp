#include "hardware_device_impl.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

#include "akida/dense.h"
#include "akida/hw_version.h"
#include "akida/input_conversion.h"
#include "akida/program_info.h"
#include "akida/program_memory_info.h"
#include "akida/registers_top_level.h"
#include "akida/shape.h"
#include "engine/dma.h"
#include "engine/dma_config_ops.h"

#include "infra/int_ops.h"
#include "infra/registers_common.h"
#include "infra/system.h"

#include "device_programmer.h"
#include "dma_config_mem_rw.h"
#include "dma_desc_format.h"
#include "dma_desc_ops.h"
#include "dma_engine.h"
#include "dma_engine_ops.h"
#include "dma_events_ops.h"
#include "dma_image_ops.h"
#include "external_mem_mgr.h"
#include "memory_utils.h"
#include "registers_dma_engine.h"
#include "reset_nps.h"
#include "skipdma_ops.h"

namespace akida {

static void toggle_multi_pass(HardwareDeviceImpl* device,
                              bool enable_multi_pass);

HardwareDeviceImpl::HardwareDeviceImpl(HardwareDriver* driver)
    : driver_(driver),
      version_(read_hw_version(*driver_)),
      dma_config_{dma::Engine(dma_config_reg_base(driver_->top_level_reg()),
                              dma::config::DESC_BYTE_SIZE, dma::kAlignment)},
      dma_event_{dma::Engine(dma_event_reg_base(driver_->top_level_reg()),
                             dma::event::DESC_BYTE_SIZE, dma::kAlignment)},
      dma_hrc_{dma::Engine(dma_hrc_reg_base(driver_->top_level_reg()),
                           dma::hrc::DESC_BYTE_SIZE, dma::kAlignment)},
      mem_mgr_(driver->scratch_memory(), driver->scratch_size()),
      current_program_buffer_{nullptr, 0},
      current_program_learn_en_(false),
      clock_counter_en_(false),
      external_mem_(&mem_mgr_, driver) {
  if (version_ == akida::NSoC_v1) {
    panic(
        "NSoC_v1 is not supported on this version. Please install akida 2.0.5 "
        "instead.");
  }
}

HardwareDeviceImpl::~HardwareDeviceImpl() {
  free_allocated_buffer(&mem_mgr_, &dma_config_.engine.descriptor_base_addr);
  free_allocated_buffer(&mem_mgr_, &dma_event_.engine.descriptor_base_addr);
  free_allocated_buffer(&mem_mgr_, &dma_hrc_.engine.descriptor_base_addr);
  for (auto& skip_dma : skip_dmas_) {
    free_allocated_buffer(&mem_mgr_,
                          &skip_dma.second.engine.descriptor_base_addr);
  }
}

HwVersion HardwareDeviceImpl::version() const { return version_; }

void HardwareDeviceImpl::read_np_registers(uint32_t* output,
                                           const struct hw::Ident& np,
                                           uint32_t nb_registers) {
  auto has_alloc = false;
  if (dma_config_.engine.descriptor_base_addr == 0) {
    alloc_dma_descriptors(&dma_config_.engine, &mem_mgr_,
                          dma::kMinNbDescriptors);
    dma::init_default_dma(driver_, dma_config_.engine, dma::kMinNbDescriptors,
                          false);
    has_alloc = true;
  }
  std::array<uint32_t, dma::kConfigNpHeaderWordSize> header;
  dma::format_config_header(header.data(), np, dma::Target::NpRegisters,
                            nb_registers, 0);
  dma::dma_config_read(output, nb_registers, header.data(), dma_config_,
                       &mem_mgr_, driver_, true);
  if (has_alloc) {
    free_allocated_buffer(&mem_mgr_, &dma_config_.engine.descriptor_base_addr);
  }
}

std::vector<TensorUniquePtr> HardwareDeviceImpl::fit(
    const std::vector<TensorConstPtr>& inputs,
    const std::vector<int32_t>& input_labels) {
  // Check the device had been programmed
  if (!current_program_info_.is_valid()) {
    panic("Cannot fit without a program");
  }
  if (!current_program_learn_en_)
    panic("Learn must be enabled to call the fit method.");

  return forward_loop(inputs, &input_labels);
}

std::vector<TensorUniquePtr> HardwareDeviceImpl::forward(
    const std::vector<TensorConstPtr>& inputs) {
  // Check the device had been programmed
  if (!current_program_info_.is_valid()) {
    panic("Cannot forward without a program");
  }
  if (current_program_learn_en_)
    panic("Learn must be disabled to call the forward method.");

  return forward_loop(inputs, nullptr);
}

const dma::Inputs& HardwareDeviceImpl::select_dma_engine(bool dense_inputs) {
  // Only enable the input DMA used by the current network:
  // HRC DMA if 1st layer is InputConvolutional, Event DMA otherwise
  dma::toggle_engine(driver_, dma_hrc_.engine.reg_base_addr, dense_inputs);
  dma::toggle_engine(driver_, dma_event_.engine.reg_base_addr, !dense_inputs);

  return dense_inputs ? dma_hrc_ : dma_event_;
}

void HardwareDeviceImpl::pipeline(bool enabled) {
  dma::toggle_pipeline(driver_, dma_event_, enabled);
  dma::toggle_pipeline(driver_, dma_hrc_, enabled);
}

void HardwareDeviceImpl::toggle_clock_counter(bool enable) {
  dma::toggle_buffer_timer(driver_, dma_event_.engine, enable);
  dma::toggle_buffer_timer(driver_, dma_hrc_.engine, enable);
  clock_counter_en_ = enable;
}

void HardwareDeviceImpl::reset_clock_counter() {
  dma::toggle_buffer_timer(driver_, dma_event_.engine, clock_counter_en_);
  dma::toggle_buffer_timer(driver_, dma_hrc_.engine, clock_counter_en_);
}

uint32_t HardwareDeviceImpl::read_clock_counter() {
  // read clock from HRC DMA or read from events DMA
  auto hrc_count_number = dma::read_buffer_timer(driver_, dma_hrc_.engine);
  auto event_count_number = dma::read_buffer_timer(driver_, dma_event_.engine);
  return std::max(hrc_count_number, event_count_number);
}

uint32_t HardwareDeviceImpl::read_config_clock_counter() {
  return dma::read_buffer_timer(driver_, dma_config_.engine);
}

bool HardwareDeviceImpl::clock_counter_enabled() {
  return dma::is_buffer_timer_enabled(*driver_, dma_event_);
}

static void enable_global_interrupts(HardwareDriver* driver,
                                     bool dense_inputs) {
  const auto top_level_registers = driver->top_level_reg();

  // mask all interrupts except input dma (SCC if input are dense else AEDMA)
  uint32_t reg = 0xFFFFFFFF;
  set_field(&reg,
            dense_inputs ? REG_INTERRUPT_CONTROLLER_SOURCE_MASK_SCC_HRC
                         : REG_INTERRUPT_CONTROLLER_SOURCE_MASK_AEDMA,
            0);
  driver->write32(top_level_registers + REG_INTERRUPT_CONTROLLER_SOURCE_MASK,
                  reg);

  // enable global interrupts
  reg = 0;
  set_field(&reg, INTERRUPT_CONTROLLER_GENERAL_CONTROL_GLB_INT_EN, 1);
  driver->write32(
      top_level_registers + REG_INTERRUPT_CONTROLLER_GENERAL_CONTROL, reg);
}

void HardwareDeviceImpl::program(const ProgramInfo& program_info) {
  if (program_info.device_version() != version_) {
    panic("Program device version and device version are not compatible");
  }
  // Unprogram the previous mapping
  unprogram();

  // allocate config dma descriptors
  alloc_dma_descriptors(
      &dma_config_.engine, &mem_mgr_,
      program_info.number_of_program_descriptors_required() +
          program_info.number_of_extra_program_descriptors_required());

  // Set multi pass mode
  bool multi_pass_en = program_info.number_of_passes() > 1;
  toggle_multi_pass(this, multi_pass_en);
  // init config dma
  dma::init_config_dma(driver_, dma_config_, program_info);

  if (multi_pass_en) {
    // alloc required multi pass memory
    multi_pass_memory_.alloc_memory(&mem_mgr_, program_info.input_is_dense());
    // Write DMA descriptors for multipass
    DeviceProgrammer programmer(program_info, this);
    programmer.program_multi_pass(&multi_pass_memory_);
    // Enable dma config for multipass mode
    dma::enable_config_dma_multipass(driver_, dma_config_);
  } else {
    DeviceProgrammer programmer(program_info, this);
    programmer.program_single_pass();
  }

  // enable akida global interrupts
  enable_global_interrupts(driver_, program_info.input_is_dense());
}

static void check_input_dims(const Index* program_in_dims,
                             const Shape& inputs_shape) {
  bool valid_dims = true;
  switch (inputs_shape.size()) {
    case 1:  // fully connected, 1 dimension
      if (inputs_shape[0] !=
          program_in_dims[0] * program_in_dims[1] * program_in_dims[2]) {
        valid_dims = false;
      }
      break;
    case 3:  // other cases (check only that data size is compatible)
      if (inputs_shape[0] * inputs_shape[1] * inputs_shape[2] !=
          program_in_dims[0] * program_in_dims[1] * program_in_dims[2]) {
        valid_dims = false;
      }
      break;
    default:
      valid_dims = false;
      break;
  }
  if (!valid_dims) {
    panic("Invalid input dimensions for this program");
  }
}

// reset whole akida core, including DMAs
static void core_reset(HardwareDriver* driver) {
  const auto top_level_reg_offset = driver->top_level_reg();
  auto reg_gen_ctrl =
      driver->read32(top_level_reg_offset + REG_GENERAL_CONTROL);
  // Reset NP & CORE
  set_field(&reg_gen_ctrl, AK_CORE_RST, 1);
  set_field(&reg_gen_ctrl, SCC_CORE_RESET, 1);
  driver->write32(top_level_reg_offset + REG_GENERAL_CONTROL, reg_gen_ctrl);
  // 20 cycles should be waited. Waiting 1ms is more than enough.
  msleep(1);
  // Fields need to be reset to 0
  set_field(&reg_gen_ctrl, AK_CORE_RST, 0);
  set_field(&reg_gen_ctrl, SCC_CORE_RESET, 0);
  driver->write32(top_level_reg_offset + REG_GENERAL_CONTROL, reg_gen_ctrl);
  // 40 cycles should be waited. Waiting 1ms is more than enough.
  msleep(1);
}

static inline const int32_t* get_label(const std::vector<int32_t>& labels,
                                       size_t index) {
  return labels.size() == 1 ? &labels[0] : &labels[index];
}

std::vector<TensorUniquePtr> HardwareDeviceImpl::forward_loop(
    const std::vector<TensorConstPtr>& inputs,
    const std::vector<int32_t>* labels) {
  std::vector<TensorUniquePtr> result;

  result.reserve(inputs.size());
  size_t nb_inputs_queued = 0;

  // used to detect eventual timeout
  auto last_output_read = time_ms();
  static constexpr int32_t timeout = 5000;  // 5s timeout

  // store converted inputs that need to be kept alive while they have not been
  // processed
  std::vector<TensorUniquePtr> converted_inputs;
  const Tensor* input_to_queue;

  // loop until all outputs have been read
  while (result.size() < inputs.size()) {
    // keep system alive
    kick_watchdog();
    // enqueue as many jobs as current pipeline allow us
    bool pipeline_ready = true;
    while (nb_inputs_queued < inputs.size() && pipeline_ready) {
      // get label that could be the same for all inputs
      const int32_t* label = nullptr;
      if (labels != nullptr && labels->size() > 0) {
        label = get_label(*labels, nb_inputs_queued);
      }
      const auto& current_input = *inputs[nb_inputs_queued];
      // convert input if needed
      if (current_program_info_.input_is_dense()) {
        // dense input
        input_to_queue = conversion::as_dense(current_input);
        if (input_to_queue == nullptr) {
          converted_inputs.push_back(
              conversion::to_dense(static_cast<const Sparse&>(current_input)));
          input_to_queue = converted_inputs.back().get();
        }
      } else {
        // sparse input
        input_to_queue = conversion::as_sparse(current_input);
        if (input_to_queue == nullptr) {
          converted_inputs.push_back(conversion::to_sparse(
              static_cast<const Dense&>(current_input), current_program_info_));
          input_to_queue = converted_inputs.back().get();
        }
      }
      // try to enqueue
      pipeline_ready = enqueue(*input_to_queue, label);
      // if input was inserted, increment counter
      if (pipeline_ready) {
        ++nb_inputs_queued;
      }
    }
    // then read outputs that are ready
    do {
      auto output = fetch();
      if (output == nullptr) {
        break;
      }
      result.push_back(std::move(output));
      last_output_read = time_ms();
    } while (true);
    // no more output to pull
    if (time_ms() - last_output_read > timeout) {
      unprogram();
      panic(
          "Fatal error: timed out while fetching output. Inputs queued: %u/%u. "
          "Results fetched: %u/%u.",
          nb_inputs_queued, inputs.size(), result.size(), inputs.size());
    }
  }
  return result;
}

static void toggle_multi_pass(HardwareDeviceImpl* device,
                              bool enable_multi_pass) {
  auto driver = device->driver();
  const auto top_level_reg_offset = driver->top_level_reg();
  auto reg_gen_ctrl =
      driver->read32(top_level_reg_offset + REG_GENERAL_CONTROL);
  // toggle partial reconfig bit at top level register
  set_field(&reg_gen_ctrl, PR_MESH_RST_END, enable_multi_pass ? 1 : 0);
  driver->write32(top_level_reg_offset + REG_GENERAL_CONTROL, reg_gen_ctrl);
}

void HardwareDeviceImpl::unprogram() {
  // free allocated outputs buffer
  free_allocated_buffer(&mem_mgr_, &inference_memory_.outputs_base_address);
  // free allocated inputs buffer
  free_allocated_buffer(&mem_mgr_, &inference_memory_.inputs_base_address);

  // free dmas memory
  free_allocated_buffer(&mem_mgr_, &dma_hrc_.engine.descriptor_base_addr);
  free_allocated_buffer(&mem_mgr_, &dma_event_.engine.descriptor_base_addr);
  // free config dma memory
  free_allocated_buffer(&mem_mgr_, &dma_config_.engine.descriptor_base_addr);
  // free config skip dma memory
  for (auto& skip_dma : skip_dmas_) {
    free_allocated_buffer(&mem_mgr_,
                          &skip_dma.second.engine.descriptor_base_addr);
    free_allocated_buffer(&mem_mgr_, &skip_dma.second.outputs_base_address);
  }
  skip_dmas_.clear();
  // if there is a current program, rewind it and reset NPs
  if (current_program_info_.is_valid()) {
    // rewind the whole program
    DeviceProgrammer programmer(current_program_info_, this);
    programmer.unprogram();

    // disable partial reconfig and reset DMAs to go back to default
    if (current_program_info_.number_of_passes() > 1) {
      toggle_multi_pass(this, false);
      multi_pass_memory_.free_memory(&mem_mgr_);
    }

    current_program_info_ = ProgramInfo();
    current_program_buffer_ = {nullptr, 0};
    current_program_learn_en_ = false;
  }

  // Core reset is necessary to avoid certains timeouts observed when
  // switching to single pass. These are probably due to an internal sync
  // issue between DMAs, but the core reset seems to be enough to fix the
  // problem. The core reset also turns off the NPs, which is necessary before
  // setting their registers.
  core_reset(driver_);

  // Reset the hardware device Mesh
  // FIXME: currently this is done on each call of unprogram, because program is
  // not allocated at once, but each track has its own allocation, so we can
  // have an out of memory in the middle of programming NPs. Once program memory
  // will be allocated in a single block, we can move this into the `if
  // (current_program_.first != nullptr)` block
  reset_nps_logic_and_cfg(driver_);

  // Reset the clock counter because it was turned off by the core reset.
  reset_clock_counter();

  // reset pipeline state (set its size to 0)
  pipeline_state_.reset(0, 0);

  // reset external memory in case of leftovers due to previous exception
  // it must be reset before MemoryManager or its entries might be already
  // free'd
  external_mem_.reset();
  // reset memory in case of leftovers due to previous exception
  mem_mgr_.reset();
}

inline static uint32_t get_pipeline_size(bool multi_pass) {
  return multi_pass ? 1 : dma::MAX_PIPELINE_SIZE;
}

ProgramInfo HardwareDeviceImpl::program(const uint8_t* program, size_t size) {
  if (!program) {
    panic("program should not be null");
  }

  // verify program info validity by creating a ProgramInfo object
  ProgramInfo program_info(program, size);

  this->program(program_info);

  // Store program info & program buffer
  current_program_info_ = program_info;
  current_program_buffer_ = {program, size};

  return program_info;
}

ProgramInfo HardwareDeviceImpl::program_external_data(
    const uint8_t* program_info_buffer, size_t program_info_size,
    uint32_t program_data_address) {
  if (!program_info_buffer) {
    panic("program_info_buffer should not be null");
  }

  // verify program info validity by creating a ProgramInfo object
  ProgramInfo program_info(program_info_buffer, program_info_size,
                           program_data_address);

  this->program(program_info);

  // Store program info
  current_program_info_ = program_info;
  current_program_buffer_ = {program_info_buffer, program_info_size};

  return program_info;
}

size_t HardwareDeviceImpl::set_batch_size(size_t requested_batch_size,
                                          bool allocate_inputs) {
  if (!current_program_info_.is_valid()) {
    panic("Cannot set batch size if device is not programmed");
  }
  if (!pipeline_state_.empty()) {
    panic("Cannot set batch size while all jobs have not been fetched");
  }

  const bool multi_pass_en = current_program_info_.number_of_passes() > 1;
  const size_t max_batch_size = get_pipeline_size(multi_pass_en);
  const auto effective_batch_size =
      std::min(requested_batch_size, max_batch_size);

  // perform action only if batch size has changed
  if (effective_batch_size != pipeline_state_.max_size()) {
    // pipeline is enabled if program is not multipass
    const bool is_pipeline = !multi_pass_en && !current_program_learn_en_;
    // reconfigure pipeline size
    auto& input_dma =
        current_program_info_.input_is_dense() ? dma_hrc_ : dma_event_;
    const auto effective_nb_desc = std::max(
        static_cast<uint32_t>(effective_batch_size), dma::kMinNbDescriptors);

    // free and reallocate input DMA descriptors then configure the input DMA
    free_allocated_buffer(&mem_mgr_, &input_dma.engine.descriptor_base_addr);
    alloc_dma_descriptors(&input_dma.engine, &mem_mgr_, effective_nb_desc);
    init_default_dma(driver_, input_dma.engine, effective_nb_desc);

    // skip dma external memory allocation only for single pass, as it memory
    // allocation depends on pipeline size.
    if (!multi_pass_en) {
      if (version_.get_ip_version() == akida::IpVersion::v2) {
        // free previously allocated memory
        for (auto& skip_dma : skip_dmas_) {
          free_allocated_buffer(&mem_mgr_,
                                &skip_dma.second.engine.descriptor_base_addr);
          free_allocated_buffer(&mem_mgr_,
                                &skip_dma.second.outputs_base_address);
        }
        skip_dmas_.clear();
        constexpr uint32_t pass_idx{0};
        constexpr bool is_single_pass = true;
        skipdma::program_ext_mem(current_program_info_, dma_config_, &mem_mgr_,
                                 &external_mem_, driver_, effective_batch_size,
                                 is_pipeline, pass_idx, is_single_pass,
                                 &skip_dmas_);
      }
    }
    //   Reset the clock counter because it was turned off by the DMA reset.
    reset_clock_counter();
    if (version_ != NSoC_v2) {
      // When using dense/sparse outputs, we need to enable/disable the output
      // buffer automatic clearing from the input dma
      uint32_t clear_size =
          current_program_info_.output_is_dense()
              ? static_cast<uint32_t>(
                    output_memory_required(current_program_info_) -
                    dma::kOutputHeaderByteSize)  // we need to substract header
                                                 // size
              : 0;
      set_output_buffer_clear(driver_, input_dma, clear_size);
    }
    pipeline(is_pipeline);
    if (multi_pass_en) {
      // configure inputs DMA for multipass
      dma::prepare_engine_multi_pass(
          driver_, input_dma, multi_pass_memory_.hw_generated_descriptor_addr,
          multi_pass_memory_.hw_generated_descriptor_out_addr,
          current_program_info_.number_of_passes());
    }
    // pipeline state must be reset with the corresponding DMA last job id
    // processed
    pipeline_state_.reset(dma::get_last_job_id_processed(driver_, input_dma),
                          effective_batch_size);

    // free & reallocate outputs memory
    free_allocated_buffer(&mem_mgr_, &inference_memory_.outputs_base_address);
    inference_memory_.outputs_base_address = mem_mgr_.alloc(
        output_memory_required(current_program_info_) * effective_batch_size);

    // free allocated inputs
    free_allocated_buffer(&mem_mgr_, &inference_memory_.inputs_base_address);
    if (allocate_inputs) {
      // if requested, allocate inputs memory. We force them to be 32b
      // aligned, because some drivers cannot access unaligned area
      const auto aligned_input_memory_required = align_up(
          static_cast<uint32_t>(input_memory_required(current_program_info_)),
          static_cast<uint32_t>(sizeof(dma::w32)));
      inference_memory_.inputs_base_address =
          mem_mgr_.alloc(aligned_input_memory_required * effective_batch_size);
    }
  }

  return effective_batch_size;
}

void HardwareDeviceImpl::toggle_learn(bool learn_en) {
  if (!current_program_info_.is_valid()) {
    panic("Cannot toggle learn if device is not programmed");
  }
  if (!current_program_info_.can_learn()) {
    panic("Cannot toggle learning mode on this program, it cannot learn");
  }

  // Learning mode is set without reprogramming entirely
  const auto multi_pass = current_program_info_.number_of_passes() > 1;
  if (multi_pass) {
    DeviceProgrammer programmer(current_program_info_, this);
    programmer.configure_learning_mode_multi_pass(multi_pass_memory_, learn_en);

    // toggle extra descriptors if learn is enabled
    dma::toggle_extra_descriptors(
        driver_, dma_config_,
        learn_en &&
            current_program_info_
                    .number_of_extra_program_descriptors_required() > 0);
  } else {
    DeviceProgrammer programmer(current_program_info_, this);
    programmer.configure_learning_mode_single_pass(learn_en);
  }

  // Pipeline can only be enabled in single pass if learn is disabled
  this->pipeline(!multi_pass && !learn_en);

  current_program_learn_en_ = learn_en;
}

std::vector<TensorUniquePtr> HardwareDeviceImpl::predict(
    const std::vector<TensorConstPtr>& inputs) {
  // Check the device had been programmed
  if (!current_program_info_.is_valid()) {
    panic("Cannot predict without a program");
  }
  if (current_program_info_.activation_enabled()) {
    panic("predict requires activations to be disabled");
  }
  if (current_program_learn_en_) {
    panic("Learn must be disabled to call the predict method.");
  }

  // first process all outputs
  auto outputs = forward_loop(inputs, nullptr);

  // Prepare results vector
  std::vector<TensorUniquePtr> result;
  result.reserve(outputs.size());
  for (Index i = 0; i < outputs.size(); i++) {
    // Outputs should be dense
    auto potentials = conversion::as_dense(*outputs[i]);
    assert(potentials);

    result.push_back(dequantize(*potentials));
  }

  return result;
}

bool HardwareDeviceImpl::enqueue_checks(bool has_labels) {
  if (!current_program_info_.is_valid()) {
    panic("Device must be programmed before enqueuing inputs");
  }
  if (!current_program_learn_en_ && has_labels) {
    panic("Learn must be enable to call enqueue with a label");
  }
  if (pipeline_state_.max_size() == 0) {
    panic("A batch size must be defined before enqueuing inputs");
  }

  // check if there is space left in pipeline
  if (pipeline_state_.full()) {
    // pipeline is full, return false
    return false;
  }
  return true;
}

bool HardwareDeviceImpl::enqueue(dma::addr input_addr, size_t input_size,
                                 const int32_t* label,
                                 std::optional<PipelineState::slot> slot) {
  if (!enqueue_checks(label)) {
    return false;
  }

  // in multi pass, we can only enqueue 1 descriptor at a time
  const auto is_multi_pass = current_program_info_.number_of_passes() > 1;

  // check if input is in the correct format
  const auto input_is_dense = current_program_info_.input_is_dense();

  // check if input dimensions are as expected
  const auto* in_dims = current_program_info_.input_dims();

  // determine which dma controller should be used for inputs
  const auto& dma_inputs = select_dma_engine(input_is_dense);

  // Job slot is the next job that should be processed.
  if (!slot.has_value()) {
    slot = pipeline_state_.reserve_job();
  }
  const auto& job_slot = slot.value();

  // calculate address where output will be written
  const auto out_buffer_size = output_memory_required(current_program_info_);
  const dma::addr address_out =
      inference_memory_.outputs_base_address +
      static_cast<dma::addr>(out_buffer_size * job_slot.index);

  // learn class is label + 1, or 0 if no label
  uint32_t learn_class = label != nullptr ? *label + 1 : 0;

  // generate descriptor
  const auto descriptor =
      input_is_dense
          ? dma_dense_descriptor(
                input_addr, address_out, job_slot.job_id, learn_class, in_dims,
                current_program_info_.dense_input_window_width(),
                current_program_info_.dense_input_window_height())
          : dma::format_event_desc(
                job_slot.job_id, input_addr, address_out,
                static_cast<uint32_t>(input_size / sizeof(dma::w32)),
                learn_class);

  // in multi pass, we have to set output address in the input DMA since we're
  // using HW generated address
  if (is_multi_pass) {
    driver_->write32(
        dma_inputs.engine.reg_base_addr + DMA_REPLAY_OB_EVENT_BUF_ADDR_REG,
        address_out);
    if (label) {
      // The learn class was already in the DMA descriptor, but in multipass
      // there is a bug that makes that it is ignored. Whe workaround is to set
      // the DMA_REPLAY_DESC_WORD5_LEARN_CLASS that will override the WORD5 pf
      // the descriptor. Since batch_size is 1 anyway, it should be fine.
      assert(learn_class && "learn_class should not be 0 for hw");
      uint32_t value = 0;
      set_field(&value, DMA_REPLAY_DESC_WORD5_LEARN_CLASS, learn_class);
      driver_->write32(
          dma_inputs.engine.reg_base_addr + DMA_REPLAY_DESC_WORD5_REG, value);
    }
  }

  // store job information.
  pipeline_state_.enqueue_job(job_slot.job_id, address_out);

  // send descriptor to dma
  dma::enqueue_descriptor(driver_, dma_inputs.engine, descriptor);

  return true;
}

bool HardwareDeviceImpl::enqueue(dma::addr address_in, const int32_t* label) {
  if (!enqueue_checks(label)) {
    return false;
  }
  auto input_dims = current_program_info_.input_dims();
  auto input_size = input_dims[0] * input_dims[1] * input_dims[2];
  return enqueue(address_in, input_size, label, {});
}

bool HardwareDeviceImpl::enqueue(const Tensor& input, const int32_t* label) {
  // check if input is in the correct format
  const auto input_is_dense = current_program_info_.input_is_dense();
  if (input_is_dense) {
    const auto* dense_input = conversion::as_dense(input);
    if (dense_input == nullptr) {
      panic("Input should be converted to Dense format before calling enqueue");
    }
  } else {
    const auto* sparse_input = conversion::as_sparse(input);
    if (sparse_input == nullptr) {
      panic(
          "Input should be converted to Sparse format before calling "
          "enqueue");
    }
  }

  // check if input dimensions are as expected
  const auto* in_dims = current_program_info_.input_dims();
  check_input_dims(in_dims, input.dimensions());

  if (!enqueue_checks(label)) {
    return false;
  }

  // Job slot is the next job that should be processed.
  const auto job_slot = pipeline_state_.reserve_job();

  // get input address on device
  dma::addr address_in;
  if (!accessible_from_akida(input.buffer()->data(), *driver_)) {
    if (inference_memory_.inputs_base_address == 0) {
      panic(
          "Input is not accessible by akida, but no memory has been "
          "allocated for it");
    }
    // calculate the input address on device (if it has been allocated, it is
    // aligned to 32 bits)
    const auto input_buffer_size = align_up(
        static_cast<uint32_t>(input_memory_required(current_program_info_)),
        static_cast<uint32_t>(sizeof(dma::w32)));
    address_in = inference_memory_.inputs_base_address +
                 static_cast<dma::addr>(input_buffer_size * job_slot.index);
    // copy input to device
    driver_->write(address_in, input.buffer()->data(), input.buffer()->size());
  } else {
    // input is already accessible by akida, no need to copy it
    address_in = to_dma_addr(input.buffer()->data());
  }

  return enqueue(address_in, input.buffer()->size(), label, job_slot);
}

TensorUniquePtr HardwareDeviceImpl::fetch() {
  // if queue is empty, return null
  if (pipeline_state_.empty()) {
    return nullptr;
  }

  // select input dma
  const auto& input_dma =
      current_program_info_.input_is_dense() ? dma_hrc_ : dma_event_;

  if (current_program_info_.number_of_passes() > 1) {
    // in multi pass, there is only 1 job at a time so we just check for an
    // interrupt
    if (!dma::check_for_interrupt(driver_, input_dma.engine,
                                  DMA_BUFFER_END_STATUS_DESC_BURST_DONE)) {
      // no interrupt, output is not ready yet
      return nullptr;
    }
  } else {
    // in single pass, we need to check that last processed job id changed
    if (pipeline_state_.last_job_fetched() ==
        dma::get_last_job_id_processed(driver_, input_dma)) {
      return nullptr;
    }
  }
  // clear interrupts
  dma::clear_interrupts(driver_, input_dma.engine);

  // pop job from the queue
  auto job = pipeline_state_.pop_job();

  // read output
  auto result = dma_events_read_outputs(driver_, job.output_address,
                                        current_program_info_);

  return result;
}

template<typename T>
DenseUniquePtr HardwareDeviceImpl::dequantize(const Dense& potentials) {
  // Get potentials strides and data from program
  auto shifts = current_program_info_.shifts();
  auto scales = current_program_info_.scales();
  assert(shifts.size == scales.size);
  const auto& shift = shifts.data;
  const auto& scale = scales.data;

  // perform sanity checks
  const auto coords = potentials.dimensions();
  if (coords.size() != 3) {
    panic("dequantize expects a 3D Dense");
  }

  // Get potentials strides and data to access them via linear index
  const auto pot_strides = potentials.strides();
  const auto pot_data = potentials.data<T>();
  // Allocate a dense output in the form of a RowMajor Tensor
  auto rescaled_outputs =
      Dense::create(TensorType::float32, coords, Dense::Layout::RowMajor);
  // Get rescaled outputs data
  const auto resc_data = rescaled_outputs->data<float>();
  for (Index x = 0; x < coords[0]; x++) {
    for (Index y = 0; y < coords[1]; y++) {
      // move pointer at the beginning of the neuron
      Index coord_n0[] = {x, y, 0};
      auto coord_lin_index_n0 = linear_index(coord_n0, pot_strides);
      auto poti = &pot_data[coord_lin_index_n0];
      auto resci = &resc_data[coord_lin_index_n0];
      for (Index n = 0; n < coords[2]; n++) {
        // Evaluate rescaled output
        auto value = static_cast<float>(poti[n] - shift[n]) * scale[n];
        // Set rescaled value at the same index than output
        resci[n] = value;
      }
    }
  }
  return rescaled_outputs;
}

DenseUniquePtr HardwareDeviceImpl::dequantize(const Dense& potentials) {
  if (potentials.layout() != Dense::Layout::RowMajor) {
    panic("dequantize expects a RowMajor Dense");
  }
  switch (potentials.type()) {
    case TensorType::int32:
      return dequantize<int32_t>(potentials);
    case TensorType::int16:
      return dequantize<int16_t>(potentials);
    case TensorType::int8:
      return dequantize<int8_t>(potentials);
    default:
      panic("Dequantize expects int32/int16/int8 Dense.");
  }
}

size_t HardwareDeviceImpl::learn_mem_size() const {
  if (!current_program_info_.is_valid()) {
    return 0;
  }
  return current_program_info_.learn_weights_word_size();
}

void HardwareDeviceImpl::learn_mem(uint32_t* output_buffer) {
  if (!current_program_learn_en_) {
    panic("learn is not enabled");
  }
  DeviceProgrammer programmer(current_program_info_, this);
  programmer.get_learn_memory(output_buffer);
}

void HardwareDeviceImpl::update_learn_mem(const uint32_t* input_buffer) {
  DeviceProgrammer programmer(current_program_info_, this);
  programmer.update_learn_memory(input_buffer);
}

}  // namespace akida
