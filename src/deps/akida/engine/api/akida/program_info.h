#pragma once

#include <cstdint>

#include "akida/hw_version.h"
#include "akida/np.h"
#include "akida/shape.h"
#include "akida/span.h"

namespace akida {
namespace fb {
struct ProgramInfo;
enum IoType : int8_t;
}  // namespace fb

// Helper class to extract information about a program
class AKIDASHAREDLIB_EXPORT ProgramInfo {
 public:
  struct SkipDmaInfoTrack {
    np::Info info;
    bool used_for_tnp_b;
    uint8_t skip_length;
    uint8_t skip_connect_id;
    uint32_t ob_32b_size;
  };

  /**
   * @brief Default constructor, initialize a ProgramInfo in an invalid state
   */
  ProgramInfo();

  /**
   * @brief constructor to build a ProgramInfo object when the program data have
   * been written to the device memory beforehand.
   *
   * @param serialized_program_info : buffer containing the serialized program
   * info
   * @param program_info_size : size of the program info buffer
   * @param program_data_address : address, as it is seen from akida on the
   * device, of corresponding program data that must have been written
   * beforehand.
   */
  explicit ProgramInfo(const uint8_t* serialized_program_info,
                       size_t program_info_size, uint32_t program_data_address);

  /**
   * @brief constructor to build a ProgramInfo object when the serialized
   * program buffer contains both program info and program data.
   *
   * @param serialized_program : buffer containing the serialized program
   * info and the program data
   * @param program_size : size of the program buffer
   */
  explicit ProgramInfo(const uint8_t* serialized_program, size_t program_size);

  /**
   * @brief Return the Hardware version the program was generated for
   */
  HwVersion device_version() const;

  /**
   * @brief Return the input dimensions of the program as a 3 elements array
   */
  const uint32_t* input_dims() const;

  /**
   * @brief Return the output dimensions of the program as a 3D Shape
   */
  Shape output_dims() const;

  /**
   * @brief Return true if inputs are expected to be dense, false otherwise
   */
  bool input_is_dense() const;

  /**
   * @brief Return true if outputs are dense, false otherwise
   */
  bool output_is_dense() const;

  /**
   * @brief Return true if outputs are activations, false if they are potentials
   */
  bool activation_enabled() const;

  /**
   * @brief Return the width of dense inputs window (area sent to HRC)
   */
  uint32_t dense_input_window_width() const;

  /**
   * @brief Return the height of dense inputs window (area sent to HRC)
   */
  uint32_t dense_input_window_height() const;

  /**
   * @brief Return true if the program can toggle learn
   */
  bool can_learn() const;

  /**
   * @brief Return the number of 32b words needed to store weights of learning
   * layer
   */
  uint32_t learn_weights_word_size() const;

  /**
   * @brief In case of a multipass program, return the number of descriptors per
   * pass (it is the same for every pass)
   */
  uint8_t number_of_descriptors_per_pass() const;

  /**
   * @brief Return the number of passes of the program
   */
  uint32_t number_of_passes() const;

  /**
   * @brief Return the number of descriptors required to program akida (it does
   * not include extra descriptor for learning if any)
   */
  uint32_t number_of_program_descriptors_required() const;

  /**
   * @brief Return the number of extra descriptors for a multipass program
   */
  uint32_t number_of_extra_program_descriptors_required() const;

  /**
   * @brief Return true if learning is executing on FNP3, false otherwise
   */
  bool learning_on_fnp3() const;

  /**
   * @brief Return the number of bytes required to program (it can be lower than
   * the total program size)
   */
  size_t program_data_required_memory() const;

  /**
   * @brief Return the number of bytes required by weights of FNP2s
   */
  size_t fnp2_required_memory() const;

  /**
   * @brief Return the serialized shifts vector, used to dequantize outputs
   */
  akida::span<int32_t> shifts() const;

  /**
   * @brief Return the serialized scales vector, used to dequantize outputs
   */
  akida::span<float> scales() const;

  /**
   * @brief Return the expected type of program inputs
   */
  fb::IoType inputs_type() const;

  /**
   * @brief Return the type of program outputs
   */
  fb::IoType outputs_type() const;

  /**
   * @brief Return the output bits of program outputs
   */
  uint8_t output_bits() const;

  /**
   * @brief Return true if a serialized program info buffer has been parsed,
   * false otherwise
   */
  bool is_valid() const;

  /**
   * @brief Return skip DMA store info tracks
   */
  std::vector<SkipDmaInfoTrack> skipdma_store_track(uint32_t pass_idx) const;

  /**
   * @brief Return skip DMA store info tracks
   */
  std::vector<SkipDmaInfoTrack> skipdma_load_track(uint32_t pass_idx) const;

  friend class DeviceProgrammer;

 protected:
  akida::span<uint8_t> program_data_;
  uint32_t program_data_address_;
  const fb::ProgramInfo* program_info_;
};

}  // namespace akida
