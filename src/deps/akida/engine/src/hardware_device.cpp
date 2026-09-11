#include "akida/hardware_device.h"

#include <memory>

#include "hardware_device_impl.h"

namespace akida {

HardwareDevicePtr HardwareDevice::create(HardwareDriver* driver) {
  return std::make_shared<HardwareDeviceImpl>(driver);
}

}  // namespace akida