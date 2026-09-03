#include "akd_spi_flash_handler.h"
#include "akd_spi_flash.h"
#include "akida.h"
#include "akida/hardware_device.h"
extern "C" {
#include "ble_services/file_transfer.h"
#include "gpio/gpio.h"
}

#include <akd1500/akd1500_spi_driver.h>
#include <hardware_device_impl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/types.h>
#include "io_objects.h"
#include "nrf_spi.h"
LOG_MODULE_REGISTER(akd_spi_flash, LOG_LEVEL_DBG);

#ifndef CONFIG_AKD_CORE_CLOCK_HZ
#define CONFIG_AKD_CORE_CLOCK_HZ 400000000
#endif

/* AKD1500 clock/reset controller (base 0xFCE0_1000). The AkidaTAG board straps the
 * AKD1500 into Safe Mode (OP_MODE1=1), where the automatic switch of the SPI_S
 * core clock from the 25 MHz reference to the 400 MHz PLL is DISABLED. Until the
 * host performs the switch, the SPI_S core runs on the reference clock and the
 * datasheet ¼-rule caps the host SPI at ~6 MHz (this was the real "4 MHz wall").
 * With the core on the PLL the ¼-rule ceiling clears the 32 MHz host max
 * (akd_spi_set_clock() then right-sizes the SPIS divider to 5x the host clock).
 * Must be done at a safe (<=4 MHz) host clock, before raising it. */
#define AKD_CLK_GENCTRL_REG 0xFCE01000U /* [0]=PLLCLK_SEL [1]=SEL_MAN [4]=BYPASS */
#define AKD_CLK_PLLCTRL_REG 0xFCE01010U /* [8:0]DIVF [21:16]DIVR [26:24]DIVQ [30:28]RANGE */
#define AKD_CLK_PLLSTAT_REG 0xFCE01014U /* [0]=PLL_LOCK */
#define AKD_CLK_DIVUPD_REG 0xFCE0102CU  /* [0]=DIV_UPDATE_EN */
#define AKD_CLK_SYSDIV_REG 0xFCE01030U  /* [7:0]=SYS_DIV_RATIO, [31]=DC_50PCT_EN */
#define AKD_CLK_APBDIV_REG 0xFCE01034U  /* [7:0]=APB_DIV_RATIO, [31]=DC_50PCT_EN */
#define AKD_CLK_SPISDIV_REG 0xFCE0103CU /* [7:0]=SPIS_DIV_RATIO, [31]=DC_50PCT_EN */

/* Live PLL output (Hz); core (SYS_DIV) and SPI_S (SPIS_DIV) both tap this. */
static uint32_t akd_pllclk_hz = 800000000u;

static uint32_t akd_clk_rd(uint32_t addr) {
    uint32_t v = 0;
    akd1500.read(addr, &v, 4);
    return v;
}
static void akd_clk_wr(uint32_t addr, uint32_t val) {
    akd1500.write(addr, &val, 4);
}

/* Route the AKD1500 dividers onto the 800 MHz PLL (AKD500 clkrst §9.1.2), leaving
 * the SPIS divider at reset (÷2 = 400 MHz). akd_spi_set_clock() then sets the
 * operating SPI_S ratio (scaled to the host clock); SYS_DIV stays at ÷2 = 400 MHz
 * so config-DMA model programming stays fast. Idempotent; run at a safe host
 * clock. Returns 0 if PLLCLK_SEL reads back 1. */
