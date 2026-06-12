#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "ble_services/ble_initialization.h"
#include "current_ic/current_ic.h"
#include "led/led_init.h"
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define ADC_CH_0 0
#define ADC_CH_1 1

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) ||                                   \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DATA_BUFF_SIZE 32
#define DT_SPEC_AND_COMMA(node_id, prop, idx)                                  \
  ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)};

static const float adc_scale = ADC_REF_MV_1v8 / (float)ADC_MAX_VALUE;

#define CHRG_STS1_NODE DT_NODELABEL(chgr_sts1)
#define CHRG_STS2_NODE DT_NODELABEL(chgr_sts2)

static const struct gpio_dt_spec chgr_sts1 =
    GPIO_DT_SPEC_GET(CHRG_STS1_NODE, gpios);

static const struct gpio_dt_spec chgr_sts2 =
    GPIO_DT_SPEC_GET(CHRG_STS2_NODE, gpios);

static uint16_t buf;
static struct adc_sequence sequences[ARRAY_SIZE(adc_channels)];

/* ============================================================================
 * Per-inference 0V8 current sampler
 *
 * Goal: characterize the 0V8 rail current drawn during one Akida inference
 * event, from akida_enqueue() to the chip's done-IRQ.
 *
 * Captured window on this board (~17 ms total):
 *   - ~16.8 ms  SPI/DMA frame transfer to the Akida chip (dominant)
 *   - ~180 us   actual Akida compute (small, but the part of interest)
 *
 * Sampling model:
 *   A single interval-paced buffered adc_read_async() per inference fills
 *   inf_samples[] (raw 12-bit codes) with ~no per-sample CPU — the driver
 *   paces samples at INF_SAMPLE_PERIOD_US and writes each into the buffer.
 *   This replaced an earlier per-sample timer+workqueue scheme whose per-sample
 *   overhead measurably slowed the inference. The buffer is reduced to stats in
 *   a single batch pass at dump time:
 *     - Full-window: sum/min/max/max_idx over all N samples -> mean (energy
 *       proxy), dominated by the SPI-transfer current.
 *     - Tail: the last INF_TAIL_LEN samples (chronological, ending at the
 *       done-IRQ) -> the Akida compute phase.
 *
 * Signal chain end-to-end:
 *   I_rail -> R_shunt (0.2 ohm) -> INA190A1 (gain 25 V/V) -> AIN1 -> SAADC
 *   raw 12-bit code -> mV (adc_scale) -> mA (/ SHUNT_RESISTOR_GAIN_0V8 = 5).
 *   Resolution ~0.088 mA/code, full scale ~360 mA.
 *
 * ADC path isolation: a dedicated adc_sequence (inf_seq) + buffer (inf_samples)
 * are used so this sampler cannot collide with the BLE-streaming
 * current_data_thread that shares the SAADC peripheral via sequences[].
 *
 * Concurrency model:
 *   - inference_current_start() (bench only) kicks off a fixed-count async read
 *     of inf_cap_n samples; the driver fills inf_samples[] and raises
 *     inf_async_sig on completion.
 *   - inference_current_stop() (Akida done-IRQ via gpio.c) is a no-op — the read
 *     self-completes after inf_cap_n samples.
 *   - inference_current_dump() waits on inf_async_sig (k_poll), trims to the
 *     real inference window (inf_time), then batch-processes the buffer.
 *
 * Output: one summary line per inference via printk in inference_current_dump.
 * Format:
 *   INF[0V8] N=<n> dur=<ms> avg=<mA> min=<mA> max=<mA> at=<ms> spike=<0|1>
 *          | tail_N=<n> tail_avg=<mA> tail_max=<mA> tail_spike=<0|1>
 *   - 'at' is the elapsed time from enqueue to the peak sample; values near
 *     the printed 'dur' indicate the peak fell in the inference phase, lower
 *     values indicate it occurred during SPI transfer.
 *   - spike=0 with tail_spike=1 is the new diagnostic: an excursion was
 *     present in the inference phase only.
 * ============================================================================
 */
static struct adc_sequence inf_seq;          /* sequence bound to AIN1 (CH1) */
static struct adc_sequence_options inf_opts; /* interval pacing (no callback) */
static bool inf_seq_inited;                  /* lazy-init guard for inf_seq */

/* Raw SAADC sample buffer, filled by ONE fixed-count, interval-paced
 * adc_read_async per inference (no per-sample timer/workqueue). The read
 * collects inf_cap_n samples over inf_cap_n * INF_SAMPLE_PERIOD_US, then raises
 * inf_async_sig. At dump the buffer is converted to mA + reduced to stats in a
 * single batch pass, trimmed to the real inference window via inf_time. */
