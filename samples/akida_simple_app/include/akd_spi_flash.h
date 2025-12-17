#ifndef SPI_FLASH_H
#define SPI_FLASH_H
#include "nrf_spi.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int spi_flash_read(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, uint8_t* data, size_t length);
int spi_flash_erase(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, uint32_t size);
int spi_flash_write(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, const uint8_t* data, size_t length);
uint32_t spi_flash_read_id(akida::ZephyrSpiDriver spi_flash_driver_);

#ifdef __cplusplus
}
#endif

#endif // SPI_FLASH_H