extern "C" int akd_core_clock_to_pll(void) {
    uint32_t gen = akd_clk_rd(AKD_CLK_GENCTRL_REG);
    /* manual mode, clear PLL bypass, keep ref clock selected while waiting */
    gen |= (1u << 1);  /* PLLCLK_SEL_MAN = 1 */
    gen &= ~(1u << 4); /* PLL_BYPASS = 0 */
    gen &= ~(1u << 0); /* PLLCLK_SEL = 0 (ref) */
    akd_clk_wr(AKD_CLK_GENCTRL_REG, gen);

    int locked = 0;
    for (int i = 0; i < 200; i++) {
        if (akd_clk_rd(AKD_CLK_PLLSTAT_REG) & 1u) {
            locked = 1;
            break;
        }
        k_msleep(1);
    }

    akd_clk_wr(AKD_CLK_DIVUPD_REG, 1u); /* latch divider ratios */
    gen = akd_clk_rd(AKD_CLK_GENCTRL_REG);
    gen |= 1u; /* PLLCLK_SEL = 1 -> route dividers onto the 800 MHz PLL */
    akd_clk_wr(AKD_CLK_GENCTRL_REG, gen);

    uint32_t spisd = akd_clk_rd(AKD_CLK_SPISDIV_REG) & 0xffu;
    uint32_t genr = akd_clk_rd(AKD_CLK_GENCTRL_REG);
    akd_pllclk_hz = 800000000u;
    LOG_INF("AKD1500 SPI_S core -> PLL: GEN_CTRL=0x%08X PLLCLK_SEL=%u SPIS_DIV=%u PLL_LOCK=%d",
            genr, genr & 1u, spisd, locked);
    return (genr & 1u) ? 0 : -1;
}

/* SPIS_DIV ratio for a host clock: SPI_S core = 5x host (25% ¼-rule margin), i.e.
 * PLLCLK/(5*host). Clamped to the 8-bit divider [2,255]. */
static uint32_t spis_div_for_host(uint32_t host_hz) {
    uint32_t div = host_hz ? (akd_pllclk_hz / (5u * host_hz)) : 255u;
    if (div < 2u)
        div = 2u;
    if (div > 255u)
        div = 255u;
    return div;
}

/* Program the SPI_S core divider (DC_50PCT_EN for odd ratios) and latch it. */
static void akd_spis_write_div(uint32_t div) {
    akd_clk_wr(AKD_CLK_SPISDIV_REG, (div & 1u) ? (div | (1u << 31)) : div);
    akd_clk_wr(AKD_CLK_DIVUPD_REG, 1u);
}

/* Set the host SPI clock and scale the SPI_S core to 5x it, ordered so the ¼-rule
 * (host <= core/4) holds throughout: raising bumps the core first (divider write
 * runs at the old, lower host); lowering drops the host first (divider write runs
 * at the new, lower host). Callers must be idle (KWS stopped). */
extern "C" int akd_spi_set_clock(uint32_t host_hz) {
    uint32_t div = spis_div_for_host(host_hz);
    if (host_hz >= akd_spi_get_frequency()) {
        akd_spis_write_div(div);
        return akd_spi_set_frequency(host_hz);
    }
    int rc = akd_spi_set_frequency(host_hz);
    if (rc == 0) {
        akd_spis_write_div(div);
    }
    return rc;
}

/* Program a divider register (DC_50PCT_EN for odd ratios), without latching. */
static void akd_div_write(uint32_t reg, uint32_t div) {
    akd_clk_wr(reg, (div & 1u) ? (div | (1u << 31)) : div);
}

/* Set the core (SYS) divider off the current PLLCLK; latch on-the-fly (§9.2).
 * Also re-sizes APB so clk_apb stays <= clk_axi with an integer ratio (clkrst
 * Table 7) — else the APB register bus dies once the core drops below APB.
 * Both dividers latch together in one DIV_UPDATE, so APB never leads AXI.
 * Rejects a ratio that would exceed 400 MHz core. */
extern "C" int akd_sys_div_set(uint32_t div) {
    if (div < 1u)
        div = 1u;
    if (div > 255u || akd_pllclk_hz / div > 400000000u)
        return -EINVAL;
    uint32_t apb_min = (akd_pllclk_hz + 199999999u) / 200000000u; /* ceil(PLLCLK/200MHz) */
    uint32_t apb = div * ((apb_min + div - 1u) / div); /* smallest multiple of div, APB<=200MHz */
    if (apb < div)
        apb = div;
    if (apb > 255u)
        apb = (255u / div) * div;
    akd_div_write(AKD_CLK_APBDIV_REG, apb);
    akd_div_write(AKD_CLK_SYSDIV_REG, div);
    akd_clk_wr(AKD_CLK_DIVUPD_REG, 1u); /* latch SYS + APB together */
    return 0;
}

