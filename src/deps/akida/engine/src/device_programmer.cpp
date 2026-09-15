#include "device_programmer.h"

#include <cstdint>

#include "akida/program_info.h"

#include "engine/akida_program_info_generated.h"
#include "engine/dma.h"
#include "engine/dma_config_ops.h"

#include "dma_config_mem_rw.h"
#include "dma_engine_ops.h"
#include "fnp2_mem_conf_reg.h"
#include "hardware_device_impl.h"
#include "memory_utils.h"
#include "multipass_memory.h"
#include "skipdma_ops.h"

namespace akida {

DeviceProgrammer::DeviceProgrammer(const ProgramInfo& program_info,
                                   HardwareDeviceImpl* device)
    : program_info_(program_info), device_(device) {}

DeviceProgrammer::TrackedSpan DeviceProgrammer::write_on_device_if_required(
    const fb::TrackSpan& track_span) {
  TrackedSpan result;
  if (program_info_.program_data_address_ != 0) {
    // We have no local address in case program was not written by engine
    result.local_address = nullptr;
    result.device_address =
        program_info_.program_data_address_ + track_span.offset_address();
  } else {
    // Put our local program data to device
    result.local_address =
        program_info_.program_data_.data + track_span.offset_address();
    const auto track_bytes_size = track_span.word_size() * sizeof(uint32_t);
    result.device_address =
        device_->external_mem()->track_and_put_on_device_if_required(
            result.local_address, track_bytes_size);
  }
  return result;
}

static void register_fnp2_track(HardwareDriver* driver,
                                dma::addr address_on_device,
                                const fb::FNP2TrackInfo& track_info) {
  // Now write external address used for this NP in the dedicated conf register
  // Note that there are 4 registers where the weights adress can be stored.
  // This works because currently existing mesh designs only contain one node
  // with 4 FNPs. Each NP will use the content of the register indexed by the ID
  // of the NP.
  // If at some point a mesh is created with a different layout, this might
  // raise an issue.
  // Also, this means that in multipass this register can only be used once per
  // program, and the FNP2 cannot be reused later, because that would require
  // updating the register value with another address, and there is no way to do
  // that.
  const auto np_id = track_info.np().id();
  auto fnp2_mem_conf_reg_addr =
      fnp2_memory_conf(driver->top_level_reg(), np_id);
  driver->write32(fnp2_mem_conf_reg_addr, address_on_device);
}

static inline uint32_t epg_reg_base(const uint32_t top_level_reg_base) {
  constexpr uint32_t EPG_REG_BASE = 0x00040000;
  return top_level_reg_base + EPG_REG_BASE;
}

static void play_epg_track(HardwareDriver* driver, uint32_t epg_base,
                           uint32_t address, uint32_t data) {
  driver->write32(epg_base + address, data);
}

static void play_epg(HardwareDeviceImpl* device,
                     const fb::ProgramInfo* program_info) {
  // Apply EPG program
  const auto* epg_tracks = program_info->epg_tracks();
  if (epg_tracks) {
    auto epg_tracks_size = epg_tracks->size();
    auto driver = device->driver();
    auto epg_base = epg_reg_base(driver->top_level_reg());
    for (uint32_t i = 0; i < epg_tracks_size; i++) {
      const auto& epg_track = epg_tracks->Get(i);
      play_epg_track(driver, epg_base, epg_track->register_offset(),
                     epg_track->value());
    }
  }
}

void DeviceProgrammer::program_single_pass() {
  const auto* program_info_fb = program_info_.program_info_;
  assert(program_info_fb->passes()->size() == 1);
  const auto* records_spans = program_info_fb->passes()->Get(0)->records();

  // play all records
  for (uint32_t i = 0; i < records_spans->size(); i++) {
    program_record(*records_spans->Get(i), true);
  }

  const auto* learn = program_info_fb->learning_layer();
  if (learn) {
    // In single pass, we just play the correct register track, depending on
    // learning activated or not, then play the weights record
    const auto* registers_span = device_->learn_enabled()
                                     ? learn->learning_registers()
                                     : learn->inference_registers();
    program_track(*registers_span, true);
    program_record(*learn->ram(), true);
  }
  play_epg(device_, program_info_.program_info_);
}

static void write_dummy_descs(HardwareDeviceImpl* device, dma::addr dummy_input,
                              dma::addr dummy_output,
                              uint32_t num_dummy_descs) {
  // Dummy descriptor is a read of size 1. Descriptor size is the header size
  auto dummy_desc =
      dma::format_config_desc(dma::kDescConfigDirectionRead, dummy_input,
                              dummy_output, dma::kConfigNpHeaderWordSize);
  for (uint32_t j = 0; j < num_dummy_descs; j++) {
    dma::enqueue_descriptor(device->driver(), device->dma_config().engine,
                            dummy_desc);
  }
}

static void generate_reading_descriptor_from_np_track(
    HardwareDeviceImpl* device, const uint32_t track_addr) {
  const auto input_addr = track_addr;
  const auto output_addr = track_addr + dma::kConfigNpHeaderByteSize;

  // format descriptor
  const auto descriptor =
      dma::format_config_desc(dma::kDescConfigDirectionRead, input_addr,
                              output_addr, dma::kConfigNpHeaderWordSize);
  // enqueue it at extra descriptor location
  dma::enqueue_extra_descriptor(device->driver(), device->dma_config(),
                                descriptor);
}

void DeviceProgrammer::program_multi_pass(MultiPassMemory* multipass_memory) {
  const auto* program_info_fb = program_info_.program_info_;
  const auto* learn = program_info_fb->learning_layer();
  const auto& passes = *program_info_fb->passes();
  uint32_t passes_size = passes.size();
  // In multi pass mode, there will always be at least 2 passes
  assert(passes_size >= 2);

  // estimate memory required to hold passes descriptors.
  const auto max_num_desc_pass = program_info_.number_of_descriptors_per_pass();

  // use program to allocate dummy config, input and output space
  const auto dummy_input =
      write_on_device_if_required(*program_info_fb->dummy_header())
          .device_address;

  uint32_t np_tracks_played = 0;

  // now that we have the memory, we can fill the descriptors
  for (uint32_t i = 0; i < passes_size; i++) {
    const auto& layer_records = *passes[i]->records();
    uint32_t records_size = layer_records.size();
    np_tracks_played = 0;
    for (uint32_t j = 0; j < records_size; j++) {
      const auto* record_span = layer_records[j];

      // get number of NP tracks (corresponding to number of DMA descriptors).
      uint32_t np_tracks_size = record_span->tracks()->size();
      program_record(*record_span, false);
      np_tracks_played += np_tracks_size;
    }
    constexpr size_t effective_batch_size{1};
    constexpr bool is_single_pass = false;
    constexpr bool is_pipeline = false;
    uint32_t ndesc = skipdma::program_ext_mem(
        program_info_, device_->dma_config(), device_->mem(),
        device_->external_mem(), device_->driver(), effective_batch_size,
        is_pipeline, i, is_single_pass, device_->skip_dma_mem_info());
    np_tracks_played += ndesc;
    if (i == passes_size - 1 && learn) {
      const auto* inference_registers = learn->inference_registers();
      const auto* learn_registers = learn->learning_registers();
      const auto* ram = learn->ram();

      // number of descriptors generated is always 1 + another one if ram has NP
      // track
      auto np_tracks_size = 1 + (ram->tracks() ? ram->tracks()->size() : 0);

      auto learn_desc_address = program_track(*inference_registers, false);
      // store the address of descriptor that correspond to the learning layer
      // registers because we will need to edit this descriptor to make it point
      // to the learning registers or inference registers when enable/disable
      // learning
      multipass_memory->update_learn_descriptor_addr(learn_desc_address);
      write_on_device_if_required(*learn_registers);
      program_record(*ram, false);
      np_tracks_played += np_tracks_size;
    }

    // fill unused pass descriptors with "dummy" descriptors for this pass
    assert(max_num_desc_pass >= np_tracks_played);
    uint32_t num_dummy_descs = max_num_desc_pass - np_tracks_played;
    write_dummy_descs(device_, dummy_input, multipass_memory->dummy_output_addr,
                      num_dummy_descs);
  }

  // Add an extra descriptor to copy the learned memory
  if (program_info_.learning_on_fnp3()) {
    assert(learn->ram()->fnp2_track() == nullptr &&
           "Learning should be on FNP3, so it should not have FNP2 track");
    const auto* tracks = learn->ram()->tracks();
    assert(
        tracks != nullptr && tracks->size() == 1 &&
        "Learning should use a single NP, so there should be a single track");
    generate_reading_descriptor_from_np_track(
        device_, get_address_from_offset(tracks->Get(0)->offset_address()));
  }
  play_epg(device_, program_info_.program_info_);
}

static void rewind_fnp2_track(HardwareDeviceImpl* device,
                              const uint8_t* program_base,
                              const fb::FNP2TrackInfo& track) {
  device->external_mem()->release(program_base +
                                  track.track().offset_address());
}

static void rewind_np_track(HardwareDeviceImpl* device,
                            const uint8_t* program_base,
                            const fb::TrackSpan& track, bool multi_pass) {
  if (multi_pass) {
    // in multi pass, free config header allocated with track as id
    device->external_mem()->release(program_base + track.offset_address());
  }
}

static void rewind_record(HardwareDeviceImpl* device,
                          const uint8_t* program_base,
                          const fb::RecordSpans& record, bool multi_pass) {
  // rewind fnp2 track if it is there
  const auto* fnp2_track = record.fnp2_track();
  if (fnp2_track) {
    rewind_fnp2_track(device, program_base, *fnp2_track);
  }
  // rewind all normal tracks
  const auto* np_tracks = record.tracks();
  if (np_tracks != nullptr) {
    for (auto np_track = np_tracks->rbegin(); np_track != np_tracks->rend();
         ++np_track) {
      rewind_np_track(device, program_base, **np_track, multi_pass);
    }
  }
}

void DeviceProgrammer::unprogram() {
  if (program_info_.program_data_address_ == 0) {
    const auto* program_info_fb = program_info_.program_info_;
    const auto* program_base = program_info_.program_data_.data;

    const auto& passes = *program_info_fb->passes();
    bool multi_pass = passes.size() > 1;

    const auto learn = program_info_fb->learning_layer();
    if (learn) {
      if (multi_pass) {
        // in multi pass, both learning & inference registers are written to the
        // device
        rewind_np_track(device_, program_base, *learn->learning_registers(),
                        multi_pass);
        rewind_np_track(device_, program_base, *learn->inference_registers(),
                        multi_pass);
      } else {
        if (device_->learn_enabled()) {
          rewind_np_track(device_, program_base, *learn->learning_registers(),
                          multi_pass);
        } else {
          rewind_np_track(device_, program_base, *learn->inference_registers(),
                          multi_pass);
        }
      }
      rewind_record(device_, program_base, *learn->ram(), multi_pass);
    }

    // rewind in reverse order
    for (auto pass = passes.rbegin(); pass != passes.rend(); ++pass) {
      const auto& layer_records = *pass->records();
      for (auto record = layer_records.rbegin(); record != layer_records.rend();
           ++record) {
        rewind_record(device_, program_base, **record, multi_pass);
      }
    }

    if (multi_pass) {
      // free up dummy config header
      device_->external_mem()->release(
          program_base +
          program_info_.program_info_->dummy_header()->offset_address());
    }
  }
}

void DeviceProgrammer::configure_learning_mode_single_pass(bool learn_enabled) {
  const auto* program_info_fb = program_info_.program_info_;
  assert(program_info_fb->passes()->size() == 1);

  const auto learn = program_info_fb->learning_layer();
  assert(learn);
  const auto& new_register = learn_enabled ? *learn->learning_registers()
                                           : *learn->inference_registers();
  // we can just play the new track, there is no need to rewind anything in
  // single pass
  program_track(new_register, true);
}

static void write_np_track_descriptor(HardwareDriver* driver,
                                      dma::addr track_addr_on_device,
                                      uint32_t track_word_size,
                                      dma::addr descriptor_address) {
  // format descriptor
  constexpr uint32_t output_addr = 0;  // not used for write
  auto descriptor = dma::format_config_desc(dma::kDescConfigDirectionWrite,
                                            track_addr_on_device, output_addr,
                                            track_word_size);
  // write descriptor in its place
  driver->write(descriptor_address, descriptor.data(),
                descriptor.size() * sizeof(dma::Descriptor::value_type));
}

void DeviceProgrammer::configure_learning_mode_multi_pass(
    const MultiPassMemory& multipass_memory, bool learn_enabled) {
  const auto* program_info_fb = program_info_.program_info_;
  assert(program_info_fb->passes()->size() > 1);
  // This function will edit the descriptor at learn_descriptor_addr to make it
  // point to either learning or inference registers
  assert(multipass_memory.learn_descriptor_addr != 0);
  const auto learn = program_info_fb->learning_layer();
  assert(learn);

  // get the correct track depending on learning
  const auto* registers_track = learn_enabled ? learn->learning_registers()
                                              : learn->inference_registers();
  const auto registers_address =
      get_address_from_offset(registers_track->offset_address());

  // Overwrite the descriptor
  write_np_track_descriptor(device_->driver(), registers_address,
                            registers_track->word_size(),
                            multipass_memory.learn_descriptor_addr);
}

void DeviceProgrammer::update_learn_memory(const uint32_t* ram_dump) {
  auto learning_layer = program_info_.program_info_->learning_layer();
  if (!learning_layer) {
    panic(
        "Learn memory update requires a device programmed with learning "
        "layers");
  }

  // detect ram size
  auto size = program_info_.learn_weights_word_size();
  // detect if FNP2 or FNP3
  auto record = learning_layer->ram();
  assert(record);
  auto fnp2_track = record->fnp2_track();
  if (fnp2_track) {
    // get memory address for this track
    auto mem_addr =
        get_address_from_offset(fnp2_track->track().offset_address());
    // update memory
    device_->driver()->write(mem_addr, ram_dump, size * sizeof(uint32_t));
  } else {
    // generate config dma header from program data
    uint32_t header[2] = {0};
    auto np = learning_layer->np();
    hw::Ident ident{np->col(), np->row(), np->id()};
    dma::format_config_header(header, ident, dma::Target::FnpWeights,
                              size - dma::kConfigNpHeaderWordSize, 0);

    // check that program info based header matches the header generated
    // by get_learn_memory
    if (memcmp(header, ram_dump,
               akida::dma::kConfigNpHeaderWordSize * sizeof(dma::w32))) {
      panic("weigths header do not match model ");
    }

    // now do transfer
    dma::dma_config_write(ram_dump, size, device_->dma_config(),
                          device_->external_mem(), device_->driver(), true);
  }
}

void DeviceProgrammer::get_learn_memory(uint32_t* ram_dump) {
  const auto* program_info_fb = program_info_.program_info_;
  auto learning_layer = program_info_fb->learning_layer();
  if (!learning_layer) {
    panic("Learn memory retrieval requires a program from learning layers");
  }

  // detect if FNP2 or FNP3
  auto record = learning_layer->ram();
  assert(record);
  auto fnp2_track = record->fnp2_track();
  if (fnp2_track) {
    // With FNP2, we can directly read in the program memory
    auto mem_addr =
        get_address_from_offset(fnp2_track->track().offset_address());
    device_->driver()->read(mem_addr, ram_dump,
                            fnp2_track->track().word_size() * sizeof(dma::w32));
  } else {
    // Get the weights size
    const auto* tracks = record->tracks();
    assert(tracks->size() == 1);
    const auto* learn_weights_track = tracks->Get(0);
    // In multi pass we can directly read in the program.
    if (program_info_.number_of_passes() > 1) {
      auto ram_addr =
          get_address_from_offset(learn_weights_track->offset_address());
      device_->driver()->read(
          ram_addr, ram_dump,
          learn_weights_track->word_size() * sizeof(dma::w32));
    } else {
      // in single pass when record is FNP3: read SRAM
      auto np = learning_layer->np();
      hw::Ident ident{np->col(), np->row(), np->id()};
      const auto size =
          learn_weights_track->word_size() - dma::kConfigNpHeaderWordSize;
      memset(ram_dump, 0,
             akida::dma::kConfigNpHeaderWordSize * sizeof(dma::w32));
      dma::format_config_header(ram_dump, ident, dma::Target::FnpWeights, size,
                                0);
      dma::dma_config_read(ram_dump + dma::kConfigNpHeaderWordSize, size,
                           ram_dump, device_->dma_config(), device_->mem(),
                           device_->driver(), true);
    }
  }
}

dma::addr DeviceProgrammer::get_address_from_offset(dma::addr offset) {
  if (program_info_.program_data_address_ != 0) {
    return program_info_.program_data_address_ + offset;
  } else {
    return device_->external_mem()->tracked(program_info_.program_data_.data +
                                            offset);
  }
}

dma::addr DeviceProgrammer::program_track(const fb::TrackSpan& track_span,
                                          bool single_pass) {
  // write track data & put buffer on device if needed, and get its address
  const auto tracked_span = write_on_device_if_required(track_span);

  // generate descriptor for the track
  const auto descriptor = dma::format_config_desc(
      dma::kDescConfigDirectionWrite, tracked_span.device_address, 0,
      track_span.word_size());
  // enqueue descriptor
  const auto descriptor_address = dma::enqueue_descriptor(
      device_->driver(), device_->dma_config().engine, descriptor);

  if (single_pass) {
    // in single pass, we need to wait for descriptor to complete
    dma::wait_config_dma_descriptor_complete(device_->driver(),
                                             device_->dma_config());
    // then release track data if it was allocated
    if (tracked_span.local_address != nullptr) {
      device_->external_mem()->release(tracked_span.local_address);
    }
  }
  return descriptor_address;
}

dma::addr DeviceProgrammer::program_record(const fb::RecordSpans& record_spans,
                                           bool single_pass) {
  dma::addr descriptor_address = 0;
  // play all np tracks
  const auto* tracks_spans = record_spans.tracks();
  if (tracks_spans != nullptr) {
    for (const auto* track_span : *tracks_spans) {
      descriptor_address = program_track(*track_span, single_pass);
    }
  }

  // play FNP2 track if there is one
  const auto* fnp2_track = record_spans.fnp2_track();
  if (fnp2_track != nullptr) {
    const auto tracked_span = write_on_device_if_required(fnp2_track->track());
    register_fnp2_track(device_->driver(), tracked_span.device_address,
                        *fnp2_track);
  }

  return descriptor_address;
}

}  // namespace akida