static int16_t inf_samples[INF_MAX_SAMPLES];
static uint16_t inf_cap_n;                 /* samples requested this capture */
static struct k_poll_signal inf_async_sig; /* adc_read_async completion signal */

/* Window timing comes from main.cpp's time_ms() enqueue->fetch measurement,
 * passed into inference_current_dump() as inf_time (ms). The sampler no longer
 * times the window itself. */

/* Current-band thresholds (mA), runtime-adjustable via `cmeas_thresh`. Stored
 * as float for comparison; set in whole mA from the shell. */
static float inf_hi_thresh_ma = INF_HI_THRESH_MA;
static float inf_lo_thresh_ma = INF_LO_THRESH_MA;

/* --- Run aggregator (CLI bench mode) -------------------------------------
 * While run_active is true, every successful inference_current_dump() folds
 * its per-event stats into the run_* accumulators. When run_count reaches
 * run_target_n the aggregator auto-finishes (prints the summary, clears
 * itself). doubles are used for the sums to keep precision over 1000+
 * events at hundreds of mA — float accumulation drifts past ~2^24. */
static bool run_active;
static uint32_t run_target_n;
static uint32_t run_count;
static double run_avg_sum;
static double run_tail_avg_sum;
static float run_min;
static float run_max;
static float run_tail_max;
static uint32_t run_spike_events;
static uint32_t run_tail_spike_events;
/* Energy accumulation: run_energy_uj sums per-event energy (uJ); run_dur_us
 * sums per-event window duration (us). Both use double for the same precision
 * rationale as run_avg_sum — float drifts past ~2^24 over 1000+ events. The
 * run mean power is derived as run_energy_uj / run_dur_us. */
static double run_energy_uj;
static double run_dur_us;
/* Band counters summed across the run, plus total sample count for percentages. */
static uint32_t run_hi_count;     /* total samples > inf_hi_thresh_ma */
static uint32_t run_lo_count;     /* total samples < inf_lo_thresh_ma */
static uint32_t run_sample_count; /* total samples across the run */
/* Tail-min across the run, and sum of per-event Akida compute (DMA) times for
 * the run-wide mean. */
static float run_tail_min;        /* min tail sample across run (sentinel 1e9) */
static double run_compute_us_sum; /* sum of per-event compute_us */

/**
 * @brief Arm the per-inference 0V8 current sampler.
 *
 * Called from the inference call site just before akida_enqueue(). No-op
 * unless a cmeas_start bench run is active, so live inference is never sampled
 * (zero overhead / no perturbation). Otherwise lazy-inits the dedicated ADC
 * sequence on first call and kicks off one fixed-count, interval-paced buffered
 * read into inf_samples[]; the driver fills the buffer with ~no per-sample CPU
 * and raises inf_async_sig when done. The dump reduces it to stats.
 */
void inference_current_start(void) {
  /* Only sample during a cmeas_start bench run. */
  if (!run_active) {
    return;
  }

  /* Number of samples for this capture: cover INF_CAP_WINDOW_US at the current
   * cadence, capped by the buffer. */
  uint32_t k = INF_CAP_WINDOW_US / INF_SAMPLE_PERIOD_US;
  if (k < 1U) {
    k = 1U;
  }
  if (k > INF_MAX_SAMPLES) {
    k = INF_MAX_SAMPLES;
  }
  inf_cap_n = (uint16_t)k;

  if (!inf_seq_inited) {
    inf_seq = (struct adc_sequence){
        .buffer = inf_samples,
        .buffer_size = sizeof(inf_samples),
    };
    int err = adc_sequence_init_dt(&adc_channels[ADC_CH_1], &inf_seq);
    if (err < 0) {
      printk("INF[0V8]: seq init failed (%d)\n", err);
      return;
    }
    inf_seq_inited = true;
  }

  /* No per-sample callback: the driver collects exactly inf_cap_n samples
   * (interval-paced) into the buffer, then raises inf_async_sig. */
  inf_opts = (struct adc_sequence_options){
      .interval_us = INF_SAMPLE_PERIOD_US,
      .callback = NULL,
      .extra_samplings = (uint16_t)(inf_cap_n - 1U),
  };
  inf_seq.options = &inf_opts;

  k_poll_signal_reset(&inf_async_sig);

  int err = adc_read_async(adc_channels[ADC_CH_1].dev, &inf_seq, &inf_async_sig);
  if (err < 0) {
    printk("INF[0V8]: read_async failed (%d)\n", err);
    inf_cap_n = 0;
  }
}

