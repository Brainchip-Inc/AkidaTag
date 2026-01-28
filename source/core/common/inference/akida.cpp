#include "akida.h"
#include "akida/hardware_device.h"
#include "akida/input_conversion.h"
#include "akida/program_info.h"
#include "akida/tensor.h"
#include "error.h"
#include "io_objects.h"

uint8_t *current_program;
static bool current_learn_en = false;
static akida::ProgramInfo program_info = akida::ProgramInfo();
#define FLASH_BASE_ADDRESS 0x80000000

int akida_program(uint8_t *buffer, int size, bool learn_en) {
  if (current_program)
    akd_device.unprogram();
  /** Program model to mesh */
  current_program = buffer;
  program_info = akd_device.program(buffer, size);
  if (program_info.is_valid()) {
    if (current_learn_en != learn_en) {
      /** Set learn mode */
      akd_device.toggle_learn(learn_en);
      current_learn_en = learn_en;
    }
    return SUCCESS;
  }
  return -EFAILURE;
}

int akida_program_only(uint8_t *buffer, int size) {
  /** Program model to mesh */
  current_program = buffer;
  program_info = akd_device.program(buffer, size);
  if (program_info.is_valid()) {
    return SUCCESS;
  }
  return -EFAILURE;
}

int akida_program_flash(uint8_t *program_info, int len,
                        uint32_t flash_address) {
  current_program = program_info;
  auto info = akd_device.program_external_data(
      program_info, len, flash_address + FLASH_BASE_ADDRESS);
  if (info.is_valid()) {
    auto inputsz = info.input_dims();
    printk("input size: (%d, %d, %d)", inputsz[0], inputsz[1], inputsz[2]);
    (void)inputsz;
    return SUCCESS;
  }
  return -EFAILURE;
}

int akida_batch_size(int size, bool allocate_inputs) {
  return akd_device.set_batch_size(size, allocate_inputs);
}

int akida_learn_mode(bool enable) {
  if (current_learn_en != enable) {
    akd_device.toggle_learn(enable);
    current_learn_en = enable;
    if (current_learn_en != akd_device.learn_enabled())
      return -EFAILURE;
  }
  return SUCCESS;
}

int akida_forward(uint8_t *input, uint32_t *input_dims, uint8_t *output,
                  int output_size) {

  akida::TensorConstPtr in = akida::Dense::create_view(
      reinterpret_cast<const char *>(input), akida::TensorType::uint8,
      {input_dims[0], input_dims[1], input_dims[2]},
      akida::Dense::Layout::RowMajor);

  /** Execute inference */
  auto ret = akd_device.forward({in});

  if (ret.size()) {
    printk(" ret.size() %d inp shape %d %d %d\n", ret.size(), input_dims[0],
           input_dims[1], input_dims[2]);
    /** Get output buffer */
    auto out = akida::Tensor::ensure_dense(std::move(ret[0]));
    printk(" out->size() * sizeof(int) %d, output_size %d \n",
           (out->size() * sizeof(int)), output_size);
    if (out && (out->size() * sizeof(int)) == (size_t)output_size) {
      const unsigned char *bytes_out = (unsigned char *)out->buffer()->data();
      memcpy(output, bytes_out, output_size);
      return SUCCESS;
    }
  }
  return -EFAILURE;
}

void akida_fit(uint8_t *input, uint32_t *input_dims, int32_t *input_label) {
  /* Create a 4-D view and split into tensors */
  akida::Shape input_shape({1, input_dims[0], input_dims[1], input_dims[2]});
  auto input_tensor = akida::Dense::create_view(
      reinterpret_cast<const char *>(input), akida::TensorType::uint8,
      input_shape, akida::Dense::Layout::RowMajor);
  auto input_vector = akida::Dense::split(*input_tensor);
  std::vector<int> in_label = {*input_label};
  auto ret = akd_device.fit(input_vector, in_label);
}

int akida_enqueue(uint8_t *input, uint32_t *input_dims, int32_t *input_label) {
  int ret = 0;
  /* Create a view and split into tensors */
  akida::Shape input_shape({input_dims[0], input_dims[1], input_dims[2]});
  auto input_tensor = akida::Dense::create_view(
      reinterpret_cast<const char *>(input), akida::TensorType::uint8,
      input_shape, akida::Dense::Layout::RowMajor);

  if (input_label) {
    ret = akd_device.enqueue(*input_tensor, input_label);
  } else {
    ret = akd_device.enqueue(*input_tensor);
  }
  if (!ret) {
    return -EFAILURE;
  }
  return SUCCESS;
}

int akida_fetch(uint8_t *output, int output_size, bool dequantize) {
  akida::TensorUniquePtr output_ptr;
  output_ptr = akd_device.fetch();
  if (output_ptr) {
    if (true == dequantize) {
      auto dequantized_output =
          akd_device.dequantize(*akida::conversion::as_dense(*output_ptr));
      const float *bytes_out = dequantized_output->data<float>();
      memcpy(output, bytes_out, output_size);
    } else {
      /** Get output buffer buffer */
      auto out = akida::Tensor::ensure_dense(std::move(output_ptr));
      const unsigned char *bytes_out = (unsigned char *)out->buffer()->data();
      memcpy(output, bytes_out, output_size);
    }
  } else {
    printk("Fetch returned NULL pointer");
    return -EFAILURE;
  }
  return SUCCESS;
}

int akida_save_learn_weights(uint32_t *weights_ptr, uint32_t size) {
  /* The learn_mem_size function returns the size of the learned weights memory
   * in 32-bit units. To convert this to bytes, the returned value should be
   * multiplied by four */
  uint32_t layer_size = akd_device.learn_mem_size() * 4;
  if (layer_size <= size) {
    akd_device.learn_mem(weights_ptr);
    return (int)layer_size;
  } else {
    return -EFAILURE;
  }
}

uint32_t akida_learn_mem_size(void) {
  /* The learn_mem_size function returns the size of the learned weights memory
   * in 32-bit units. To convert this to bytes, the returned value should be
   * multiplied by four */
  return (akd_device.learn_mem_size() * 4);
}

int akida_update_learn_weights(const uint32_t *weights_ptr, uint32_t size) {
  /* The learn_mem_size function returns the size of the learned weights memory
   * in 32-bit units. To convert this to bytes, the returned value should be
   * multiplied by four */
  uint32_t layer_size = akd_device.learn_mem_size() * 4;
  if (layer_size == size) {
    akd_device.update_learn_mem(weights_ptr);
    return (int)layer_size;
  } else {
    return -EFAILURE;
  }
}

int32_t get_inferred_class(int32_t *result, int num_classes, int num_neurons) {
  int32_t max_val = 0, max_index = -1, n_activations = 0;
  n_activations = num_classes * num_neurons;
  if (num_neurons > 0) {
    for (int i = 0; i < n_activations; i++) {
      if (result[i] > max_val) {
        max_val = result[i];
        max_index = i;
      }
      printk("value at index %d is %d\n", i, result[i]);
    }
    return (max_index / num_neurons);
  }
  return -EFAILURE;
}