/* Solve DIVF/DIVQ for a PLL output, holding DIVR=0 and DIVQ_VAL=4 so the VCO
 * (=50 MHz*DIVF_VAL) stays in [2400,3200] MHz (<= the proven 3.2 GHz). Valid
 * outputs are 12.5 MHz*DIVF_VAL for DIVF_VAL in [48,64] => 600..800 MHz. */
static int akd_pll_solve(uint32_t pll_out_hz, uint32_t* divf, uint32_t* divq) {
    if (pll_out_hz % 12500000u)
        return -ERANGE;
    uint32_t divf_val = pll_out_hz / 12500000u;
    if (divf_val < 48u || divf_val > 64u)
        return -ERANGE;
    *divf = divf_val; /* DIVF_VAL; register field = DIVF_VAL-1 */
    *divq = 2u;       /* DIVQ field: DIVQ_VAL = 2^2 = 4 */
    return 0;
}

/* Reprogram the PLL output (AKD500 clkrst §9.1.2). Runs at a safe 1.4 MHz host
 * and parks the chip on the 25 MHz ref while the PLL relocks, so the core never
 * runs on an unstabilized PLL; only routes onto the PLL after lock + settle, then
 * re-scales SPIS and restores the host clock. Returns -EIO if it never locks. */
extern "C" int akd_pll_set(uint32_t pll_out_hz) {
    uint32_t divf, divq;
    if (akd_pll_solve(pll_out_hz, &divf, &divq) != 0)
        return -ERANGE;

    uint32_t host = akd_spi_get_frequency();
    akd_spi_set_frequency(1400000u);
    (void)akd_clk_rd(AKD_CLK_GENCTRL_REG); /* flush the pointer-swap reconfigure */

    /* Shrink SPIS so SPI_S stays fast enough for the <=1 MHz host once PLLCLK
     * collapses to the 25 MHz ref during the reprogram (else the ¼-rule breaks
     * and the register bus dies). SPI_S = PLLCLK/2: 400 MHz on the PLL, 12.5 MHz
     * on the ref — both clear the 1 MHz host. */
    akd_spis_write_div(2u);

    uint32_t gen = akd_clk_rd(AKD_CLK_GENCTRL_REG);
    gen |= (1u << 1);  /* PLLCLK_SEL_MAN = 1 */
    gen &= ~(1u << 0); /* PLLCLK_SEL = 0 (park on 25 MHz ref) */
    akd_clk_wr(AKD_CLK_GENCTRL_REG, gen);
    gen |= (1u << 4); /* PLL_BYPASS = 1 */
    akd_clk_wr(AKD_CLK_GENCTRL_REG, gen);

    akd_clk_wr(AKD_CLK_PLLCTRL_REG, (3u << 28) | ((divq & 0x7u) << 24) | ((divf - 1u) & 0x1FFu));

    gen &= ~(1u << 4); /* PLL_BYPASS = 0 */
    akd_clk_wr(AKD_CLK_GENCTRL_REG, gen);
    akd_clk_wr(AKD_CLK_DIVUPD_REG, 1u);

    int locked = 0;
    for (int i = 0; i < 200; i++) {
        if (akd_clk_rd(AKD_CLK_PLLSTAT_REG) & 1u) {
            locked = 1;
            break;
        }
        k_msleep(1);
    }
    k_msleep(2); /* settle after lock, before routing */

    if (locked) {
        gen = akd_clk_rd(AKD_CLK_GENCTRL_REG);
        gen |= 1u; /* PLLCLK_SEL = 1 -> route dividers onto the relocked PLL */
        akd_clk_wr(AKD_CLK_GENCTRL_REG, gen);
        akd_pllclk_hz = pll_out_hz;
        akd_spi_set_clock(host);
    } else {
        akd_pllclk_hz = 25000000u; /* left on the ref; keep the host safe */
        akd_spi_set_clock(1400000u);
    }
    LOG_INF("AKD1500 PLL -> %u Hz (DIVF=%u DIVQ_VAL=4) locked=%d", pll_out_hz, divf, locked);
    return locked ? 0 : -EIO;
}

/* Set the AKD1500 core clock (Hz), bounded [5,400] MHz. If it divides the default
 * 800 MHz PLL evenly (SYS_DIV <= 255), just change the divider (SPI untouched);
 * otherwise reprogram the PLL to an output that divides evenly (re-scaling SPI),
 * then set SYS_DIV. Returns -ERANGE if unreachable. */