/**
 * @brief Stop hook, called from the Akida done-IRQ (gpio.c). With the
 * fixed-count buffered read the capture self-completes after inf_cap_n samples,
 * so there is nothing to stop here — kept as a no-op for the call site.
 */
void inference_current_stop(void) {}

/**
 * @brief Emit a one-line summary of the captured inference event.
 *
 * Called from akd_async_thread after akida_fetch() succeeds. Computes the
 * full-window mean / spike flag and the tail-window mean / spike flag, then
 * prints both on a single line so the two phases can be compared at a
 * glance:
 *
 *   spike=0, tail_spike=0  -- no excursion in either window
 *   spike=1, tail_spike=0  -- excursion during SPI transfer (use 'at' to
 * confirm) spike=0, tail_spike=1  -- excursion during inference compute only
 *   spike=1, tail_spike=1  -- excursion in both, or one large event near
 * transition
 *
 * Tail stats use the last INF_TAIL_LEN samples of inf_samples[] (chronological,
 * ending at the done-IRQ).
 */
void inference_current_dump(uint32_t compute_us, uint32_t inf_time) {
  /* Only emit a line during a cmeas_start bench run; live inference and the
   * learning path are silent. */
  if (!run_active) {
    return;
  }
  if (inf_cap_n == 0U) {
    printk("INF[0V8]: no samples\n");
    return;
  }

  /* Wait for the buffered read to finish so inf_samples[] is stable. The read
   * collects inf_cap_n samples then raises inf_async_sig. */
  struct k_poll_event ev = K_POLL_EVENT_INITIALIZER(
      K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &inf_async_sig);
  (void)k_poll(&ev, 1, K_MSEC(200));

  /* Trim to the real inference window: the capture spans INF_CAP_WINDOW_US, but
   * the inference is only inf_time ms — process the leading samples that fall
   * within it (the rest is post-inference idle). */
  uint32_t win_n = (inf_time * 1000U) / INF_SAMPLE_PERIOD_US;
  if (win_n == 0U) {
    win_n = 1U;
  }
  uint32_t n = (win_n < inf_cap_n) ? win_n : inf_cap_n;

  /* Batch pass: convert raw SAADC codes -> mA and reduce to full-window stats.
   * Done once here in thread context (floats fine; nothing runs per-sample in
   * an ISR), which is what removes the per-sample perturbation of the
   * inference. */
  float inf_sum = 0.0f;
  float inf_min = 1e9f;  /* sentinel: any real sample is smaller */
  float inf_max = -1e9f; /* sentinel: any real sample is larger */
  uint32_t inf_max_idx = 0;
  uint32_t inf_hi_count = 0;
  uint32_t inf_lo_count = 0;
  for (uint32_t i = 0; i < n; i++) {
    float mA =
        ((float)(int32_t)inf_samples[i] * adc_scale) / SHUNT_RESISTOR_GAIN_0V8;
    inf_sum += mA;
    if (mA > inf_hi_thresh_ma)
      inf_hi_count++;
    if (mA < inf_lo_thresh_ma)
      inf_lo_count++;
    if (mA < inf_min)
      inf_min = mA;
    if (mA > inf_max) {
      inf_max = mA;
      inf_max_idx = i;
    }
  }

  /* Window duration is the measured enqueue->fetch wall-clock time (time_ms,
   * in ms), passed in as inf_time. Effective per-sample spacing (tsamp_us) and
   * peak time (max_us) are derived from it assuming uniform sampling. */
  float avg = inf_sum / (float)n;
  int spike = (inf_max > avg + INF_SPIKE_THRESH_MA) ? 1 : 0;
  uint32_t inf_time_us = inf_time * 1000U;
  uint32_t tsamp_us = inf_time_us / n;
  uint32_t max_us = (uint32_t)((uint64_t)inf_max_idx * inf_time_us / n);

  /* Power & energy on the 0V8 rail (rail voltage assumed constant — see
   * RAIL_VOLTAGE_0V8). power_mw = V * avg (mean power); energy = mean power *
   * measured inference time, mW * ms = uJ. */
  float power_mw = RAIL_VOLTAGE_0V8 * avg;
  float energy_uj = power_mw * (float)inf_time;

  /* Tail-window stats: the last min(n, INF_TAIL_LEN) samples — chronological,
   * ending at the done-IRQ, i.e. the Akida compute phase. */
  uint8_t tail_n = (n < INF_TAIL_LEN) ? (uint8_t)n : (uint8_t)INF_TAIL_LEN;
  float tail_sum = 0.0f;
  float tail_min = 1e9f;  /* sentinel: any real sample is smaller */
  float tail_max = -1e9f; /* sentinel: any real sample is larger */
  for (uint8_t i = 0; i < tail_n; i++) {
    float mA = ((float)(int32_t)inf_samples[n - tail_n + i] * adc_scale) /
               SHUNT_RESISTOR_GAIN_0V8;
    tail_sum += mA;
    if (mA > tail_max)
      tail_max = mA;
    if (mA < tail_min)
      tail_min = mA;
  }
  float tail_avg = tail_sum / (float)tail_n;
  int tail_spike = (tail_max > tail_avg + INF_SPIKE_THRESH_MA) ? 1 : 0;

  /* Manual sign + integer + 2-decimal fraction extraction below, repeated
   * for each mA field. printk in this build does not support %f, so floats
   * are split into "sign string" + "%d.%02d" by hand. */
  int a_sign = (avg < 0.0f) ? -1 : 1;
  float a_abs = avg * a_sign;
  int a_int = (int)a_abs;
  int a_frac = (int)((a_abs - a_int) * 100.0f);

  int mn_sign = (inf_min < 0.0f) ? -1 : 1;
  float mn_abs = inf_min * mn_sign;
  int mn_int = (int)mn_abs;
  int mn_frac = (int)((mn_abs - mn_int) * 100.0f);

  int mx_sign = (inf_max < 0.0f) ? -1 : 1;
  float mx_abs = inf_max * mx_sign;
  int mx_int = (int)mx_abs;
  int mx_frac = (int)((mx_abs - mx_int) * 100.0f);

  int tm_sign = (tail_min < 0.0f) ? -1 : 1;
  float tm_abs = tail_min * tm_sign;
  int tm_int = (int)tm_abs;
  int tm_frac = (int)((tm_abs - tm_int) * 100.0f);

  int ta_sign = (tail_avg < 0.0f) ? -1 : 1;
  float ta_abs = tail_avg * ta_sign;
  int ta_int = (int)ta_abs;
  int ta_frac = (int)((ta_abs - ta_int) * 100.0f);

  int tx_sign = (tail_max < 0.0f) ? -1 : 1;
  float tx_abs = tail_max * tx_sign;
  int tx_int = (int)tx_abs;
  int tx_frac = (int)((tx_abs - tx_int) * 100.0f);

  int pw_sign = (power_mw < 0.0f) ? -1 : 1;
  float pw_abs = power_mw * pw_sign;
  int pw_int = (int)pw_abs;
  int pw_frac = (int)((pw_abs - pw_int) * 100.0f);

  int en_sign = (energy_uj < 0.0f) ? -1 : 1;
  float en_abs = energy_uj * en_sign;
  int en_int = (int)en_abs;
  int en_frac = (int)((en_abs - en_int) * 100.0f);

  printk("INF[0V8] N=%u inf_time=%u ms Tsamp=%u us avg=%s%d.%02d min=%s%d.%02d "
         "max=%s%d.%02d mA at=%u.%03u ms spike=%d | tail_N=%u "
         "tail_min=%s%d.%02d tail_avg=%s%d.%02d tail_max=%s%d.%02d mA "
         "tail_spike=%d | dma=%u us | pwr=%s%d.%02d mW E=%s%d.%02d uJ | "
         "hi=%u lo=%u\n",
         (unsigned)n, (unsigned)inf_time,
         (unsigned)tsamp_us, (a_sign < 0 ? "-" : ""), a_int, a_frac,
         (mn_sign < 0 ? "-" : ""),
         mn_int, mn_frac, (mx_sign < 0 ? "-" : ""), mx_int, mx_frac,
         (unsigned)(max_us / 1000U), (unsigned)(max_us % 1000U), spike,
         (unsigned)tail_n, (tm_sign < 0 ? "-" : ""), tm_int, tm_frac,
         (ta_sign < 0 ? "-" : ""), ta_int, ta_frac, (tx_sign < 0 ? "-" : ""),
         tx_int, tx_frac, tail_spike, (unsigned)compute_us,
         (pw_sign < 0 ? "-" : ""), pw_int, pw_frac, (en_sign < 0 ? "-" : ""),
         en_int, en_frac, (unsigned)inf_hi_count, (unsigned)inf_lo_count);

  /* Fold this event into the run aggregator if a bench run is armed. The
   * fold reuses values already computed above — no extra arithmetic in the
   * sampling path. Auto-finish once we reach the target count so the
   * audio-thread side and the done-IRQ side don't race over who finishes. */
  if (run_active) {
    run_avg_sum += (double)avg;
    run_tail_avg_sum += (double)tail_avg;
    run_energy_uj += (double)energy_uj;
    run_dur_us += (double)inf_time_us;
    run_hi_count += inf_hi_count;
    run_lo_count += inf_lo_count;
    run_sample_count += n;
    run_compute_us_sum += (double)compute_us;
    if (inf_min < run_min)
      run_min = inf_min;
    if (inf_max > run_max)
      run_max = inf_max;
    if (tail_min < run_tail_min)
      run_tail_min = tail_min;
    if (tail_max > run_tail_max)
      run_tail_max = tail_max;
    if (spike)
      run_spike_events++;
    if (tail_spike)
      run_tail_spike_events++;
    run_count++;
    if (run_count >= run_target_n) {
      inference_current_run_finish();
    }
  }
}

