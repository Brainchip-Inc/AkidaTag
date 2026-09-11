#include "dma_events_ops.h"

#include <cstddef>
#include <vector>

#include "akida/dense.h"
#include "akida/program_info.h"
#include "akida/shape.h"
#include "akida/sparse.h"
#include "akida/tensor.h"
#include "dma_cnp_events.h"
#include "dma_events_format.h"
#include "dma_fnp_events.h"
#include "dma_hrc_events.h"
#include "engine/akida_program_info_generated.h"
#include "engine/dma.h"
#include "engine/int_conversion.h"
#include "infra/registers_common.h"

#include "dma_desc_ops.h"

namespace akida {

static void set_cnp_event(Index x, Index y, Index z, uint8_t v,
                          dma::w32* evt_word1, dma::w32* evt_word2) {
  set_field(evt_word1, CONV_X, x);
  set_field(evt_word1, CONV_Y, y);
  set_field(evt_word2, CONV_F, z);
  set_field(evt_word2, CONV_ACTIVATION, v);
  // NOTE: polarity is not set
}

static void set_fnp_event(Index f, uint8_t v, dma::w32* evt_word1,
                          dma::w32* evt_word2) {
  set_field(evt_word1, FC_F, f);
  set_field(evt_word2, FC_ACTIVATION, v);
  set_field(evt_word2, FC_POLARITY, 1);
  // NOTE: the "fire" field is always set
}

static void add_dummy_event_fnp(const Tensor& inputs, dma::wbuffer* events) {
  // Add a dummy event for fnp, outside of dimensions with value 1. One index
  // out of the tensor in every dimension
  // (max coord is (shape[0] -1, shape[1] - 1, shape[2] - 1)
  // When null event forwarding, a dummy neuron is needed on NSoC-v2 and is
  // harmless on newer IP versions.
  const auto& shape = inputs.dimensions();
  auto strides = Dense::eval_strides(shape, Dense::Layout::ColMajor);
  auto index = static_cast<Index>(linear_index(shape.data(), strides));
  dma::w32 evt_words[2] = {0, 0};
  set_fnp_event(index, 1, &evt_words[0], &evt_words[1]);
  events->insert(events->end(), evt_words, evt_words + 2);
}

static void add_dummy_event_cnp(const Tensor& inputs, dma::wbuffer* events) {
  // Add a dummy event for cnp, outside of dimensions with value 1. One index
  // out of the tensor in every dimension
  // (max coord is (shape[0] -1, shape[1] - 1, shape[2] - 1)
  // When null event forwarding, a dummy neuron is needed on NSoC-v2 and is
  // harmless on newer IP versions.
  const auto& shape = inputs.dimensions();
  dma::w32 evt_words[2] = {0, 0};
  set_cnp_event(shape[0], shape[1], shape[2], 1, &evt_words[0], &evt_words[1]);
  events->insert(events->end(), evt_words, evt_words + 2);
}

static dma::wbuffer format_cnp_events(const Sparse& inputs) {
  auto nbevents = inputs.size();
  dma::wbuffer events(dma::kSparseEventWordSize * nbevents, 0);

  auto events_it = inputs.begin();
  for (size_t i = 0; i < nbevents; i++) {
    // map event words
    auto& evt_word1 = events[i * dma::kSparseEventWordSize];
    auto& evt_word2 = events[i * dma::kSparseEventWordSize + 1];

    auto v = events_it->value<uint8_t>();
    auto coords = events_it->coords();
    set_cnp_event(coords[0], coords[1], coords[2], v, &evt_word1, &evt_word2);
    events_it->next();
  }
  return events;
}

static dma::wbuffer format_fnp_events_3D(const Sparse& inputs) {
  auto nbevents = inputs.size();
  dma::wbuffer events(dma::kSparseEventWordSize * nbevents, 0);

  // Evaluate the strides of the inputs following a col-major convention,
  // because the hardware linearizes the inputs using that convention
  auto strides =
      Dense::eval_strides(inputs.dimensions(), Dense::Layout::ColMajor);
  auto events_it = inputs.begin();
  for (size_t i = 0; i < nbevents; i++) {
    // map event words
    auto& evt_word1 = events[i * dma::kSparseEventWordSize];
    auto& evt_word2 = events[i * dma::kSparseEventWordSize + 1];

    auto v = events_it->value<uint8_t>();
    auto c = static_cast<Index>(events_it->unravel(strides));
    set_fnp_event(c, v, &evt_word1, &evt_word2);
    events_it->next();
  }
  return events;
}

static dma::wbuffer format_fnp_events(const Sparse& inputs) {
  auto shape = inputs.dimensions();
  if ((shape.size() == 3) && ((shape[0] != 1) || (shape[1] != 1))) {
    // non-flat FNP inputs should be formatted as CNP, but if channel do not fit
    // on 11 bits, they need to be formatted as FNP and then we need to flatten
    // them
    assert(shape[2] > (1u << CONV_F.nb_bits) && "channels should be > 2048");
    return format_fnp_events_3D(inputs);
  }

  auto nbevents = inputs.size();
  dma::wbuffer events(dma::kSparseEventWordSize * nbevents, 0);

  auto events_it = inputs.begin();
  for (size_t i = 0; i < nbevents; i++) {
    // map event words
    auto& evt_word1 = events[i * dma::kSparseEventWordSize];
    auto& evt_word2 = events[i * dma::kSparseEventWordSize + 1];

    auto v = events_it->value<uint8_t>();
    // For a flat input, we only care about the last coordinate
    auto c = events_it->coords()[2];
    set_fnp_event(c, v, &evt_word1, &evt_word2);
    events_it->next();
  }
  return events;
}

static dma::wbuffer format_cnp_events(const Dense& inputs) {
  // We don't know how many events the Dense contains, but assume it is full
  dma::wbuffer events;
  events.reserve(dma::kSparseEventWordSize * inputs.size());

  auto shape = inputs.dimensions();
  auto w = shape[0];
  auto h = shape[1];
  auto c = shape[2];
  auto values_ptr = inputs.data<uint8_t>();
  for (Index x = 0; x < w; ++x) {
    for (Index y = 0; y < h; ++y) {
      for (Index z = 0; z < c; ++z) {
        // Extract value for the current coordinate
        auto value = *values_ptr;
        if (*values_ptr > 0) {
          dma::w32 evt_words[2] = {0, 0};
          set_cnp_event(x, y, z, value, &evt_words[0], &evt_words[1]);
          events.insert(events.end(), evt_words, evt_words + 2);
        }
        ++values_ptr;
      }
    }
  }
  return events;
}

static dma::wbuffer format_fnp_events(const Dense& inputs) {
  // We don't know how many events the Dense contains, but assume it is full
  dma::wbuffer events;
  events.reserve(dma::kSparseEventWordSize * inputs.size());

  auto values = inputs.data<uint8_t>();
  // For a flat input, we don't care about the coordinates
  for (Index i = 0; i < inputs.size(); ++i) {
    // Extract value for the current index
    auto value = values[i];
    if (value > 0) {
      dma::w32 evt_words[2] = {0, 0};
      set_fnp_event(i, value, &evt_words[0], &evt_words[1]);
      events.insert(events.end(), evt_words, evt_words + 2);
    }
  }
  return events;
}

static dma::wbuffer format_cnp_events(const Tensor& inputs) {
  // Assume the tensor is Sparse
  auto sparse = dynamic_cast<const Sparse*>(&inputs);
  if (sparse) {
    return format_cnp_events(*sparse);
  }
  return format_cnp_events(dynamic_cast<const Dense&>(inputs));
}

static dma::wbuffer format_fnp_events(const Tensor& inputs) {
  // Assume the tensor is Sparse
  auto sparse = dynamic_cast<const Sparse*>(&inputs);
  if (sparse) {
    return format_fnp_events(*sparse);
  }
  return format_fnp_events(dynamic_cast<const Dense&>(inputs));
}

using read_data_func = void (*)(int32_t*, const std::vector<uint32_t>&,
                                dma::w32, dma::w32);

/**
 * Extract the potentials from a DMA word buffer and build an int32 Dense tensor
 */
static TensorUniquePtr format_output_potentials(
    const dma::wbuffer& output_words, const Shape& out_dims,
    const ProgramInfo& program_info) {
  // We use lambdas to distinguish between the three potentials output formats
  read_data_func read_data;

  switch (program_info.outputs_type()) {
    case fb::IoType_fnp_sparse:
      read_data = [](int32_t* output, const std::vector<uint32_t>&,
                     dma::w32 word1, dma::w32 word2) {
        // with FullyConnected, output is (1, 1, F) so the index is just the
        // channel
        const auto index = static_cast<Index>(get_field(word1, FC_F));
        output[index] = intN_to_int32<26>(get_field(word2, FC_ACTIVATION));
      };
      break;

    case fb::IoType_hrc_sparse:
      read_data = [](int32_t* output, const std::vector<uint32_t>& strides,
                     dma::w32 word1, dma::w32 word2) {
        // get index from coordinate
        std::array<Index, 3> coords;
        coords[0] = static_cast<Index>(get_field(word1, CONV_X));
        coords[1] = static_cast<Index>(get_field(word1, CONV_Y));
        coords[2] = static_cast<Index>(get_field(word2, CONV_F));
        const auto index = linear_index(coords.data(), strides);

        // get potential value
        // The potential 4 MSB (including the sign) are on word 1
        auto pot_msb = intN_to_int32<4>(get_field(word1, CONV_POTENTIAL_MSB));
        // And the 20 LSB (positive number only) are on word 2
        auto pot_lsb =
            static_cast<int32_t>(get_field(word2, CONV_POTENTIAL_LSB));
        // write value
        output[index] = (pot_msb << CONV_POTENTIAL_LSB.nb_bits) | pot_lsb;
      };
      break;

    case fb::IoType_cnp_sparse:
      read_data = [](int32_t* output, const std::vector<uint32_t>& strides,
                     dma::w32 word1, dma::w32 word2) {
        // get index from coordinate
        std::array<Index, 3> coords;
        coords[0] = static_cast<Index>(get_field(word1, CONV_X));
        coords[1] = static_cast<Index>(get_field(word1, CONV_Y));
        coords[2] = static_cast<Index>(get_field(word2, CONV_F));
        const auto index = linear_index(coords.data(), strides);

        // write potential value
        output[index] = intN_to_int32<20>(get_field(word2, CONV_POTENTIAL_LSB));
      };
      break;

    default:
      panic("Unexpected output type");
  }
  // Allocate the output dense buffer
  auto dense =
      Dense::create(TensorType::int32, out_dims, Dense::Layout::RowMajor);
  // Iterate over DMA events to fill the dense buffer
  uint32_t n_events =
      static_cast<uint32_t>(output_words.size() / dma::kSparseEventWordSize);
  // Get pointer to dense, to avoid using set() method that calls data() and
  // then check_type() method at every iteration
  auto dense_ptr = dense->data<int32_t>();
  for (uint32_t i = 0; i < n_events; i++) {
    // Events are stored in two consecutive words
    const auto& output_word1 = output_words[i * dma::kSparseEventWordSize];
    const auto& output_word2 = output_words[i * dma::kSparseEventWordSize + 1];
    // Read the output coords and value
    read_data(dense_ptr, dense->strides(), output_word1, output_word2);
  }
  return dense;
}

DmaEventsPtr to_dma_events(const Tensor& inputs, bool input_is_fnp) {
  dma::wbuffer event_data;
  DmaEventsPtr result;
  dma::wbuffer::size_type nb_words;
  if (input_is_fnp) {
    event_data = format_fnp_events(inputs);
    // workaround when null event
    if (event_data.empty()) {
      add_dummy_event_fnp(inputs, &event_data);
    }
    nb_words = event_data.size();
    result = std::make_unique<DmaFnpEvents>(inputs.dimensions(),
                                            std::move(event_data));
  } else {
    event_data = format_cnp_events(inputs);
    // workaround when null event
    if (event_data.empty()) {
      add_dummy_event_cnp(inputs, &event_data);
    }
    nb_words = event_data.size();
    result = std::make_unique<DmaCnpEvents>(inputs.dimensions(),
                                            std::move(event_data));
  }
  // check we are below the maximum number of events
  auto max_dma_events = dma::max_dma_events();
  if (nb_words > max_dma_events * dma::kSparseEventWordSize) {
    panic("%d exceeds the maximum number of events: %d", nb_words,
          max_dma_events);
  }
  return result;
}

TensorUniquePtr dma_events_read_outputs(HardwareDriver* driver,
                                        const uint32_t addr_output_events,
                                        const ProgramInfo& program_info) {
  const auto output_dimensions = program_info.output_dims();

  // The header is the first word of the output
  uint32_t header = driver->read32(addr_output_events);
  uint32_t output_word_size = get_field(header, OUTPUT_WORD_SIZE);
  auto output_bytes_size = output_word_size * sizeof(dma::w32);
  const auto output_bits = program_info.output_bits();
  // adjust past header to data
  uint32_t read_offset_addr = addr_output_events + dma::kOutputHeaderByteSize;
  // Read back data aligned on 32-bit words
  dma::wbuffer output_events(output_word_size);
  switch (program_info.outputs_type()) {
    case fb::IoType_cnp_sparse:
      if (program_info.activation_enabled()) {
        driver->read(read_offset_addr, output_events.data(), output_bytes_size);
        // We can directly wrap the output events in a DmaCnpEvents sparse
        // tensor
        return std::make_unique<DmaCnpEvents>(output_dimensions,
                                              std::move(output_events));
      }
      // Handle potential output case here
      driver->read(read_offset_addr, output_events.data(), output_bytes_size);
      // Extract potentials and create a Dense
      return format_output_potentials(output_events, output_dimensions,
                                      program_info);

    case fb::IoType_fnp_sparse:
      if (program_info.activation_enabled()) {
        driver->read(read_offset_addr, output_events.data(), output_bytes_size);
        // We can directly wrap the output events in a DmaFnpEvents sparse
        // tensor
        return std::make_unique<DmaFnpEvents>(output_dimensions,
                                              std::move(output_events));
      }
      // Handle potential output case here
      driver->read(read_offset_addr, output_events.data(), output_bytes_size);
      // Extract potentials and create a Dense
      return format_output_potentials(output_events, output_dimensions,
                                      program_info);

    case fb::IoType_hrc_sparse:
      if (program_info.activation_enabled()) {
        driver->read(read_offset_addr, output_events.data(), output_bytes_size);
        // We can directly wrap the output events in a DmaHrcEvents sparse
        // tensor
        return std::make_unique<DmaHrcEvents>(output_dimensions,
                                              std::move(output_events));
      }
      // Handle potential output case here
      driver->read(read_offset_addr, output_events.data(), output_bytes_size);
      // Extract potentials and create a Dense
      return format_output_potentials(output_events, output_dimensions,
                                      program_info);

    case fb::IoType_dense: {
      TensorUniquePtr dense;
      size_t out_size = 0;

      // Output format depends on output_bits.
      if (output_bits < 8) {
        out_size = shape_size(output_dimensions) * sizeof(uint8_t);
        dense = Dense::create(TensorType::uint8, output_dimensions,
                              Dense::Layout::RowMajor);
      } else if (output_bits == 8) {
        out_size = shape_size(output_dimensions) * sizeof(int8_t);
        dense = Dense::create(TensorType::int8, output_dimensions,
                              Dense::Layout::RowMajor);
      } else {
        out_size = shape_size(output_dimensions) * sizeof(int32_t);
        dense = Dense::create(TensorType::int32, output_dimensions,
                              Dense::Layout::RowMajor);
      }
      auto* potentials = dense->buffer()->data();
      driver->read(read_offset_addr, potentials, out_size);
      return dense;
    }
    default:
      // Final catch-all for any unsupported types
      panic("Unsupported output type");
  }
}

}  // namespace akida
