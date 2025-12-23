#ifndef AKD_SPI_FLASH_HANDLER_H
#define AKD_SPI_FLASH_HANDLER_H


#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define FLASH_BASE_ADDRESS 0x80000000
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
extern const unsigned char* inputs[];
extern uint32_t valid_program_data[];
extern const unsigned char* program_info[];
extern const int64_t program_info_len[];

/* #################################################################### */



void akida_config_spi(bool is_mcu_master);
void init_akd_1500_spi_flash();
void akida_spiflash_init();
int akida_program_info(uint8_t *program_info, int len, uint32_t offset);
#ifdef __cplusplus
extern "C" {
#endif

int spi_flash_erase_helper_func(uint32_t offset, uint32_t size);
void spi_flash_write_helper_func(const uint8_t *data, size_t offset,
                                 size_t size);
								
int akida_program_infer();

#ifdef __cplusplus
}
#endif
int infer(int app_index_l);

#endif // AKD_SPI_FLASH_HANDLER_H