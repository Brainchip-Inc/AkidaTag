#pragma once

#include "akida/hardware_device.h"
#include "akida/program_info.h"

#include <cstdint>

namespace akida {

// Forward declarations
class HardwareDeviceImpl;
struct MultiPassMemory;

namespace fb {
struct RecordSpans;
struct TrackSpan;
}  // namespace fb

class DeviceProgrammer {
 public:
  explicit DeviceProgrammer(const ProgramInfo& program_info,
                            HardwareDeviceImpl* device);

  void program_single_pass();
  void program_multi_pass(MultiPassMemory* multipass_memory);
  void unprogram();
  void configure_learning_mode_single_pass(bool learn_enabled);
  void configure_learning_mode_multi_pass(
      const MultiPassMemory& multipass_memory, bool learn_enabled);
  void update_learn_memory(const uint32_t* ram_dump);
  void get_learn_memory(uint32_t* ram_dump);

 private:
  struct TrackedSpan {
    const uint8_t* local_address = nullptr;
    dma::addr device_address = 0;
  };

  dma::addr get_address_from_offset(dma::addr offset);
  dma::addr program_track(const fb::TrackSpan& track_span, bool single_pass);
  dma::addr program_record(const fb::RecordSpans& record_spans,
                           bool single_pass);
  TrackedSpan write_on_device_if_required(const fb::TrackSpan& track_span);

  const ProgramInfo& program_info_;
  HardwareDeviceImpl* device_;
};

}  // namespace akida
