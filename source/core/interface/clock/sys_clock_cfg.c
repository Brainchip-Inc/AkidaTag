/*
 * System clock configuration for the nRF5340 application core.
 *
 * The nRF5340 application core boots at its reset default of 64 MHz
 * (HFCLKCTRL.HCLK = DIV2). Nordic Connect SDK does not raise this automatically,
 * so without this hook the core runs at 64 MHz. This early SYS_INIT selects DIV1
 * so the core runs at its maximum 128 MHz ("standard operation").
 *
 * Only the CPU (HCLK) domain is affected. Peripheral clocks (the SPIM base clock,
 * the 32.768 kHz RTC system timer, etc.) are independent of this divider, so the
 * OS tick, k_busy_wait() and SPI baud rates are unchanged. Note the ~2x increase
 * in core dynamic power that comes with 128 MHz operation.
 *
 * This lives in the firmware layer so any app targeting the spark board can reuse
 * it. It is gated by CONFIG_SYS_CPU_128MHZ (CMake only compiles it when enabled).
 */
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <hal/nrf_clock.h>

LOG_MODULE_REGISTER(sys_clock_cfg, LOG_LEVEL_INF);

/* CMSIS core clock global (Hz) and its updater, provided by the nRF5340 MDK. */
extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);

static int sys_cpu_128mhz_init(void)
{
	/* Select HCLK = 128 MHz (HFCLKCTRL.HCLK = DIV1); reset default is DIV2 = 64 MHz. */
	nrf_clock_hfclk_div_set(NRF_CLOCK, NRF_CLOCK_HFCLK_DIV_1);

	/* Refresh the CMSIS SystemCoreClock global so any cycle-based delays use the
	 * correct frequency. */
	SystemCoreClockUpdate();

	LOG_INF("App core HCLK = DIV1 (128 MHz); SystemCoreClock = %u Hz", SystemCoreClock);
	return 0;
}

SYS_INIT(sys_cpu_128mhz_init, PRE_KERNEL_1, 0);
