#ifndef IO_OBJECTS_H
#define IO_OBJECTS_H

#include "akd_spi_flash.h"
#include "akida/hardware_device.h"
#include <akd1500/akd1500_spi_driver.h>
#include <hardware_device_impl.h>
#include <zephyr/types.h>

#define SRAM_128_BYTES_LEN 128

#define WORD_SIZE 4
#define NUM_WORDS 1
#define READ_LEN (NUM_WORDS * WORD_SIZE)

#define ONE_MB_SRAM_ADDR 0xFC800000 //  AKD1500 1MB SRAM address

#define CONFIG_AKD1500_CTRL 0XFCE00018
#define EN_SPI_S2M_Pos (16U)
#define EN_SPI_S2M_Msk (0x1UL << EN_SPI_S2M_Pos)
#define EN_SPI_S2M EN_SPI_S2M_Msk

// spi_driver and akd1500 are constructed at startup (before main).
extern akida::ZephyrSpiDriver spi_driver;
extern akida::Akd1500SpiDriver akd1500;

// akd_device is constructed in init_akd_object() — valid only after that call.
extern akida::HardwareDeviceImpl &akd_device;

// Constructs akd_device. Must be called once from main() after akida is powered
// ON.
void init_akd_object();

#endif // IO_OBJECTS_H