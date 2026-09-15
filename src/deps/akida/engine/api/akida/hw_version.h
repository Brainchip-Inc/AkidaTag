#pragma once

#include <string>

#include "infra/exports.h"
#include "infra/hardware_driver.h"
#include "ip_version.h"

namespace akida {

/**
 * The hardware version identifier
 * Vendor_id / Product_id / Major_rev / Minor_rev
 */
struct AKIDASHAREDLIB_EXPORT HwVersion {
  uint8_t vendor_id;
  uint8_t product_id;
  uint8_t major_rev;
  uint8_t minor_rev;

  bool operator==(const HwVersion& ref) const {
    return (vendor_id == ref.vendor_id) && (product_id == ref.product_id) &&
           (major_rev == ref.major_rev) && (minor_rev == ref.minor_rev);
  }

  bool operator!=(const HwVersion& ref) const { return !(*this == ref); }

  std::string to_string() const {
    return std::to_string(vendor_id) + "." + std::to_string(product_id) + "." +
           std::to_string(major_rev) + "." + std::to_string(minor_rev) + ".";
  }

  /**
   * @brief Get the IP version
   * @return a IpVersion
   */
  IpVersion get_ip_version() const {
    switch (product_id) {
      case 0x0:
      case 0xA1:
        return IpVersion::v1;
      case 0xA2:
        return IpVersion::v2;
      default:
        return IpVersion::none;
    }
  }
};

inline constexpr HwVersion NSoC_v1 = {0xBC, 0, 0, 1};
inline constexpr HwVersion NSoC_v2 = {0xBC, 0, 0, 2};
inline constexpr HwVersion TwoNodesIP_v1 = {0xBC, 0xA1, 3, 6};
inline constexpr HwVersion AKD1500_v1 = {0xBC, 0xA1, 3, 9};
inline constexpr HwVersion FPGA_v2 = {0xBC, 0xA2, 1, 0};

// This method reads hardware version using the given driver
AKIDASHAREDLIB_EXPORT
HwVersion read_hw_version(const HardwareDriver& driver);

}  // namespace akida