/**
 * @brief Arm the run aggregator for a multi-inference bench run.
 *
 * After this call, each per-event inference_current_dump() folds its stats
 * into the run_* accumulators. The aggregator auto-finishes (prints the
 * summary, clears itself) once run_count reaches target_n.
 */
void inference_current_run_start(uint32_t target_n) {
  run_active = false; /* clear before reset so dump-hook sees false */
  run_target_n = target_n;
  run_count = 0;
  run_avg_sum = 0.0;
  run_tail_avg_sum = 0.0;
  run_min = 1e9f;
  run_max = -1e9f;
  run_tail_max = -1e9f;
  run_spike_events = 0;
  run_tail_spike_events = 0;
  run_energy_uj = 0.0;
  run_dur_us = 0.0;
  run_hi_count = 0;
  run_lo_count = 0;
  run_sample_count = 0;
  run_tail_min = 1e9f;
  run_compute_us_sum = 0.0;
  run_active = true;
}

bool inference_current_run_active(void) { return run_active; }

/**
 * @brief Emit the run-wide summary and clear the aggregator.
 *
 * Idempotent — second call (e.g. abort followed by natural completion) is a
 * no-op. Uses the same manual sign + int + 2-decimal print style as the
 * per-event dump since this build's printk lacks %f.
 */
