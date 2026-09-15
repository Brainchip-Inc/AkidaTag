#pragma once

#include <cstdint>
#include <vector>

#include "akida/hardware_device.h"

namespace akida {
namespace dma {

using w32 = uint32_t;
using wbuffer = std::vector<w32>;

// Many operations require address alignment to 32 bit.
// Inputs and outputs for all inbound buffers for DMA controllers (except for
// HRC, that can be just byte aligned), and for all outbound buffers used by DMA
// controllers.
inline constexpr uint32_t kAlignment = sizeof(addr);
inline constexpr uint32_t kSkipDmaAlignment = 4 * sizeof(addr);
// Sparse tensors use 2 words per item
inline constexpr uint32_t kSparseEventWordSize = 2;
inline constexpr size_t kSparseEventByteSize =
    kSparseEventWordSize * sizeof(dma::w32);
// Output from DMA has a header
inline constexpr uint32_t kOutputHeaderByteSize = 0x20;

inline constexpr uint32_t kMinNbDescriptors = 2;
inline constexpr uint32_t kMaxNbDescriptors = 256;
// The number of descriptors to program skipdma external memory
inline constexpr uint8_t kSkipDmaStoreNbExtraDescriptors = 4;
inline constexpr uint8_t kSkipDmaLoadNbExtraDescriptors = 1;
}  // namespace dma
}  // namespace akida
