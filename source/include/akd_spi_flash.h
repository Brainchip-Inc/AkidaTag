#ifndef AKD_SPI_FLASH_H
#define AKD_SPI_FLASH_H
#include "nrf_spi.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
  SPI_FLASH_VENDOR_UNKNOWN = 0,
  SPI_FLASH_VENDOR_MICRON,     // 0x20
  SPI_FLASH_VENDOR_WINBOND,    // 0xEF
  SPI_FLASH_VENDOR_GIGADEVICE, // 0xC8
} spi_flash_vendor_t;

#ifdef __cplusplus
extern "C" {
#endif

int spi_flash_read(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address,
                   uint8_t *data, size_t length);
int spi_flash_erase(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address,
                    uint32_t size);
int spi_flash_write(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address,
                    const uint8_t *data, size_t length);
uint32_t spi_flash_read_id(akida::ZephyrSpiDriver spi_flash_driver_);
int spi_flash_init_quad_mode(akida::ZephyrSpiDriver spi_flash_driver_);
spi_flash_vendor_t spi_flash_get_vendor(void);

#ifdef __cplusplus
}
#endif

#endif // AKD_SPI_FLASH_H
