#pragma once

#include <cstdint>

namespace akida {
namespace soc {
namespace akd1500 {
// NSoC top level address
constexpr uint32_t kTopLevelRegBase = 0xFCC00000;
// registers region size
constexpr uint32_t kRegistersRegionSize = 1 * 1024 * 1024;

// Main memory offset in AKD1500
constexpr uint32_t kPcieDmaDescritorsSize = 256;
constexpr uint32_t kMainMemoryBase = 0x20000000 + kPcieDmaDescritorsSize;
// Main memory size is 1MB
constexpr uint32_t kMainMemorySize = 1 * 1024 * 1024 - kPcieDmaDescritorsSize;

// Extended memory offset. This corresponds to the address of host DDR mapped in
// the AKD1500
constexpr uint32_t kExtendedMainMemoryBase = 0xc0000000;
// Extended memory size is minimum 2MB, but more can be taken
constexpr uint32_t kExtendedMainMemoryMinSize = 2 * 1024 * 1024;
constexpr uint32_t kExtendedMainMemoryMaxSize = 32 * 1024 * 1024;

}  // namespace akd1500

}  // namespace soc

}  // namespace akida
