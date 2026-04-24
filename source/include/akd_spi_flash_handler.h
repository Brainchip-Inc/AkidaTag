#ifndef AKD_SPI_FLASH_HANDLER_H
#define AKD_SPI_FLASH_HANDLER_H

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/types.h>

#define FLASH_READ_BACK_CHECK 0

#define AKD_FLASH_OFFSET 0x1000
#define AKD_MODEL_OFFSET 0x100000

#if FLASH_READ_BACK_CHECK
#define HALF_OF_SRAM_BUFFER_SIZE (SRAM_BUFFER_SIZE / 2)
#define BUFFER_SIZE HALF_OF_SRAM_BUFFER_SIZE
#else
#define BUFFER_SIZE SRAM_BUFFER_SIZE
#endif

#define FLASH_MAX_16_MB_SIZE (16777216) // total max size of SPI-Flash => 16 MB

extern volatile size_t akd_flash_offset;
extern uint32_t buff_size;
extern uint32_t flash_offsets[];
extern int app_index;

/* #################################################################### */
/* These have to be defined by the application */
extern const unsigned char *inputs[];
extern uint32_t valid_program_data[];
extern const unsigned char *program_info[];
extern const int64_t program_info_len[];

/* #################################################################### */

void akida_config_spi(bool is_mcu_master);
void init_akd_1500_spi_flash();
void akida_spiflash_init();
void turn_off_nodes_npu(uint8_t row, uint8_t collumn, uint8_t np_id);
void change_akida_core_clock(uint32_t conf_value);

#ifdef __cplusplus
extern "C" {
#endif

int spi_flash_erase_helper_func(uint32_t offset, uint32_t size);
void spi_flash_write_helper_func(const uint8_t *data, size_t offset,
                                 size_t size);
/**
 * Read @p size bytes from SPI flash at @p offset into @p buf.
 * Handles SPI master switching (MCU↔AKD1500) internally.
 */
void spi_flash_read_helper_func(uint8_t *buf, uint32_t offset, uint32_t size);

int akida_program_infer();

#ifdef __cplusplus
}
#endif

#endif // AKD_SPI_FLASH_HANDLER_H