void inference_current_run_finish(void) {
  if (!run_active) {
    return;
  }
  run_active = false;

  if (run_count == 0U) {
    printk("INF[0V8] RUN_DONE N=0/%u (no events captured)\n",
           (unsigned)run_target_n);
    return;
  }

  float r_avg = (float)(run_avg_sum / (double)run_count);
  float r_tail_avg = (float)(run_tail_avg_sum / (double)run_count);

  int ra_sign = (r_avg < 0.0f) ? -1 : 1;
  float ra_abs = r_avg * ra_sign;
  int ra_int = (int)ra_abs;
  int ra_frac = (int)((ra_abs - ra_int) * 100.0f);

  int rn_sign = (run_min < 0.0f) ? -1 : 1;
  float rn_abs = run_min * rn_sign;
  int rn_int = (int)rn_abs;
  int rn_frac = (int)((rn_abs - rn_int) * 100.0f);

  int rx_sign = (run_max < 0.0f) ? -1 : 1;
  float rx_abs = run_max * rx_sign;
  int rx_int = (int)rx_abs;
  int rx_frac = (int)((rx_abs - rx_int) * 100.0f);

  int rta_sign = (r_tail_avg < 0.0f) ? -1 : 1;
  float rta_abs = r_tail_avg * rta_sign;
  int rta_int = (int)rta_abs;
  int rta_frac = (int)((rta_abs - rta_int) * 100.0f);

  int rtn_sign = (run_tail_min < 0.0f) ? -1 : 1;
  float rtn_abs = run_tail_min * rtn_sign;
  int rtn_int = (int)rtn_abs;
  int rtn_frac = (int)((rtn_abs - rtn_int) * 100.0f);

  int rtx_sign = (run_tail_max < 0.0f) ? -1 : 1;
  float rtx_abs = run_tail_max * rtx_sign;
  int rtx_int = (int)rtx_abs;
  int rtx_frac = (int)((rtx_abs - rtx_int) * 100.0f);

  /* Mean Akida compute (DMA) time over the run, in microseconds. */
  uint32_t dma_avg_us = (uint32_t)(run_compute_us_sum / (double)run_count);

  /* Effective per-sample period over the run = total measured window time /
   * total samples, in microseconds. Reveals the true cadence (~30-35 us). */
  uint32_t tsamp_avg_us =
      run_sample_count ? (uint32_t)(run_dur_us / (double)run_sample_count) : 0U;

  /* Energy/power run totals.
   *   E_total (mJ)  = summed per-event energy / 1000
   *   E_avg   (uJ)  = summed per-event energy / event count
   *   Pavg    (mW)  = E_total_energy / total_window_time; uJ/us == W, so *1000
   * for mW. run_dur_us is always > 0 here (run_count > 0 implies >=1 sample). */
  float e_total_mj = (float)(run_energy_uj / 1000.0);
  float e_avg_uj = (float)(run_energy_uj / (double)run_count);
  float p_avg_mw =
      (run_dur_us > 0.0) ? (float)(run_energy_uj / run_dur_us * 1000.0) : 0.0f;

  int et_sign = (e_total_mj < 0.0f) ? -1 : 1;
  float et_abs = e_total_mj * et_sign;
  int et_int = (int)et_abs;
  int et_frac = (int)((et_abs - et_int) * 100.0f);

  int ea_sign = (e_avg_uj < 0.0f) ? -1 : 1;
  float ea_abs = e_avg_uj * ea_sign;
  int ea_int = (int)ea_abs;
  int ea_frac = (int)((ea_abs - ea_int) * 100.0f);

  int rp_sign = (p_avg_mw < 0.0f) ? -1 : 1;
  float rp_abs = p_avg_mw * rp_sign;
  int rp_int = (int)rp_abs;
  int rp_frac = (int)((rp_abs - rp_int) * 100.0f);

  /* Band-count percentages of all run samples, in integer basis-points to
   * avoid %f: bp = count*10000/total → int=bp/100, frac=bp%100. */
  uint32_t hi_bp =
      run_sample_count
          ? (uint32_t)((uint64_t)run_hi_count * 10000U / run_sample_count)
          : 0U;
  uint32_t lo_bp =
      run_sample_count
          ? (uint32_t)((uint64_t)run_lo_count * 10000U / run_sample_count)
          : 0U;

  printk("INF[0V8] RUN_DONE N=%u/%u avg=%s%d.%02d min=%s%d.%02d max=%s%d.%02d "
         "mA | tail_min=%s%d.%02d tail_avg=%s%d.%02d tail_max=%s%d.%02d mA "
         "spike_events=%u tail_spike_events=%u | dma_avg=%u us Tsamp_avg=%u us | "
         "E_total=%s%d.%02d mJ E_avg=%s%d.%02d uJ "
         "Pavg=%s%d.%02d mW | hi>%dmA=%u(%u.%02u%%) lo<%dmA=%u(%u.%02u%%) "
         "of %u samples\n",
         (unsigned)run_count, (unsigned)run_target_n, (ra_sign < 0 ? "-" : ""),
         ra_int, ra_frac, (rn_sign < 0 ? "-" : ""), rn_int, rn_frac,
         (rx_sign < 0 ? "-" : ""), rx_int, rx_frac, (rtn_sign < 0 ? "-" : ""),
         rtn_int, rtn_frac, (rta_sign < 0 ? "-" : ""), rta_int, rta_frac,
         (rtx_sign < 0 ? "-" : ""), rtx_int, rtx_frac,
         (unsigned)run_spike_events, (unsigned)run_tail_spike_events,
         (unsigned)dma_avg_us, (unsigned)tsamp_avg_us,
         (et_sign < 0 ? "-" : ""), et_int, et_frac,
         (ea_sign < 0 ? "-" : ""), ea_int, ea_frac, (rp_sign < 0 ? "-" : ""),
         rp_int, rp_frac, (int)inf_hi_thresh_ma, (unsigned)run_hi_count,
         (unsigned)(hi_bp / 100U), (unsigned)(hi_bp % 100U),
         (int)inf_lo_thresh_ma, (unsigned)run_lo_count,
         (unsigned)(lo_bp / 100U), (unsigned)(lo_bp % 100U),
         (unsigned)run_sample_count);
}

