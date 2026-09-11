#include "akida/hw_version.h"
#include "akida/registers_top_level.h"

#include <cstring>

namespace akida {

HwVersion read_hw_version(const HardwareDriver& driver) {
  HwVersion version{0, 0, 0, 0};
  // Try first to read IP revision from device
  const auto top_level_reg_offset = driver.top_level_reg();
  auto reg = driver.read32(top_level_reg_offset + REG_IP_VERSION);
  auto vendor_id = static_cast<uint8_t>(get_field(reg, VENDOR_ID));
  if (reg != 0) {
    auto minor_rev = static_cast<uint8_t>(get_field(reg, MINOR_REV));
    auto major_rev = static_cast<uint8_t>(get_field(reg, MAJOR_REV));
    auto prod_id = static_cast<uint8_t>(get_field(reg, PROD_ID));
    version = {vendor_id, prod_id, major_rev, minor_rev};
  } else {
    // Legacy device: rely instead on the information provided by the driver
    auto driver_desc = driver.desc();
    if (strstr(driver_desc, "NSoC_v2") != nullptr) {
      version = NSoC_v2;

    } else if (strstr(driver_desc, "NSoC_v1") != nullptr) {
      version = NSoC_v1;
    }
  }
  return version;
}

}  // namespace akida
