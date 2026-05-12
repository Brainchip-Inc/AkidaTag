#ifndef CURRENT_IC_H
#define CURRENT_IC_H

#define CURRENT_STACK_SIZE 2048
#define CURRENT_PRIORITY 5
#define AVG_SAMPLES 20
#define ADC_MAX_VALUE 4095.0f
#define ADC_REF_MV_1v8 1800.0f
#define SHUNT_RESISTOR_GAIN_1V8 (25.0f * 1.0f) /*(GAIN * Resistor)*/
#define SHUNT_RESISTOR_GAIN_0V8 (25.0f * 0.2f) /*(GAIN * Resistor)*/

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

/* Sample cadence. 40 us is past the INA190A1 step-settling time (~30 us per
 * datasheet SBOS863D) and yields ~4-5 samples inside the 180 us Akida
 * compute phase. */
#define INF_SAMPLE_PERIOD_US  40

/* Safety stop. The chip's done-IRQ normally ends sampling well before this
 * fires; the timeout only kicks in if that IRQ never arrives. */
#define INF_SAMPLE_TIMEOUT_US 50000

/* Tail-ring depth. 6 * 40 us = 240 us trailing window — covers the 180 us
 * inference compute with margin against timer-phase jitter, while keeping
 * the tail mean dominated by inference (not SPI-transfer) current. */
#define INF_TAIL_LEN          6

/* Spike threshold: a sample is flagged as a spike when it exceeds the mean
 * by this many mA. Applied to both full-window and tail stat sets. */
#define INF_SPIKE_THRESH_MA   50.0f

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
 * tail-window avg/max/spike that isolates inference-phase behavior. */
void inference_current_dump(void);

#ifdef __cplusplus
}
#endif

#endif