extern "C" int akd_core_clock_set(uint32_t core_hz) {
    if (core_hz < 5000000u || core_hz > 400000000u)
        return -EINVAL;

    if (akd_pllclk_hz == 800000000u && (800000000u % core_hz) == 0u) {
        uint32_t q = 800000000u / core_hz;
        if (q <= 255u)
            return akd_sys_div_set(q);
    }

    for (uint32_t divf_val = 64u; divf_val >= 48u; divf_val--) {
        uint32_t pll = 12500000u * divf_val;
        if (pll % core_hz)
            continue;
        if (pll / core_hz > 255u)
            continue;
        int rc = akd_pll_set(pll);
        if (rc)
            return rc;
        return akd_sys_div_set(pll / core_hz);
    }
    return -ERANGE;
}

/* Run the AKD1500 from the 25 MHz reference (on=true, PLL output unused, lowest
 * power) or back onto the 800 MHz PLL (on=false), via the glitch-free PLLCLK_SEL
 * mux. Runs at 1.4 MHz host; the caller raises the host clock afterwards. */
extern "C" int akd_clk_use_ref(bool on) {
    akd_spi_set_frequency(1400000u);
    (void)akd_clk_rd(AKD_CLK_GENCTRL_REG);
    if (!on) {
        /* Restore the default 800 MHz PLL (it may have been reprogrammed) and route
         * back onto it, then normalize to 400 MHz core / 200 MHz APB. */
        int rc = akd_pll_set(800000000u);
        akd_sys_div_set(2u);
        return rc;
    }

    /* SPI_S = PLLCLK/2 keeps the <=1 MHz host inside the ¼-rule on both the PLL
     * (400 MHz) and the ref (12.5 MHz); set it before flipping so the register
     * bus survives the 800->25 MHz PLLCLK collapse. */
    akd_spis_write_div(2u);
    uint32_t gen = akd_clk_rd(AKD_CLK_GENCTRL_REG);
    gen |= (1u << 1);  /* PLLCLK_SEL_MAN = 1 */
    gen &= ~(1u << 4); /* PLL_BYPASS = 0 (use the mux, not the glitchy bypass) */
    gen &= ~(1u << 0); /* PLLCLK_SEL = 0 (25 MHz ref) */
    akd_clk_wr(AKD_CLK_GENCTRL_REG, gen);
    akd_pllclk_hz = 25000000u;
    return (akd_clk_rd(AKD_CLK_GENCTRL_REG) & 1u) ? -EIO : 0;
}
#if FLASH_READ_BACK_CHECK
uint8_t read_back_flash[HALF_OF_SRAM_BUFFER_SIZE];
#endif

union _data {
    uint8_t ucdata[4];
    uint32_t uint_data;
};

uint32_t flash_offsets[] = {AKD_FLASH_OFFSET, AKD_FLASH_OFFSET + AKD_MODEL_OFFSET};
int app_index = -1;

/**
 * @brief Reads and prints the AKIDA device ID.
 *
 * This function reads the device identification registers from the
 * AKD1500 (AKIDA) chip over SPI and prints the device ID words
 * to the console for verification.
 */
void get_akida_device_id(const struct shell* sh) {
    static uint8_t read_data[READ_LEN];
    /* read Akida Device ID */
    akd1500.read(0xFCC00000, read_data, READ_LEN);
    if (sh) {
        shell_print(sh, "Akida Device ID:");
        for (int i = 0; i < READ_LEN; i += WORD_SIZE) {
            shell_print(sh, "Word %d: 0x%02X%02X%02X%02X", i / WORD_SIZE, read_data[i],
                        read_data[i + 1], read_data[i + 2], read_data[i + 3]);
        }
    } else {
        LOG_INF("Akida Device ID:");
        for (int i = 0; i < READ_LEN; i += WORD_SIZE) {
            LOG_INF("Word %d: 0x%02X%02X%02X%02X", i / WORD_SIZE, read_data[i], read_data[i + 1],
                    read_data[i + 2], read_data[i + 3]);
        }
    }
}

