#include "akida/program_info.h"

#include <cstring>

#include "akida/hw_version.h"
#include "akida/shape.h"
#include "akida/version.h"

#include "engine/dma.h"
#include "infra/system.h"

#include "flatbuffers/base.h"

#include "engine/akida_program_info_generated.h"
#include "engine/flatbuffers_utils.h"

namespace akida {

ProgramInfo::ProgramInfo()
    : program_data_{nullptr, 0},
      program_data_address_(0),
      program_info_(nullptr) {}

ProgramInfo::ProgramInfo(const uint8_t* serialized_program_info,
                         [[maybe_unused]] size_t program_info_size,
                         uint32_t program_data_address)
    : ProgramInfo() {
  if (!serialized_program_info) {
    panic("Program is null");
  }
  // Get the size of our program info flatbuffer
  const auto program_info_fb_size =
      get_flatbuffer_size(serialized_program_info);

  // 1st verify program info
  flatbuffers::Verifier program_info_verifier(serialized_program_info,
                                              program_info_fb_size);
  auto* program_info = fb::GetSizePrefixedProgramInfo(serialized_program_info);
  if (program_info == nullptr ||
      fb::VerifySizePrefixedProgramInfoBuffer(program_info_verifier) == false) {
    panic("Unable to parse program info");
  }

  // Check that the akida version this program was compiled with matches the
  // current version.
  const auto& program_version = program_info->version()->c_str();
  const auto& lib_version = version();
  if (strcmp(program_version, lib_version) != 0) {
    panic("Program version [%s] does not match library version [%s]",
          program_version, lib_version);
  }

  program_data_address_ = program_data_address;

  program_info_ = program_info;
}

ProgramInfo::ProgramInfo(const uint8_t* serialized_program, size_t program_size)
    : ProgramInfo(serialized_program, program_size, 0) {
  // In this constructor, serialized_program contains both program info &
  // program data. The program data are serialized right after program info.
  const auto program_info_size = get_flatbuffer_size(serialized_program);
  const auto* program_data_ptr = serialized_program + program_info_size;

  // Get our program data size
  const auto program_data_size = get_flatbuffer_size(program_data_ptr);

  // size of buffer should be equal to the sum of both flatbuffers size (+
  // size words)
  assert((program_info_size + program_data_size == program_size) &&
         "Unexpected program buffer size");

  // store info about the program parts
  program_data_ = {program_data_ptr, program_data_size};
}

HwVersion ProgramInfo::device_version() const {
  const auto* fb_device_version = program_info_->device_version();

  return HwVersion{
      fb_device_version->vendor_id(), fb_device_version->product_id(),
      fb_device_version->major_rev(), fb_device_version->minor_rev()};
}

const uint32_t* ProgramInfo::input_dims() const {
  return program_info_->input_dims()->data();
}

Shape ProgramInfo::output_dims() const {
  const auto& output_dims = *program_info_->output_dims();
  return Shape{output_dims[0], output_dims[1], output_dims[2]};
}

bool ProgramInfo::input_is_dense() const {
  return program_info_->input_type() == fb::IoType_dense;
}

bool ProgramInfo::output_is_dense() const {
  return program_info_->output_type() == fb::IoType_dense;
}

bool ProgramInfo::activation_enabled() const {
  return program_info_->activation();
}

uint32_t ProgramInfo::dense_input_window_width() const {
  return program_info_->dense_window_w();
}

uint32_t ProgramInfo::dense_input_window_height() const {
  return program_info_->dense_window_h();
}

bool ProgramInfo::can_learn() const {
  return program_info_->learning_layer() != nullptr;
}

uint32_t ProgramInfo::learn_weights_word_size() const {
  const auto* learning_layer = program_info_->learning_layer();
  if (learning_layer == nullptr) {
    return 0;
  } else {
    if (learning_layer->ram()->fnp2_track() != nullptr) {
      // if learning is on FNP2, return the word size of FNP2 track
      return learning_layer->ram()->fnp2_track()->track().word_size();
    } else {
      // return the word size of the np track
      assert(learning_layer->ram()->tracks()->size() == 1);
      return learning_layer->ram()->tracks()->Get(0)->word_size();
    }
  }
}

uint8_t ProgramInfo::number_of_descriptors_per_pass() const {
  return program_info_->max_num_desc();
}

uint32_t ProgramInfo::number_of_passes() const {
  return program_info_->passes()->size();
}

uint32_t ProgramInfo::number_of_program_descriptors_required() const {
  const auto nb_passes = number_of_passes();
  return nb_passes > 1 ? nb_passes * number_of_descriptors_per_pass()
                       : dma::kMinNbDescriptors;
}

uint32_t ProgramInfo::number_of_extra_program_descriptors_required() const {
  // There is an extra descriptor if leaning is running on FNP3 during a
  // multipass program
  return (number_of_passes() > 1 && learning_on_fnp3()) ? 1 : 0;
}

bool ProgramInfo::learning_on_fnp3() const {
  return program_info_->learning_layer() != nullptr &&
         program_info_->learning_layer()->ram()->tracks() != nullptr &&
         program_info_->learning_layer()->ram()->fnp2_track() == nullptr;
}

static inline size_t track_bytes_size(const fb::TrackSpan& track) {
  return track.word_size() * sizeof(uint32_t);
}

static size_t record_np_tracks_byte_size(const fb::RecordSpans& record) {
  size_t result = 0;
  // get size of np tracks
  for (const auto* np_track : *record.tracks()) {
    result += track_bytes_size(*np_track);
  }
  return result;
}

static size_t largest_np_track_byte_size(const fb::RecordSpans& record) {
  size_t result = 0;
  if (record.tracks() != nullptr) {
    for (const auto* np_track : *record.tracks()) {
      result = std::max(result, track_bytes_size(*np_track));
    }
  }
  return result;
}

size_t ProgramInfo::program_data_required_memory() const {
  size_t result = 0;
  const auto nb_passes = number_of_passes();
  if (nb_passes > 1) {
    // in multipass, all np tracks must be in memory
    for (const auto* pass : *program_info_->passes()) {
      for (const auto* record : *pass->records()) {
        result += record_np_tracks_byte_size(*record);
      }
    }
    // if there is learning records we need to count it as well
    const auto* learn = program_info_->learning_layer();
    if (learn) {
      // learn have both inference & learning tracks, plus weights
      result += track_bytes_size(*learn->inference_registers());
      result += track_bytes_size(*learn->learning_registers());
      result += record_np_tracks_byte_size(*learn->ram());
    }
  } else {
    // in single pass, tracks are played once at a time, so the required
    // memory is the size of the largest one
    const auto* pass = (*program_info_->passes())[0];
    for (const auto* record : *pass->records()) {
      result = std::max(result, largest_np_track_byte_size(*record));
    }
    // check if there are learning records
    const auto learn = program_info_->learning_layer();
    if (learn) {
      result =
          std::max(result, track_bytes_size(*learn->inference_registers()));
      result = std::max(result, track_bytes_size(*learn->learning_registers()));
      result = std::max(result, largest_np_track_byte_size(*learn->ram()));
    }
  }
  return result;
}

size_t ProgramInfo::fnp2_required_memory() const {
  size_t result = 0;

  // iterate over all records from all passes
  for (const auto* pass : *program_info_->passes()) {
    for (const auto* record : *pass->records()) {
      // check if we have FNP2 weights
      if (record->fnp2_track()) {
        result += track_bytes_size(record->fnp2_track()->track());
      }
    }
  }
  // check if there is learning records that could contain FNP2
  const auto learn = program_info_->learning_layer();
  if (learn) {
    // Only ram may contain an FNP2 track
    if (learn->ram()->fnp2_track()) {
      result += track_bytes_size(learn->ram()->fnp2_track()->track());
    }
  }
  return result;
}

akida::span<int32_t> akida::ProgramInfo::shifts() const {
  const auto* shifts_vector = program_info_->shifts();
  return {shifts_vector->data(), shifts_vector->size()};
}

akida::span<float> ProgramInfo::scales() const {
  const auto* scales_vector = program_info_->scales();
  return {scales_vector->data(), scales_vector->size()};
}

fb::IoType ProgramInfo::inputs_type() const {
  return program_info_->input_type();
}

fb::IoType ProgramInfo::outputs_type() const {
  return program_info_->output_type();
}

uint8_t ProgramInfo::output_bits() const {
  return program_info_->output_bits();
}

bool ProgramInfo::is_valid() const { return program_info_ != nullptr; }

std::vector<ProgramInfo::SkipDmaInfoTrack> ProgramInfo::skipdma_store_track(
    uint32_t pass_idx) const {
  std::vector<ProgramInfo::SkipDmaInfoTrack> skipdma_track{};
  if (program_info_->passes()->Get(pass_idx)->skipdma_store_track() !=
      nullptr) {
    const auto& skipdma_track_fb =
        *program_info_->passes()->Get(pass_idx)->skipdma_store_track();
    for (uint32_t idx = 0; idx < skipdma_track_fb.size(); idx++) {
      const auto* const skipdma_fb = skipdma_track_fb.Get(idx);
      np::Info info{};
      info.ident = hw::Ident{
          skipdma_fb->ident().col(), skipdma_fb->ident().row(),
          skipdma_fb->ident().id(), skipdma_fb->ident().channel_idx()};
      info.types = {hw::Type::SKIP_DMA_STORE};
      skipdma_track.push_back(
          {info, skipdma_fb->used_for_tnp_b(), skipdma_fb->skip_length(),
           skipdma_fb->skip_connect_id(), skipdma_fb->ob_32b_size()});
    }
  }
  return skipdma_track;
}

std::vector<ProgramInfo::SkipDmaInfoTrack> ProgramInfo::skipdma_load_track(
    uint32_t pass_idx) const {
  std::vector<ProgramInfo::SkipDmaInfoTrack> skipdma_track{};
  if (program_info_->passes()->Get(pass_idx)->skipdma_load_track() != nullptr) {
    const auto& skipdma_track_fb =
        *program_info_->passes()->Get(pass_idx)->skipdma_load_track();
    for (uint32_t idx = 0; idx < skipdma_track_fb.size(); idx++) {
      const auto* const skipdma_fb = skipdma_track_fb.Get(idx);
      np::Info info{};
      info.ident = hw::Ident{
          skipdma_fb->ident().col(), skipdma_fb->ident().row(),
          skipdma_fb->ident().id(), skipdma_fb->ident().channel_idx()};
      info.types = {hw::Type::SKIP_DMA_LOAD};
      skipdma_track.push_back(
          {info, skipdma_fb->used_for_tnp_b(), skipdma_fb->skip_length(),
           skipdma_fb->skip_connect_id(), skipdma_fb->ob_32b_size()});
    }
  }
  return skipdma_track;
}
}  // namespace akida
