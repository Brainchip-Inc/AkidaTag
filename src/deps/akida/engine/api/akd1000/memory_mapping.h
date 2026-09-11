#pragma once

#include <cstdint>

namespace akida {
namespace soc {
namespace akd1000 {
// NSoC top level address
constexpr uint32_t kTopLevelRegBase = 0xFCC00000;

// Typical scratch memory values
// DDR offset in NSoC
constexpr uint32_t kDdrBase = 0x20000000;
// First 64 MB of DDR could be used by a firmware
constexpr uint32_t kMemOffset = 64 * 1024 * 1024;
constexpr uint32_t kScratchBase = kDdrBase + kMemOffset;
// Allow scratch to go up to 256 MB after the DDR base, i.e.: 192 MB.
// On a model like Mobilenet 0.5, forwarding more than 16 frames, the NSoC
// scratch memory usage hits a maximum peak of 17 MB, so 192 MB is
// probably large enough for inference on most models.
constexpr uint32_t kScratchSize = 192 * 1024 * 1024;

}  // namespace akd1000

}  // namespace soc

}  // namespace akida
