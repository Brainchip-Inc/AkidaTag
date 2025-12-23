#ifndef IO_OBJECTS_H
#define IO_OBJECTS_H

#include <zephyr/types.h>
#include "akida/hardware_device.h"
#include "akd_spi_flash.h"
#include <akd1500/akd1500_spi_driver.h>
#include <hardware_device_impl.h>


#define SRAM_128_BYTES_LEN 128

#define WORD_SIZE 4
#define NUM_WORDS 1
#define READ_LEN (NUM_WORDS * WORD_SIZE)

#define ONE_MB_SRAM_ADDR 0xFC800000 //  AKD1500 1MB SRAM address

#define CONFIG_AKD1500_CTRL 0XFCE00018
#define EN_SPI_S2M_Pos (16U)
#define EN_SPI_S2M_Msk (0x1UL << EN_SPI_S2M_Pos)
#define EN_SPI_S2M EN_SPI_S2M_Msk





// Create an instance of ZephyrSpiDriver
extern akida::ZephyrSpiDriver spi_driver;
// Create an instance of Akd1500SpiDriver with predefined memory regions
extern akida::Akd1500SpiDriver akd1500;
extern akida::HardwareDeviceImpl akd_device;


#endif //IO_OBJECTS_H