/**
 * @brief Performs a sanity test on AKIDA SRAM.
 *
 * This function writes a test pattern to the AKD1500 SRAM and reads it back
 * to verify data integrity. The comparison is done to detect any mismatch
 * between written and read data. Currently, the test is limited to a single
 * 128-byte block. To test the full 1 MB SRAM, update the loop condition
 * to iterate across the entire memory range.
 */
void akida_sram_test(void) {
    static uint8_t msg[SRAM_128_BYTES_LEN] =
        "Hello world!!! This is a test for writing and reading 128 bytes of data "
        "to and from 1MB of RAM within Brainchip's AKD1500 chip";
    uint8_t sram_read_data[SRAM_128_BYTES_LEN] = "kkkkkkkkk";
    uint32_t fail_cnt = 0;

    // Write and read 1 MB RAM in Akida in 128-byte chunks (Currently it is tested
    // for 128 Bytes). To check complete 1MB replace offset < 1 with offset <
    // ONE_MB in the below for loop

    for (uint32_t offset = 0; offset < 1; offset += SRAM_128_BYTES_LEN) {
        uint32_t addr = ONE_MB_SRAM_ADDR + offset;

        // Write
        akd1500.write(addr, msg, SRAM_128_BYTES_LEN);

        akd1500.read(addr, sram_read_data, SRAM_128_BYTES_LEN);

        // Compare
        if (memcmp(msg, sram_read_data, SRAM_128_BYTES_LEN) != 0) {
            fail_cnt++;
            LOG_ERR("Data mismatch at address 0x%08X (offset: %u bytes)", addr, offset);
            for (int i = 0; i < SRAM_128_BYTES_LEN; ++i) {
                LOG_INF("%c", sram_read_data[i]);
            }
        }
        // Clear the read data
        memset(sram_read_data, 0, SRAM_128_BYTES_LEN);
    }
    if (fail_cnt == 0) {
        LOG_PRINTK("Sanity test of 1 MB SRAM is passed\n");
    } else {
        LOG_PRINTK("Sanity test of 1 MB SRAM is failed\n");
    }
}

void akida_spiflash_init(void) {
    /* Set MCU as SPI-Master */
    akida_config_spi(1);

    get_akida_device_id(nullptr);
    akida_sram_test();

    /* Initialize the AKD1500 SPI-Flash functionality */
    init_akd_1500_spi_flash();

    /* get akida device version */
    auto hw_version = akida::read_hw_version(akd1500);
    LOG_INF("Device Version: v%u.%u", hw_version.major_rev, hw_version.minor_rev);

    spi_flash_read_id(spi_driver);  // read SPI-Flash id

    /* Device is confirmed alive at the conservative bring-up clock. Route the SPI_S
     * core onto the 800 MHz PLL (still at the safe bring-up clock). */
    akd_core_clock_to_pll();

    /* Apply the configured operating clock; akd_spi_set_clock() scales the SPI_S
     * core to 5x it (e.g. 8 MHz host -> 40 MHz core). Runtime-tunable via
     * `spi_freq`/`akd_probe`. */
    akd_spi_set_clock(CONFIG_AKD_SPI_FREQ_HZ);

    /* Apply the configured AKD1500 core clock (default 400 MHz = no-op).
     * Runtime-tunable via `akd_coreclk`. */
    akd_core_clock_set(CONFIG_AKD_CORE_CLOCK_HZ);
}
/* function to enable external host MCU/AKD1500 as SPI master for 16 MB flash */
void akida_config_spi(bool is_mcu_master) {
    union _data rw_data;
    akd1500.read(CONFIG_AKD1500_CTRL, rw_data.ucdata, 4);
    if (is_mcu_master) {
        // configure external host (MCU) as SPI master for 16 MB flash
        rw_data.uint_data = rw_data.uint_data | EN_SPI_S2M;
        akd1500.write(CONFIG_AKD1500_CTRL, rw_data.ucdata, 4);
        LOG_INF("Enabling external host as SPI master");
    } else {
        // configure AKD1500 as SPI master for 16 MB flash
        rw_data.uint_data = rw_data.uint_data & (~EN_SPI_S2M);
        akd1500.write(CONFIG_AKD1500_CTRL, rw_data.ucdata, 4);
        LOG_INF("Enabling AKD1500 as SPI master");
    }
}

