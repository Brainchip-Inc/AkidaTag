/*******************************************************************************
 * Copyright 2021 Brainchip Holdings Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ********************************************************************************
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "akida/dense.h"
#include "akida/hw_version.h"
#include "akida/program_info.h"
#include "akida/span.h"
#include "akida/tensor.h"
#include "infra/exports.h"
#include "infra/hardware_driver.h"

#include "hardware_ident.h"

namespace akida {

class HardwareDevice;

using HardwareDevicePtr = std::shared_ptr<HardwareDevice>;
using HardwareDeviceConstPtr = std::shared_ptr<const HardwareDevice>;

namespace dma {
// Akida memory addresses are stored in uint32_t
using addr = uint32_t;
}  // namespace dma

/**
 * class HardwareDevice
 *
 * Public interface to an Akida Hardware Device.
 *
 * The main difference with Akida Device is the fact that HardwareDevice
 * objects driver real hardware, so they are capable of programming, performing
 * inference and few other hardware-specific calls.
 *
 */
class AKIDASHAREDLIB_EXPORT HardwareDevice {
 public:
  /**
   * @brief Get the Device version
   * @return a HwVersion
   */
  virtual HwVersion version() const = 0;

  /**
   * @brief Get the Device description
   * @return a char*
   */
  virtual const char* desc() const = 0;

  /**
   * @brief Creates a Hardware Device
   *
   * @param driver : the driver that should be used by the hardware device
   *
   * @return a HardwareDevice
   */
  static HardwareDevicePtr create(HardwareDriver* driver);

  /**
   * @brief Toggle the HardwareDevice clock counter on/off
   * @param enable : boolean to enable/disable clock counter
   */
  virtual void toggle_clock_counter(bool enable) = 0;

  /**
   * @brief Read the current HardwareDevice clock counter
   * @return a uint32 representing the clock count (can overlap)
   */
  virtual uint32_t read_clock_counter() = 0;

  /**
   * @brief Read the current HardwareDevice configuration clock counter
   * @return a uint32 representing the clock count (can overlap)
   */
  virtual uint32_t read_config_clock_counter() = 0;

  /**
   * @brief Return memory information
   * @return a tuple containing current memory usage (in bytes) and top memory
   * usage (in bytes)
   */
  using MemoryInfo = std::pair<uint32_t, uint32_t>;
  virtual MemoryInfo memory() const = 0;

  /**
   * @brief Reset top memory usage to current one
   */
  virtual void reset_top_memory() = 0;

  /**
   * @brief Perform hardware device programming with program buffer containing
   * both program info and program data
   *
   * @param program : serialized buffer containing both program info and the
   * program data
   * @param size : byte size of the program buffer
   * @return a ProgramInfo object containing several information about the
   * program
   */
  virtual ProgramInfo program(const uint8_t* program, size_t size) = 0;

  /**
   * @brief Perform hardware device programming when the program data have been
   * written to the device memory beforehand.
   *
   * @param program_info_buffer : serialized buffer containing only program info
   * @param program_info_size : byte size of the program_info buffer
   * @param program_data_address : address ofthis assumes program parameter
   * contains only serialized program info buffer, and program data are already
   * written at the address given
   * @return a ProgramInfo object containing several information about the
   * program
   */
  virtual ProgramInfo program_external_data(const uint8_t* program_info_buffer,
                                            size_t program_info_size,
                                            uint32_t program_data_address) = 0;

  /**
   * @brief Retrieve current program buffer
   * @return an akida::span (pointer and size) referencing the current program
   */
  virtual const span<uint8_t>& program() const = 0;

  /**
   * @brief Read the registers of a NP
   * @param output : A pointer to a buffer to contain the registers read
   * @param np : The NP to read
   * @param nb_registers : The number of registers to read
   */
  virtual void read_np_registers(uint32_t* output, const struct hw::Ident& np,
                                 uint32_t nb_registers) = 0;

  /**
   * @brief Configure the number of inputs that can be sent at the same time
   * (the number of enqueue calls without calling fetch). It is 15 max for a
   * single pass program without learning, or 1 for multipass program and when
   * learning is enabled. It will return the effective batch size applied.
   * @param requested_batch_size : the requested batch size
   * @param alloc_inputs : boolean to allocate memory for inputs. Required if
   * inputs are not directly accessible from akida
   * @return the effective batch size applied (can be lower than
   * requested_batch_size)
   */
  virtual size_t set_batch_size(size_t requested_batch_size,
                                bool allocate_inputs) = 0;

  /**
   * @brief Enable or disable learning mode of the current program
   * @param learn_en : boolean to enable learning on the last layer
   */
  virtual void toggle_learn(bool learn_en) = 0;

