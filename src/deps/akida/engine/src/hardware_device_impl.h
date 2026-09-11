#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

#include "akida/hardware_device.h"
#include "akida/hw_version.h"
#include "akida/program_info.h"
#include "akida/tensor.h"
#include "infra/hardware_driver.h"

#include "device_memory.h"
#include "dma_engine.h"
#include "external_mem_mgr.h"
#include "memory_mgr.h"
#include "multipass_memory.h"
#include "pipeline_state.h"

namespace akida {

namespace dma {
// forward declarations
enum class Target;
}  // namespace dma

class HardwareDeviceImpl final : public HardwareDevice {
 public:
  HardwareDeviceImpl(HardwareDriver* driver);

  ~HardwareDeviceImpl();

  HwVersion version() const override;

  const char* desc() const override { return driver_->desc(); }

  void pipeline(bool enable);

  void toggle_clock_counter(bool enable) override;

  void reset_clock_counter();

  uint32_t read_clock_counter() override;

  uint32_t read_config_clock_counter() override;

  void read_np_registers(uint32_t* output, const struct hw::Ident& np,
                         uint32_t nb_registers) override;

  // Device fit
  std::vector<TensorUniquePtr> fit(
      const std::vector<TensorConstPtr>& inputs,
      const std::vector<int32_t>& input_labels) override;

  // Device forward
  std::vector<TensorUniquePtr> forward(
      const std::vector<TensorConstPtr>& inputs) override;

  // Device predict
  std::vector<TensorUniquePtr> predict(
      const std::vector<TensorConstPtr>& inputs) override;

  // Queue input
  bool enqueue(const Tensor& input, const int32_t* label = nullptr) override;

  // Queue input using Akida visible address
  bool enqueue(dma::addr input_addr, const int32_t* label = nullptr) override;

  // check for output
  TensorUniquePtr fetch() override;

  // apply rescale
  DenseUniquePtr dequantize(const Dense& potentials) override;

  // perform hardware device programming
  ProgramInfo program(const uint8_t* program, size_t size) override;

  ProgramInfo program_external_data(const uint8_t* program_info,
                                    size_t program_info_size,
                                    uint32_t program_data_address) override;

  size_t set_batch_size(size_t requested_batch_size,
                        bool allocate_inputs) override;

  // enable/disable learning mode
  void toggle_learn(bool learn_en) override;

  // unprogram current program
  void unprogram() override;

  // Return the memory used currently in the device
  MemoryInfo memory() const override { return mem_mgr_.report(); }

  void reset_top_memory() override { mem_mgr_.reset_top_usage(); }

  const akida::span<uint8_t>& program() const override {
    return current_program_buffer_;
  }

  bool learn_enabled() const override { return current_program_learn_en_; }

  size_t learn_mem_size() const override;

  void learn_mem(uint32_t* output_buffer) override;

  void update_learn_mem(const uint32_t* input_buffer) override;

  HardwareDriver* driver() const override { return driver_; }

  MemoryMgr* mem() { return &mem_mgr_; }

  ExternalMemoryMgr* external_mem() { return &external_mem_; }

  const dma::Config& dma_config() const { return dma_config_; }

  std::map<uint8_t, dma::Skip>* skip_dma_mem_info() { return &skip_dmas_; }

  dma::addr scratch_alloc(size_t byte_size) override {
    return mem_mgr_.alloc(byte_size);
  }

  void scratch_free(dma::addr address) override { mem_mgr_.free(address); }

 private:
  HardwareDriver* driver_;
  HwVersion version_;
  dma::Config dma_config_;
  dma::Inputs dma_event_;
  dma::Inputs dma_hrc_;
  // There is one skip dma memory info per skip connection
  std::map<uint8_t, dma::Skip> skip_dmas_;
  MemoryMgr mem_mgr_;
  akida::span<uint8_t> current_program_buffer_;
  ProgramInfo current_program_info_;
  bool current_program_learn_en_;
  bool clock_counter_en_;
  ExternalMemoryMgr external_mem_;

  // infos on memory that need to be allocated for multi pass program
  MultiPassMemory multi_pass_memory_;
  // infos on memory that need to be allocated for inference
  InferenceMemory inference_memory_;

  PipelineState pipeline_state_;

  // pipeline helper
  std::vector<TensorUniquePtr> forward_loop(
      const std::vector<TensorConstPtr>& inputs,
      const std::vector<int32_t>* labels);

  // DMA helpers
  const dma::Inputs& select_dma_engine(bool is_hrc);
  bool clock_counter_enabled();

  // programming helper
  void program(const ProgramInfo& program_info);

  // enqueue helpers
  bool enqueue(dma::addr input_addr, size_t size, const int32_t* label,
               std::optional<PipelineState::slot> slot);
  bool enqueue_checks(bool has_labels);

  template<typename T>
  DenseUniquePtr dequantize(const Dense& potentials);
};

}  // namespace akida
