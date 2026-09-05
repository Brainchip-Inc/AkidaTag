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
/* Read the AKD1500 device ID over SPI and print it (pass a shell, or nullptr to
 * log). Handy as an SPI integrity check after changing the clock. */
struct shell;
void get_akida_device_id(const struct shell *sh);

#ifdef __cplusplus
extern "C" {
#endif

/* Route the AKD1500 dividers onto the 800 MHz PLL (needed in Safe Mode to lift
 * the ¼-rule host-SPI ceiling). Leaves SPIS at ÷2; akd_spi_set_clock() sets the
 * operating ratio. Call at a safe host clock. Returns 0 on success. */
int akd_core_clock_to_pll(void);

/* Set the AKD1500 host SPI clock (Hz) and scale the SPI_S core to 5x it, ordered
 * so the ¼-rule holds during the change. Call with SPI idle. Returns 0 on
 * success (else akd_spi_set_frequency()'s error). */
int akd_spi_set_clock(uint32_t host_hz);

/* Set the AKD1500 core clock (Hz), bounded [5,400] MHz. Uses a divider off the
 * 800 MHz PLL when possible (on-the-fly, SPI untouched); otherwise reprograms the
 * PLL and re-scales SPI. Returns 0, -ERANGE if unreachable. Run with SPI idle for
 * the PLL path. */
int akd_core_clock_set(uint32_t core_hz);

/* Reprogram the PLL output (Hz), conservative band 600..800 MHz in 12.5 MHz
 * steps. Re-scales SPI and restores the host clock. Returns 0, -ERANGE for an
 * unsupported value, -EIO if the PLL fails to lock. Run with SPI idle. */
int akd_pll_set(uint32_t pll_out_hz);

/* Set the core (SYS) divider off the current PLLCLK, on-the-fly (§9.2). Rejects a
 * ratio giving >400 MHz. Returns 0 on success. */
int akd_sys_div_set(uint32_t div);

/* Run the AKD1500 from the 25 MHz reference (on=true, PLL output unused) or back
 * onto the 800 MHz PLL (on=false). Runs at a safe host clock; raise it after.
 * Returns 0 on success. */
int akd_clk_use_ref(bool on);

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