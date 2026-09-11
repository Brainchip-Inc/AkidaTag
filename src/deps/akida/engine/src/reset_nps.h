#pragma once

#include "infra/hardware_driver.h"

namespace akida {

// This method resets logic and configuration of all NPs. It is available on
// versions > nsoc v1
void reset_nps_logic_and_cfg(HardwareDriver* driver);

}  // namespace akida