/* configure the spi-flash driver settings */
void init_akd_1500_spi_flash() {
    union _data rw_data;
    akd1500.read(0xfce00010, rw_data.ucdata, 4);
    uint32_t reg = rw_data.uint_data;
    akd1500.read(0xfce00018, rw_data.ucdata, 4);
    rw_data.uint_data |= 1 << 21 | 1 << 16 | 1 << 17; /* switch endianness on spi master */
    akd1500.write(0xfce00018, rw_data.ucdata, 4);

    int64_t delay = time_ms() + 100;
    while (time_ms() < delay)
        ;

    /* Configure spi flash */
    rw_data.uint_data = 0;
    akd1500.write(0xfcf20008, rw_data.ucdata, 4);
    rw_data.uint_data = 0x8080001f;
    akd1500.write(0xfcf20000, rw_data.ucdata, 4);
    rw_data.uint_data = 1;
    akd1500.write(0xfcf20010, rw_data.ucdata, 4);
    rw_data.uint_data = 8;
    akd1500.write(0xfcf20014, rw_data.ucdata, 4); /* BAUD */
    rw_data.uint_data = 0x08100200 | 1 << 20 | 10 << 11 | 6 << 2 | 1;
    akd1500.write(0xfcf200f4, rw_data.ucdata, 4);
    rw_data.uint_data = 0;
    akd1500.write(0xfcf200f8, rw_data.ucdata, 4);
    rw_data.uint_data = 0xcc;
    akd1500.write(0xfcf200fc, rw_data.ucdata, 4);
    rw_data.uint_data = 0xeb;
    akd1500.write(0xfcf20100, rw_data.ucdata, 4);
    rw_data.uint_data = 0x1;
    akd1500.write(0xfcf20008, rw_data.ucdata, 4);
    akd1500.read(0xfce00018, rw_data.ucdata, 4);
    LOG_INF("Akida1500 SPI Flash initialized on %x %x", reg, rw_data.uint_data);
    /* setup gpio mux for interrupts selecting pin 3*/
    rw_data.uint_data = 0x08;
    akd1500.write(0xfce00038, rw_data.ucdata, 4);
    rw_data.uint_data = 0x00;
    akd1500.write(0xfce0003c, rw_data.ucdata, 4);
}

/* ---------------------------------------------------------------------------
 * Claiming the AKD1500 flash
 *
 * The model flash is not on a bus of the nRF's own: it hangs off the AKD1500 and
 * is only reachable when akida_config_spi(1) routes the AKD1500's S2M
 * feedthrough to the host. Two consequences drive everything below.
 *
 * 1. The AKD1500 has to be AWAKE. SLEEP is a hardware clock gate, so a sleeping
 *    chip feeds nothing through and every transaction reads back all-zero
 *    (AN-002, Sleep and Low-Power Operation: "A sleeping AKD1500 returns
 *    0x00000000 on register reads. Wake it before any register access or
 *    inference.") or all-ones when MISO floats. During normal async KWS the
 *    chip is asleep between inferences, which is its steady state, so any
 *    caller that is not the inference path finds it asleep. Holding a wake
 *    reference is part of preparing the bus, which is why it belongs here next
 *    to akida_config_spi(1) rather than at each call site: this covers the BLE
 *    model-update erase, the BLE chunk write, the readback validation and the
 *    `full_erase` shell command in one place. The reference is what keeps the
 *    KWS duty cycle from clock-gating the chip mid-erase: the inference that
 *    finishes while we hold the bus returns its own reference, sees ours still
 *    outstanding, and leaves SLEEP de-asserted.
 *
 * 2. Access has to be SERIALISED. Three threads reach this code (the Bluetooth
 *    RX thread for a model update, the shell thread for `full_erase`, and the
 *    KWS inference path for boot validation and readback) and a flash operation
 *    holds the bus in MCU-master mode for hundreds of milliseconds. The mutex
 *    is about the bus alone - akida_config_spi(1) is global state, so two
 *    concurrent operations would interleave on one S2M feedthrough. It says
 *    nothing about the sleep state, which the wake count above owns.
 * ------------------------------------------------------------------------ */