/* Shell front-end for the inference bench (cmeas_start / cmeas_stop) lives
 * in core/common/inference_bench_cli.c — that file is built unconditionally
 * so the same CLI is available on the DK build, which delegates only the
 * inference-burst side and skips all current measurement. */

/* `cmeas_thresh` — view/set the current-band thresholds used by the per-sample
 * hi/lo counters. Registered here (Spark-only file) because the counters are a
 * current-measurement feature and do not exist on the DK build. Thresholds are
 * taken in whole mA to keep the shell free of %f. */
static int cmd_cmeas_thresh(const struct shell *sh, size_t argc, char **argv) {
  if (argc == 1) {
    shell_print(sh, "current thresholds: hi=%d mA lo=%d mA",
                (int)inf_hi_thresh_ma, (int)inf_lo_thresh_ma);
    return 0;
  }
  if (argc != 3) {
    shell_error(sh, "usage: cmeas_thresh [<hi_mA> <lo_mA>]");
    return -EINVAL;
  }

  char *hi_end;
  char *lo_end;
  unsigned long hi = strtoul(argv[1], &hi_end, 10);
  unsigned long lo = strtoul(argv[2], &lo_end, 10);
  if (*hi_end != '\0' || *lo_end != '\0') {
    shell_error(sh, "invalid threshold(s): hi=%s lo=%s", argv[1], argv[2]);
    return -EINVAL;
  }
  /* Full scale on the 0V8 rail is ~360 mA (4095 * ~0.088 mA/code). */
  if (hi > 360UL || lo > 360UL) {
    shell_error(sh, "threshold out of range (0..360 mA)");
    return -EINVAL;
  }
  if (hi <= lo) {
    shell_error(sh, "hi (%lu) must be greater than lo (%lu)", hi, lo);
    return -EINVAL;
  }

  inf_hi_thresh_ma = (float)hi;
  inf_lo_thresh_ma = (float)lo;
  shell_print(sh, "thresholds set: hi=%lu mA lo=%lu mA", hi, lo);
  return 0;
}

