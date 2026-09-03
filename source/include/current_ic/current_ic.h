#ifndef CURRENT_IC_H
#define CURRENT_IC_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Background sampler thread                                                 */
/* ------------------------------------------------------------------------- */
#define CURRENT_STACK_SIZE 2048
#define CURRENT_PRIORITY 5

/* ------------------------------------------------------------------------- */
/* SAADC signal chain                                                        */
/* Gain 1/3 + internal 0.6 V ref => 1.8 V full scale, 12-bit (see overlay).  */
/* ------------------------------------------------------------------------- */
#define ADC_MAX_VALUE 4095.0f
#define ADC_REF_MV_1v8 1800.0f

/* ------------------------------------------------------------------------- */
/* INA190 current-sense conversion (shunts verified against Board BOM V10).  */
/* U25 senses VDD_1V8 (shunt R117), U26 senses VDD_0V8_AKD (shunt R118).     */
/* Vout(mV) = I(mA) * Rshunt(ohm) * gain  =>  I(mA) = Vout(mV) / (Rshunt*gain)*/
/* The A1/A3 difference is amplifier gain only; the shunts are board parts    */
/* (same on the A1 board — re-verify against the A3 board BOM).               */
/* ------------------------------------------------------------------------- */
#define SHUNT_OHMS_1V8 0.1f   /* R117 */
#define SHUNT_OHMS_0V8 0.02f  /* R118 */
#define INA190_GAIN_A1 25.0f  /* INA190A1RSW gain (V/V) */
#define INA190_GAIN_A3 100.0f /* INA190A3RSW gain (V/V) */

/* Nominal regulated rail voltages (V), used for power/energy since the
 * INA190 chain measures current only. */
#define RAIL_VOLTAGE_1V8 1.8f
#define RAIL_VOLTAGE_0V8 0.8f

/* SAADC samples averaged back-to-back per rail read (noise reduction). */
#define CURRENT_AVG_SAMPLES 8

/* Runtime sample-rate bounds for the polled sampler. Above ~1 kHz the
 * k_sleep granularity dominates; higher rates need the hardware-timed SAADC
 * path (per-inference profiling task). */
#define CURRENT_RATE_MIN_HZ 1
#define CURRENT_RATE_MAX_HZ 1000
#ifndef CONFIG_CURRENT_DEFAULT_RATE_HZ
#define CONFIG_CURRENT_DEFAULT_RATE_HZ 10
#endif

typedef enum {
    BAT_STATUS_UNKNOWN = -2,
    BAT_READ_FAILED = -1,
    BAT_NOT_CHARGING = 0,
    BAT_CHARGING = 1,
    BAT_FAULT_RECOVERABLE = 2,
    BAT_FAULT_NON_RECOVERABLE = 3,
} bat_status;

/* Measured rails. Values index the per-rail arrays below and map to the
 * SAADC channels declared in the board overlay (AIN0 = 1V8, AIN1 = 0V8). */
typedef enum {
    CURRENT_RAIL_1V8 = 0, /* U25 -> SAADC AIN0 */
    CURRENT_RAIL_0V8 = 1, /* U26 -> SAADC AIN1 */
    CURRENT_RAIL_COUNT
} current_rail_t;

/* Fitted INA190 amplifier variant (gain). Cannot be auto-detected (analog
 * part, no board ID), so it is selected: boot default CONFIG_INA190_VARIANT,
 * overridable at runtime. */
typedef enum {
    INA190_A1 = 0, /* gain 25 V/V (current AkidaTAG board) */
    INA190_A3 = 1, /* gain 100 V/V */
} ina190_variant_t;

/* Latest per-rail current/power snapshot maintained by the sampler. */
typedef struct {
    float current_ma[CURRENT_RAIL_COUNT];
    float power_mw[CURRENT_RAIL_COUNT];
} current_sense_reading_t;

/* Accumulated energy since the last reset (integrated by the sampler). */
typedef struct {
    float energy_uj[CURRENT_RAIL_COUNT];
    float avg_power_mw[CURRENT_RAIL_COUNT];
    uint32_t duration_ms;
} current_energy_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes GPIO pins for battery charger status monitoring */
int bat_sts_gpio_init(void);

/* Initializes ADC channels for the current-sense IC (both rails) */
int current_ic_init(void);

/* Always-on sampler thread: samples both rails at the configured rate,
 * updates the latest snapshot, integrates energy, and (when enabled) streams
 * over BLE. */
void current_data_thread(void* a, void* b, void* c);

/* Gets current battery charger status from GPIO pins */
bat_status check_bat_status(void);

/* --- Current / power / energy read API -----------------------------------
 * Reads return the latest values cached by the sampler (no direct ADC access,
 * safe from any thread). Return 0 on success, -EINVAL on a bad rail. */

/* Latest averaged current (mA) for one rail. */
int current_sense_read_ma(current_rail_t rail, float* current_ma);

/* Latest averaged current (mA) for both rails in one call. */
int current_sense_read_all(float* ma_1v8, float* ma_0v8);

/* Latest power (mW) for one rail (nominal rail voltage * current). */
int current_sense_read_power_mw(current_rail_t rail, float* power_mw);

/* Copy the full latest snapshot (both rails, current + power). */
void current_sense_get_latest(current_sense_reading_t* out);

/* Reset the energy accumulator and its time base. */
void current_sense_energy_reset(void);

/* Copy accumulated energy, average power, and elapsed time since last reset. */
void current_sense_energy_get(current_energy_t* out);

/* Set the sampler rate (Hz); clamped to [CURRENT_RATE_MIN_HZ,
 * CURRENT_RATE_MAX_HZ]. */
void current_sense_set_rate(uint16_t rate_hz);

/* Current sampler rate (Hz). */
uint16_t current_sense_get_rate(void);

/* Select the fitted INA190 variant (recomputes the current conversion). Safe
 * to call anytime; no hardware access. */
void current_sense_set_variant(ina190_variant_t variant);

/* Currently selected INA190 variant. */
ina190_variant_t current_sense_get_variant(void);

/* Blocking averaged measurement over @p window_ms, sampled independently of
 * the background rate. Fills @p avg (mean current/power per rail) and @p energy
 * (integrated over the window); either may be NULL. Returns 0, -EINVAL if
 * @p window_ms is 0. */
int current_sense_measure(uint16_t window_ms, current_sense_reading_t* avg,
                          current_energy_t* energy);

#ifdef __cplusplus
}
#endif

#endif