K_MUTEX_DEFINE(akd_flash_mutex);

namespace {

/* Scoped claim of the AKD1500 flash: take the bus lock, hold a wake reference
 * for the whole access, and route the feedthrough to the host. Scoped so that
 * every exit path out of the helpers below unwinds all three in the reverse
 * order without each early return having to remember to. */
class FlashClaim {
   public:
    FlashClaim() {
        k_mutex_lock(&akd_flash_mutex, K_FOREVER);
        akd_wake_get();
        akida_config_spi(1);
    }
    ~FlashClaim() {
        akida_config_spi(0);
        akd_wake_put();
        k_mutex_unlock(&akd_flash_mutex);
    }
    FlashClaim(const FlashClaim&) = delete;
    FlashClaim& operator=(const FlashClaim&) = delete;
};

}  // namespace

/* helper function to read from SPI flash – used by file_transfer.c for CRC */
extern "C" void spi_flash_read_helper_func(uint8_t* buf, uint32_t offset, uint32_t size) {
    if (!buf || size == 0) {
        return;
    }
    int ret;
    {
        FlashClaim claim;
        ret = spi_flash_probe(spi_driver);
        if (ret == 0) {
            ret = spi_flash_read(spi_driver, offset, buf, size);
        }
    }
    if (ret != 0) {
        LOG_ERR("spi_flash_read_helper: read failed at 0x%x size=%u (err %d)", offset, size, ret);
    }
}

/* helper function to invoke flash erase API calls */
extern "C" int spi_flash_erase_helper_func(uint32_t offset, uint32_t size) {
    if (size == 0 || size > (FLASH_MAX_16_MB_SIZE - offset)) {
        LOG_ERR("Invalid size. Must be > 0 and <= %d", (FLASH_MAX_16_MB_SIZE - offset));
        return 1;
    }
    FlashClaim claim;

    uint64_t s_tick = 0;
    uint64_t e_tick = 0;
    uint32_t erase_time = 0;

    LOG_INF("Flash erase offset %x and size = %d bytes", offset, size);

    /* Confirm something is actually answering before believing the erase. The
     * status poll inside spi_flash_erase() reads WIP from bit 0, so an unreachable
     * flash either looks instantly ready (all-zero: erase "succeeds" having done
     * nothing) or busy forever (all-ones: the full timeout burns per sector).
     * Neither is a truthful result, so gate on the JEDEC ID instead. */
    int ret = spi_flash_probe(spi_driver);
    if (ret == 0) {
        /* Bracket the erase with the gpio.c tallies so what follows is a fact the
         * firmware observed rather than something inferred from the order console
         * lines happened to arrive in. `releases` is how many wake references other
         * threads handed back while the erase ran, i.e. how much duty-cycle
         * contention it actually survived; `gated` is how many times SLEEP was
         * asserted, which must be zero because `claim` holds a reference throughout.
         * Unsigned differences, so both stay correct across the UINT32_MAX wrap. */
        const uint32_t releases_before = akd_wake_release_count();
        const uint32_t gates_before = akd_wake_gate_count();

        s_tick = time_ms();
        ret = spi_flash_erase(spi_driver, offset, size);
        e_tick = time_ms();
        erase_time = e_tick - s_tick;

        const uint32_t releases = akd_wake_release_count() - releases_before;
        const uint32_t gated = akd_wake_gate_count() - gates_before;
        LOG_INF("erase time= %u ms (wake releases during erase: %u, sleep gated: %u)", erase_time,
                releases, gated);

        /* A non-zero `gated` means the AKD1500 was clock-gated part way through, so
         * the WIP poll spent the rest of the erase reading a dead bus and reported
         * every remaining sector as instantly ready. Cheap enough to check on every
         * erase, and it turns that into a logged failure instead of a silent
         * "Erase Successful" for sectors that were never touched. */
        if (gated != 0) {
            LOG_ERR(
                "AKD1500 wake reference lost during erase: the chip slept "
                "mid-erase and the erase result cannot be trusted");
            ret = -EIO;
        }
    }

    if (ret) {
        LOG_PRINTK("Erase failed\n");
    } else {
        LOG_PRINTK("Erase Successful\n");
    }

    return ret;
}