SHELL_CMD_REGISTER(cmeas_thresh, NULL,
                   "View/set hi/lo current-band thresholds (mA): "
                   "cmeas_thresh [<hi> <lo>]",
                   cmd_cmeas_thresh);

/**
 * @brief Initialize GPIO pins for battery charger status monitoring
 *
 * Checks readiness of the two charger status GPIO pins and configures
 * them as inputs for reading charger state information.
 *
 * @return int 0 on success, negative error code on failure:
 *         -ENODEV if GPIO device not ready or configuration fails
 */
int bat_sts_gpio_init(void) {
  int err;

  /* --- Check GPIO readiness --- */
  if (!gpio_is_ready_dt(&chgr_sts1)) {
    printk("Charging status1 GPIO not ready\n");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&chgr_sts2)) {
    printk("Charging status2 GPIO not ready\n");
    return -ENODEV;
  }
  /* --- Configure control pins as input --- */
  err = gpio_pin_configure_dt(&chgr_sts1, GPIO_INPUT);
  if (err) {
    printk("Failed to configure Charging status1 pin (err %d)\n", err);
    return err;
  }
  err = gpio_pin_configure_dt(&chgr_sts2, GPIO_INPUT);
  if (err) {
    printk("Failed to configure Charging status2 pin (err %d)\n", err);
    return err;
  }
  return 0;
}
/**
 * @brief Initialize ADC channels for current monitoring IC
 *
 * Configures all ADC channels defined in the adc_channels array.
 * Verifies each ADC controller device is ready and sets up channel
 * parameters (resolution, gain, differential mode, etc.) for current
 * sensing measurements.
 *
 * @return int 0 on success, -ENODEV if ADC device not ready or channel setup
 * fails
 */
int current_ic_init(void) {
  int err;

  /* Configure channels individually prior to sampling. */
  for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
    if (!adc_is_ready_dt(&adc_channels[i])) {
      printk("ADC controller device %s not ready\n", adc_channels[i].dev->name);
      return -ENODEV;
    }

    err = adc_channel_setup_dt(&adc_channels[i]);
    if (err < 0) {
      printk("Could not setup channel #%d (%d)\n", i, err);
      return -ENODEV;
    }
    sequences[i] = (struct adc_sequence){
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };
    err = adc_sequence_init_dt(&adc_channels[i], &sequences[i]);
    if (err < 0) {
      printk("ADC sequence init failed for channel %d: %d\n", i, err);
      return err;
    }
  }

  k_poll_signal_init(&inf_async_sig);

  return 0;
}
/**
 * @brief Get the current battery charger status
 *
 * Reads two GPIO status pins from the battery charger IC and decodes
 * the combined state to determine if the battery is charging, not charging,
 * or in a fault condition (recoverable or non-recoverable fault).
 *
 * @return bat_status_t Battery status code:
 *         - BAT_NOT_CHARGING: Battery not charging
 *         - BAT_CHARGING: Battery actively charging
 *         - BAT_FAULT_RECOVERABLE: Recoverable fault condition detected
 *         - BAT_FAULT_NON_RECOVERABLE: Non-recoverable fault condition detected
 */
