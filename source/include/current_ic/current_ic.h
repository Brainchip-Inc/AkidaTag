#ifndef CURRENT_IC_H
#define CURRENT_IC_H

#include <stdbool.h>
#include <stdint.h>

#define CURRENT_STACK_SIZE 2048
#define CURRENT_PRIORITY 5
#define AVG_SAMPLES 20
#define ADC_MAX_VALUE 4095.0f
#define ADC_REF_MV_1v8 1800.0f
#define SHUNT_RESISTOR_GAIN_1V8 (25.0f * 1.0f) /*(GAIN * Resistor)*/
#define SHUNT_RESISTOR_GAIN_0V8 (25.0f * 0.2f) /*(GAIN * Resistor)*/

/* Nominal regulated 0V8 rail voltage, used for power (P = V*I) and energy.
 * The signal chain measures current only (INA190 sense output); the rail
 * voltage is assumed constant at this nominal value. */
#define RAIL_VOLTAGE_0V8 0.800f

typedef enum {
  BAT_STATUS_UNKNOWN = -2,
  BAT_READ_FAILED = -1,
  BAT_NOT_CHARGING = 0,
  BAT_CHARGING = 1,
  BAT_FAULT_RECOVERABLE = 2,
  BAT_FAULT_NON_RECOVERABLE = 3,
} bat_status;

/* Initializes GPIO pins for battery charger status monitoring */
int bat_sts_gpio_init(void);

/* Initializes ADC channels for current monitoring IC */
int current_ic_init(void);

/* thread to reads and averages current measurements from monitoring IC */
void current_data_thread(void *a, void *b, void *c);

/* Gets current battery charger status from GPIO pins */
bat_status check_bat_status(void);

/* ---------------------------------------------------------------------------
 * Per-inference 0V8 current sampler — tuning parameters
 *
 * The sampler captures the full enqueue->done-IRQ window (~17 ms on this
 * board: ~16.8 ms SPI/DMA frame transfer + ~180 us Akida compute) and also
 * retains the last few samples in a tail ring so spikes that occur during
 * the short inference phase are not buried by the dominant SPI-transfer
 * current. See current_ic.c for the full pipeline description.
 * ---------------------------------------------------------------------------
 */

/* Sample period (us) for the per-inference current capture. Now driven by the
 * SAADC's internal HARDWARE timer (16 MHz), so the period is honored exactly
 * with zero CPU per sample. The hardware CC = 16 * period_us must be in
 * [80, 2047], i.e. period_us in [5, 127]. 40 us -> CC = 640; ~7 samples land in
 * the ~285 us Akida compute phase, and ~400 over a ~16 ms window (fits the
 * INF_MAX_SAMPLES buffer). */
#define INF_SAMPLE_PERIOD_US 40

/* Safety stop. The chip's done-IRQ normally ends sampling well before this
 * fires; the timeout only kicks in if that IRQ never arrives. */
#define INF_SAMPLE_TIMEOUT_US 50000

/* Tail depth. The trailing window is INF_TAIL_LEN * INF_SAMPLE_PERIOD_US ending
 * at the done-IRQ. At 40 us that is 6 * 40 = 240 us, which closely matches the
 * ~285 us Akida compute phase — so the tail_* stats characterize compute. */
#define INF_TAIL_LEN 6

/* Spike threshold: a sample is flagged as a spike when it exceeds the mean
 * by this many mA. Applied to both full-window and tail stat sets. */
#define INF_SPIKE_THRESH_MA 50.0f

/* Buffered-read capacity (int16_t[512] = 1 KB RAM). */
#define INF_MAX_SAMPLES 512

/* Capture window per inference (us). The buffered read collects
 * INF_CAP_WINDOW_US / INF_SAMPLE_PERIOD_US samples (capped at INF_MAX_SAMPLES),
 * chosen a bit above the typical inference (~16 ms) so the whole inference is
 * covered; the dump trims to the real window using inf_time. */
#define INF_CAP_WINDOW_US 25000U

/* Default current-band thresholds (mA) for per-sample counting. Samples
 * strictly above INF_HI_THRESH_MA and strictly below INF_LO_THRESH_MA are
 * counted per inference and per run. Runtime-adjustable via `cmeas_thresh`. */
#define INF_HI_THRESH_MA 120.0f
#define INF_LO_THRESH_MA 50.0f

/* Default N for a CLI-driven bench run (`cmeas start` with no argument). */
#define INF_RUN_DEFAULT_N 1000U

#ifdef __cplusplus
extern "C" {
#endif

/* Arm the per-inference 0V8 sampler. Called just before akida_enqueue().
 * Re-arming cancels any in-flight session. Resets all accumulators and the
 * tail ring, then starts the periodic sampling timer and the safety-stop
 * timeout. */
void inference_current_start(void);

/* Stop the sampler. Called from the Akida done-IRQ (normal path) and from
 * the safety timeout (fallback). IRQ-safe and idempotent via atomic_cas. */
void inference_current_stop(void);

/* Emit a one-line summary of the captured event: sample count, window
 * duration, full-window avg/min/max/spike, peak-occurrence time, plus
 * tail-window min/avg/max/spike that isolates inference-phase behavior.
 * compute_us is the Akida compute (DMA) time in microseconds for this event
 * (0 if not available, e.g. the learning path). */
void inference_current_dump(uint32_t compute_us, uint32_t);

/* --- Run aggregator (CLI bench mode) ------------------------------------
 * While a run is armed, every per-event dump folds itself into a run-wide
 * accumulator. The aggregator finishes itself when run_count reaches
 * target_n, or via inference_current_run_finish() on abort. */

/* Arm the run aggregator. target_n is informational + the auto-finish
 * trigger. Resets all run_* state. */
void inference_current_run_start(uint32_t target_n);

/* True if a run is currently armed and accumulating events. */
bool inference_current_run_active(void);

/* Emit the run-wide summary line and clear the aggregator. Idempotent. */
void inference_current_run_finish(void);

#ifdef __cplusplus
}
#endif

#endif