/* helper function to invoke spi-flash write driver API for the given data,
 * offset, and size */
extern "C" void spi_flash_write_helper_func(const uint8_t* data, size_t offset, size_t size) {
    FlashClaim claim;

    uint32_t flash_addr = offset;

    uint64_t s_write = 0;
    uint64_t e_write = 0;
    uint32_t write_time = 0;

    /* Same reasoning as the erase: do not report a write that the flash never
     * saw. See the comment in spi_flash_erase_helper_func. */
    int ret = spi_flash_probe(spi_driver);
    if (ret != 0) {
        LOG_ERR("Flash write skipped, flash not responding (addr = 0x%x, size = %d)", flash_addr,
                size);
        return;
    }

    s_write = time_ms();
    ret = spi_flash_write(spi_driver, flash_addr, data, size);
    if (ret != 0) {
        LOG_ERR("Flash write failed with error %d", ret);
        LOG_INF("Flash Address = 0x%x, size = %d", flash_addr, size);
        return;
    }
    e_write = time_ms();
    write_time = e_write - s_write;
    LOG_INF("write time = %u ms", write_time);
#if FLASH_READ_BACK_CHECK
    uint64_t s_read = 0;
    uint64_t e_read = 0;
    uint32_t read_time = 0;
    s_read = time_ms();
    // Read back from flash into second half of data[]
    ret = spi_flash_read(spi_driver, flash_addr, read_back_flash, size);
    if (ret != 0) {
        LOG_ERR("Flash read failed with error %d", ret);
        return;
    }

    // Compare the written and read data
    if (memcmp(data, read_back_flash, size) == 0) {
        LOG_INF("Flash write-read verification successful.");
    } else {
        LOG_ERR("Flash data mismatch!");
    }
    e_read = time_ms();
    read_time = e_read - s_read;
    LOG_INF("read_time= %u ms", read_time);
#endif
    if (!ret)
        LOG_INF("Flash Write Successful");
}

/**
 * @brief CLI command to read the AKIDA device ID.
 *
 * This command enables the SPI interface, reads the device ID from the
 * AKD1500 (AKIDA) chip using get_akida_device_id(), and then disables
 * the SPI interface.
 *
 * @param shell Pointer to the shell instance.
 * @param argc  Number of command arguments.
 * @param argv  Command arguments.
 *
 * @return 0 on success.
 */
static int cmd_akida_device_id(const struct shell* shell, size_t argc, char** argv) {
    akida_config_spi(1);
    get_akida_device_id(shell);
    akida_config_spi(0);
    return 0;
}

/**
 * @brief CLI command to perform AKIDA SRAM sanity test.
 *
 * This command enables the SPI interface, runs a SRAM read/write
 * verification test on the AKD1500 using akida_sram_test(), and
 * then disables the SPI interface.
 *
 * @param shell Pointer to the shell instance.
 * @param argc  Number of command arguments.
 * @param argv  Command arguments.
 *
 * @return 0 on completion.
 */
static int cmd_akida_sram_test(const struct shell* shell, size_t argc, char** argv) {
    akida_config_spi(1);
    akida_sram_test();
    akida_config_spi(0);
    return 0;
}

/**
 * @brief CLI command to read the SPI flash device ID.
 *
 * This command enables the SPI interface, reads the SPI flash ID
 * using spi_flash_read_id(), prints the result to the shell, and
 * then disables the SPI interface.
 *
 * @param shell Pointer to the shell instance.
 * @param argc  Number of command arguments.
 * @param argv  Command arguments.
 *
 * @return 0 on success.
 */
static int cmd_flash_id(const struct shell* shell, size_t argc, char** argv) {
    akida_config_spi(1);
    spi_flash_read_id(spi_driver);
    akida_config_spi(0);
    shell_print(shell, "SPI Flash ID read completed");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    akida_cmds, SHELL_CMD(device_id, NULL, "Read Akida device ID", cmd_akida_device_id),
    SHELL_CMD(sram_test, NULL, "Run SRAM test", cmd_akida_sram_test),
    SHELL_CMD(flash_id, NULL, "Read Flash ID", cmd_flash_id), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(akida, &akida_cmds, "Akida commands", NULL);