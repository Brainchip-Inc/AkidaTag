
#include "io_objects.h"
#include <new>

constexpr uint32_t scratch_base_address = 0x00000000; // Use a valid address
constexpr uint32_t scratch_size = akida::soc::akd1500::kMainMemorySize;
constexpr uint32_t akida_visible_memory_base = scratch_base_address;
constexpr uint32_t akida_visible_memory_size = scratch_size;

// Create an instance of ZephyrSpiDriver
akida::ZephyrSpiDriver spi_driver;

// Create an instance of Akd1500SpiDriver with predefined memory regions
akida::Akd1500SpiDriver akd1500(&spi_driver, akida_visible_memory_base,
                                akida_visible_memory_size);

// akd_device is deferred: raw storage reserved here, object constructed
// in init_akd_object() which is called from main() after
alignas(akida::HardwareDeviceImpl) static uint8_t
    akd_device_buf[sizeof(akida::HardwareDeviceImpl)];
akida::HardwareDeviceImpl &akd_device =
    reinterpret_cast<akida::HardwareDeviceImpl &>(akd_device_buf);

void init_akd_object() {
  new (&akd_device) akida::HardwareDeviceImpl(&akd1500);
}