bat_status check_bat_status(void) {
  int sts1 = gpio_pin_get_dt(&chgr_sts1);
  if (sts1 < 0) {
    printk("Failed to read CHGR_STS1: %d", sts1);
    return BAT_READ_FAILED;
  }

  int sts2 = gpio_pin_get_dt(&chgr_sts2);
  if (sts2 < 0) {
    printk("Failed to read CHGR_STS2: %d", sts2);
    return BAT_READ_FAILED;
  }

  if (sts1 == 1 && sts2 == 1) {
    return BAT_NOT_CHARGING;
  } else if (sts1 == 1 && sts2 == 0) {
    return BAT_CHARGING;
  } else if (sts1 == 0 && sts2 == 1) {
    printk("Charger Status: Recoverable fault\n");
    return BAT_FAULT_RECOVERABLE;
  } else {
    printk("Charger Status: Non-recoverable fault\n");
    return BAT_FAULT_NON_RECOVERABLE;
  }
}
/**
 *@brief Read and average current measurements from the current monitoring IC

 * Periodically samples ADC channels corresponding to different voltage rails.
 * Each channel is read up to AVG_SAMPLES times, and raw ADC values are
 converted
 * to millivolts and then to milliamps using the respective shunt resistor gain.

 * ADC read failures are skipped, and only successfully acquired samples are
 * accumulated. The average current for each channel is computed using the
 * number of valid samples to avoid bias due to read errors.

 * If no valid samples are available for a channel, its average is reported as
 zero.
 * The computed averages are formatted and transmitted over BLE while streaming
 * is enabled.
 */

void current_data_thread(void *a, void *b, void *c) {
  int err;
  char data[DATA_BUFF_SIZE] = {0};

  while (1) {

    k_sem_take(&current_stream_sem, K_FOREVER); // sleep until signaled

    printk("Current streaming started\n");

    while (current_stream_flag == FLAG_ENABLE) {

      // Guard: if BLE dropped, stop immediately
      if (!is_ble_connected()) {
        printk("BLE disconnected during current stream, stopping\n");
        current_stream_flag = FLAG_DISABLE;
        break;
      }
      float sum_1v8 = 0.0f;
      float sum_0v8 = 0.0f;
      int valid_cnt_1v8 = 0;
      int valid_cnt_0v8 = 0;
      for (int cnt = 0; cnt < AVG_SAMPLES; cnt++) {
        // Channel 0 — 1V8 rail
        err = adc_read_dt(&adc_channels[ADC_CH_0], &sequences[ADC_CH_0]);
        if (err < 0) {
          printk("CH0: read error (%d)\n", err);
          continue;
        }
        float val_mv = (float)(int32_t)buf * adc_scale;
        sum_1v8 += val_mv / SHUNT_RESISTOR_GAIN_1V8;
        valid_cnt_1v8++;
        k_msleep(10);
      }

      // Settling delay when switching channels
      k_msleep(1);

      // Channel 1 — 0V8 rail
      for (int cnt = 0; cnt < AVG_SAMPLES; cnt++) {
        err = adc_read_dt(&adc_channels[ADC_CH_1], &sequences[ADC_CH_1]);
        if (err < 0) {
          printk("CH1: read error (%d)\n", err);
          continue;
        }
        float val_mv = (float)(int32_t)buf * adc_scale;
        sum_0v8 += val_mv / SHUNT_RESISTOR_GAIN_0V8;
        valid_cnt_0v8++;
        k_msleep(10);
      }

      // Avoid divide-by-zero
      float avg_1v8 = (valid_cnt_1v8 > 0) ? (sum_1v8 / valid_cnt_1v8) : 0.0f;
      float avg_0v8 = (valid_cnt_0v8 > 0) ? (sum_0v8 / valid_cnt_0v8) : 0.0f;

      printk("CH0 1V8 Avg Current: %.2f mA (valid=%d)\n", (double)avg_1v8,
             valid_cnt_1v8);
      printk("CH1 0V8 Avg Current: %.2f mA (valid=%d)\n", (double)avg_0v8,
             valid_cnt_0v8);

      /* Handle sign separately */
      int sign_1v8 = (avg_1v8 < 0) ? -1 : 1;
      int sign_0v8 = (avg_0v8 < 0) ? -1 : 1;

      /* Work with absolute values */
      float abs_1v8 = avg_1v8 * sign_1v8;
      float abs_0v8 = avg_0v8 * sign_0v8;

      /* Split into integer and fractional parts */
      int int_1v8 = (int)abs_1v8;
      int frac_1v8 = (int)((abs_1v8 - int_1v8) * 100);

      int int_0v8 = (int)abs_0v8;
      int frac_0v8 = (int)((abs_0v8 - int_0v8) * 100);

      /* Format with sign added explicitly */
      snprintf(data, sizeof(data), "%s%d.%02d,%s%d.%02d",
               (sign_1v8 < 0 ? "-" : ""), int_1v8, frac_1v8,
               (sign_0v8 < 0 ? "-" : ""), int_0v8, frac_0v8);

      send_current_value(data);
      k_msleep(50);
    }
  }
}
