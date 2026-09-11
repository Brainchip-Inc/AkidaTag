#pragma once
#include <cstdint>

#include "akida/program_info.h"
#include "akida/tensor.h"
#include "infra/hardware_driver.h"

#include "dma_events.h"

namespace akida {

// convert tensor to dma events
DmaEventsPtr to_dma_events(const Tensor& inputs, bool input_is_fnp);

// Read events at the given memory address and reformat them to Tensor
TensorUniquePtr dma_events_read_outputs(HardwareDriver* driver,
                                        const uint32_t addr_output_events,
                                        const ProgramInfo& program_info);

}  // namespace akida
