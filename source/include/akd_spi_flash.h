#ifndef AKD_SPI_FLASH_H
#define AKD_SPI_FLASH_H
#include <stddef.h>
#include <stdint.h>
#include "nrf_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

int spi_flash_read(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, uint8_t* data,
                   size_t length);
int spi_flash_erase(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, uint32_t size);
int spi_flash_write(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, const uint8_t* data,
                    size_t length);
uint32_t spi_flash_read_id(akida::ZephyrSpiDriver spi_flash_driver_);

/**
 * Confirm the flash is actually answering before trusting an erase or a write.
 *
 * Reads the JEDEC ID and rejects the two "nobody answered" patterns (all-0x00
 * and all-0xFF). The status-register poll used by erase and write cannot make
 * this call itself: WIP is bit 0, so an all-zero read looks instantly ready and
 * an all-ones read looks busy forever.
 *
 * Quiet on success (unlike spi_flash_read_id), so it is cheap to call on every
 * operation. Requires the bus already routed to the MCU and the AKD1500 awake.
 *
 * @return 0 if a plausible device answered, -ENODEV otherwise.
 */
int spi_flash_probe(akida::ZephyrSpiDriver spi_flash_driver_);

#ifdef __cplusplus
}
#endif

#endif  // AKD_SPI_FLASH_H