  /**
   * @brief Tells if current program has learning enabled
   * @return true if current program has learning enabled
   */
  virtual bool learn_enabled() const = 0;

  /**
   * @brief Tells current program learning memory size
   * @return memory size, in number of 32 bit words
   */
  virtual size_t learn_mem_size() const = 0;

  /**
   * @brief Writes a copy of the learn memory of current program in the given
   * buffer.
   * @param output_buffer : A pointer to a buffer large enough to contain
   * learning memory.
   */
  virtual void learn_mem(uint32_t* output_buffer) = 0;

  std::vector<uint32_t> learn_mem() {
    auto size = learn_mem_size();
    std::vector<uint32_t> ret(size);
    learn_mem(ret.data());
    return ret;
  }

  /**
   * @brief Updated learn memory from buffer containg a previously saved one
   * @param input_buffer : A pointer to a buffer containing updated learning
   * memory buffer
   */
  virtual void update_learn_mem(const uint32_t* input_buffer) = 0;

  /**
   * @brief Clear current program from hardware device, restoring its initial
   * state
   */
  virtual void unprogram() = 0;

  /**
   * @brief Processes inputs to train on a programmed device
   * @param inputs       : vector of 3D inputs Tensor
   * @param input_labels : integer value labels of the input classes,
   * for supervised learning
   * @return Sparse or Dense outputs from the model last layer
   */
  virtual std::vector<TensorUniquePtr> fit(
      const std::vector<TensorConstPtr>& inputs,
      const std::vector<int32_t>& input_labels) = 0;

  /**
   * @brief Processes inputs on a programmed device
   * @param inputs : vector of 3D inputs Tensor
   * @return vector of 3D outputs from the device
   */
  virtual std::vector<TensorUniquePtr> forward(
      const std::vector<TensorConstPtr>& inputs) = 0;

  /**
   * @brief Evaluates the results of processing on a programmed device
   *
   * This method propagates a set of inputs through a programmed device and
   * returns the results in the form of a Tensor of float values.
   * It applies ONLY on programs coming from models that do not have an
   * activation on the last layer.
   * The output values are obtained from the outputs discrete potentials by
   * applying a shift and a scale.
   *
   * @param inputs : vector of 3D inputs Tensor
   * @return vector of 3D rescaled output potentials from the programmed device
   */
  virtual std::vector<TensorUniquePtr> predict(
      const std::vector<TensorConstPtr>& inputs) = 0;

  /**
   * @brief Try to put the input in the pipeline queue on a programmed device,
   * starting queue execution if required
   * @param input: 3D input Tensor
   * @param label: integer value of the input class, for supervised learning
   * @return True if the tensor was successfully inserted in the pipeline, False
   * if the pipeline was full
   */
  virtual bool enqueue(const Tensor& input, const int32_t* label = nullptr) = 0;

  /**
   * @brief Try to put the input in the pipeline queue on a programmed device,
   * starting queue execution if required
   * @param address: akida-visible address where the input buffer is stored
   * @param label: integer value of the input class, for supervised learning
   * @return True if the input was successfully inserted in the pipeline, False
   * if the pipeline was full
   */
  virtual bool enqueue(dma::addr input_addr,
                       const int32_t* label = nullptr) = 0;

  /**
   * @brief Allocate the memory from the scratch buffer. Address in this area
   *  can then be used for inference by calling the enqueue method.
   * @param byte_size: amount of bytes that should be reserved.
   * @return Akida visible address that can be used for inference.
   */
  virtual dma::addr scratch_alloc(size_t byte_size) = 0;

  /**
   * @brief Free memory previously allocated using the scratch_alloc method.
   * Note that the Akida engine require alloc/free methods to be called in
   * symmetrical order.
   * @param address Akida address of the allocated memory.
   */
  virtual void scratch_free(dma::addr address) = 0;

  /**
   * @brief Fetch the pipeline queue for eventual result. This function will pop
   * one input from the pipeline queue, so it must be called once for each input
   * @return A 3D Tensor output, or nullptr if no output is available
   */
  virtual TensorUniquePtr fetch() = 0;

  /**
   * @brief Transform an output Dense Tensor in the form of a Tensor of float
   * values. It applies ONLY on outputs coming from programs that do not have an
   * activation on the last layer.
   * The output values are obtained from the outputs discrete potentials by
   * applying a shift and a scale.
   * */
  virtual DenseUniquePtr dequantize(const Dense& potentials) = 0;

  /**
   * @brief Get the driver used by the device
   * @return The HardwareDriver object used by the device
   */
  virtual HardwareDriver* driver() const = 0;
};

}  // namespace akida
