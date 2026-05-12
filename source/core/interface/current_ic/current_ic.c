#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

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
 * Two parallel statistic sets are maintained at the same INF_SAMPLE_PERIOD_US
 * cadence:
 *
 *   1) Full-window running accumulators (inf_sum / inf_min / inf_max /
 *      inf_max_idx / inf_count). These cover the entire 17 ms window with
 *      O(1) RAM. The avg derived from them reflects the SPI-transfer-
 *      dominated mean and is useful as a per-event energy proxy.
 *
 *   2) Tail ring (inf_tail[INF_TAIL_LEN]) overwriting oldest on wrap. By the
 *      time the done-IRQ fires, the ring holds the most recent N samples
 *      i.e. the last ~240 us ending at done-IRQ — mostly the 180 us Akida
 *      compute phase. Tail stats expose inference-phase spikes that the
 *      full-window check misses because its avg baseline is SPI-dominated.
 *
 * Signal chain end-to-end:
 *   I_rail -> R_shunt (0.2 ohm) -> INA190A1 (gain 25 V/V) -> AIN1 -> SAADC
 *   raw 12-bit code -> mV (adc_scale) -> mA (/ SHUNT_RESISTOR_GAIN_0V8 = 5).
 *   Resolution ~0.088 mA/code, full scale ~360 mA.
 *
 * ADC path isolation: a dedicated adc_sequence (inf_seq) and raw buffer
 * (inf_raw_buf) are used so this sampler cannot collide with the BLE-
 * streaming current_data_thread that shares the SAADC peripheral via
 * sequences[].
 *
 * Concurrency model:
 *   - inf_period_timer fires every INF_SAMPLE_PERIOD_US in ISR context and
 *     submits inf_sample_work to the system workqueue; the blocking ADC
 *     read runs in thread context (adc_read_dt cannot run from an ISR).
 *   - inf_active (atomic) gates both the work submit (in the timer ISR) and
 *     the work handler itself, so a handler that was queued just before
 *     stop bails out cleanly when it eventually runs.
 *   - inference_current_stop() uses atomic_cas so it is idempotent — the
 *     normal stop path (Akida done-IRQ -> akd_async_isr_handler in gpio.c)
 *     and the fallback (50 ms timeout) can both fire safely.
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
static uint16_t            inf_raw_buf;       /* SAADC raw read target */
static struct adc_sequence inf_seq;           /* sequence bound to AIN1 (CH1) */
static bool                inf_seq_inited;    /* lazy-init guard for inf_seq */

/* Full-window running accumulators. Reset in inference_current_start(). */
static float    inf_sum;        /* sum of all mA samples; /count -> mean */
static float    inf_min;        /* lowest sample seen; init +1e9 sentinel */
static float    inf_max;        /* highest sample seen; init -1e9 sentinel */
static uint32_t inf_max_idx;    /* sample index where inf_max was recorded */
static uint32_t inf_count;      /* number of samples taken so far */
static atomic_t inf_active = ATOMIC_INIT(0);  /* 1 while sampling armed */

/* Tail ring: overwrites oldest. After >= INF_TAIL_LEN samples, contents are
 * always the most-recent INF_TAIL_LEN values (slot order does not matter
 * because tail stats are order-independent sum/max). */
static float    inf_tail[INF_TAIL_LEN];
static uint8_t  inf_tail_head;  /* next write index, 0..INF_TAIL_LEN-1 */

static struct k_timer inf_period_timer;   /* periodic, INF_SAMPLE_PERIOD_US */
static struct k_timer inf_timeout_timer;  /* one-shot, INF_SAMPLE_TIMEOUT_US */
static struct k_work  inf_sample_work;    /* runs ADC read in thread ctx */

/* Runs in workqueue thread context, submitted by inf_period_expiry every
 * INF_SAMPLE_PERIOD_US. Performs one SAADC conversion (10 us ACQ + ~2 us
 * convert), folds the result into the full-window accumulators, and pushes
 * the raw mA into the tail ring. The inf_active guard catches the race
 * where stop fires after the work was submitted but before it runs. */
static void inf_sample_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  if (!atomic_get(&inf_active)) {
    return;
  }
  int err = adc_read_dt(&adc_channels[ADC_CH_1], &inf_seq);
  if (err < 0) {
    return;
  }
  float val_mv = (float)(int32_t)inf_raw_buf * adc_scale;
  float mA = val_mv / SHUNT_RESISTOR_GAIN_0V8;
  inf_sum += mA;
  if (mA < inf_min) inf_min = mA;
  /* inf_max_idx captured BEFORE inf_count++ so it stays 0-based: first
   * sample is index 0, elapsed-us = inf_max_idx * INF_SAMPLE_PERIOD_US. */
  if (mA > inf_max) { inf_max = mA; inf_max_idx = inf_count; }
  inf_tail[inf_tail_head] = mA;
  inf_tail_head = (inf_tail_head + 1U) % INF_TAIL_LEN;
  inf_count++;
}

/* Periodic timer ISR — kicks off a sample read via the workqueue (cannot
 * call adc_read_dt from ISR context). Guarded by inf_active so cancelled
 * sessions don't keep submitting work. */
static void inf_period_expiry(struct k_timer *t) {
  ARG_UNUSED(t);
  if (atomic_get(&inf_active)) {
    k_work_submit(&inf_sample_work);
  }
}

/* Fallback timeout — only fires if the Akida done-IRQ never arrives. The
 * normal stop path is akd_async_isr_handler in gpio.c calling
 * inference_current_stop() when the chip signals completion. */
static void inf_timeout_expiry(struct k_timer *t) {
  ARG_UNUSED(t);
  inference_current_stop();
}

/**
 * @brief Arm the per-inference 0V8 current sampler.
 *
 * Called from the inference call site just before akida_enqueue(). Cancels
 * any in-flight session, lazy-inits the dedicated ADC sequence on first
 * call, resets all accumulators + the tail ring, then starts the periodic
 * sampling timer and the safety-stop timeout.
 *
 * The +/-1e9 sentinels on inf_min / inf_max guarantee the first real sample
 * wins both comparisons unconditionally.
 */
void inference_current_start(void) {
  /* Cancel any in-flight session before resetting state. */
  atomic_set(&inf_active, 0);
  k_timer_stop(&inf_period_timer);
  k_timer_stop(&inf_timeout_timer);

  if (!inf_seq_inited) {
    inf_seq = (struct adc_sequence){
        .buffer      = &inf_raw_buf,
        .buffer_size = sizeof(inf_raw_buf),
    };
    int err = adc_sequence_init_dt(&adc_channels[ADC_CH_1], &inf_seq);
    if (err < 0) {
      printk("INF[0V8]: seq init failed (%d)\n", err);
      return;
    }
    inf_seq_inited = true;
  }

  inf_sum       = 0.0f;
  inf_min       = 1e9f;   /* sentinel: any real sample is smaller */
  inf_max       = -1e9f;  /* sentinel: any real sample is larger */
  inf_max_idx   = 0;
  inf_tail_head = 0;      /* inf_tail[] contents become valid as samples land;
                             at dump we only read min(inf_count, INF_TAIL_LEN) slots */
  inf_count     = 0;
  atomic_set(&inf_active, 1);
  k_timer_start(&inf_period_timer,
                K_USEC(INF_SAMPLE_PERIOD_US),
                K_USEC(INF_SAMPLE_PERIOD_US));
  k_timer_start(&inf_timeout_timer,
                K_USEC(INF_SAMPLE_TIMEOUT_US),
                K_NO_WAIT);
}

/**
 * @brief Stop the sampler. Safe to call from ISR or thread, idempotent.
 *
 * Two paths invoke this: the Akida done-IRQ via akd_async_isr_handler in
 * gpio.c (normal end-of-inference), and the safety timeout (fallback if
 * that IRQ never arrives). The atomic_cas(1, 0) ensures only the first
 * caller actually performs the timer stops; the second call is a no-op.
 */
void inference_current_stop(void) {
  if (!atomic_cas(&inf_active, 1, 0)) {
    return;
  }
  k_timer_stop(&inf_period_timer);
  k_timer_stop(&inf_timeout_timer);
}

/**
 * @brief Emit a one-line summary of the captured inference event.
 *
 * Called from akd_async_thread after akida_fetch() succeeds. Computes the
 * full-window mean / spike flag and the tail-window mean / spike flag, then
 * prints both on a single line so the two phases can be compared at a
 * glance:
 *
 *   spike=0, tail_spike=0  -- no excursion in either window
 *   spike=1, tail_spike=0  -- excursion during SPI transfer (use 'at' to confirm)
 *   spike=0, tail_spike=1  -- excursion during inference compute only
 *   spike=1, tail_spike=1  -- excursion in both, or one large event near transition
 *
 * Tail stats iterate inf_tail[] in slot order (not chronological); this is
 * safe because sum and max are order-independent.
 */
void inference_current_dump(void) {
  uint32_t n = inf_count;
  if (n == 0U) {
    printk("INF[0V8]: no samples\n");
    return;
  }

  /* Full-window derived metrics. */
  float    avg    = inf_sum / (float)n;
  int      spike  = (inf_max > avg + INF_SPIKE_THRESH_MA) ? 1 : 0;
  uint32_t dur_us = n * INF_SAMPLE_PERIOD_US;
  uint32_t max_us = inf_max_idx * INF_SAMPLE_PERIOD_US;

  /* Tail-window stats: average + max over the last min(n, INF_TAIL_LEN)
   * samples — these land in the ~240 us ending at done-IRQ, i.e. mostly
   * the Akida compute phase, not the SPI transfer. */
  uint8_t tail_n = (inf_count < INF_TAIL_LEN) ? (uint8_t)inf_count : (uint8_t)INF_TAIL_LEN;
  float   tail_sum = 0.0f;
  float   tail_max = -1e9f;
  for (uint8_t i = 0; i < tail_n; i++) {
    tail_sum += inf_tail[i];
    if (inf_tail[i] > tail_max) tail_max = inf_tail[i];
  }
  float tail_avg   = tail_sum / (float)tail_n;
  int   tail_spike = (tail_max > tail_avg + INF_SPIKE_THRESH_MA) ? 1 : 0;

  /* Manual sign + integer + 2-decimal fraction extraction below, repeated
   * for each mA field. printk in this build does not support %f, so floats
   * are split into "sign string" + "%d.%02d" by hand. */
  int   a_sign  = (avg < 0.0f) ? -1 : 1;
  float a_abs   = avg * a_sign;
  int   a_int   = (int)a_abs;
  int   a_frac  = (int)((a_abs - a_int) * 100.0f);

  int   mn_sign = (inf_min < 0.0f) ? -1 : 1;
  float mn_abs  = inf_min * mn_sign;
  int   mn_int  = (int)mn_abs;
  int   mn_frac = (int)((mn_abs - mn_int) * 100.0f);

  int   mx_sign = (inf_max < 0.0f) ? -1 : 1;
  float mx_abs  = inf_max * mx_sign;
  int   mx_int  = (int)mx_abs;
  int   mx_frac = (int)((mx_abs - mx_int) * 100.0f);

  int   ta_sign = (tail_avg < 0.0f) ? -1 : 1;
  float ta_abs  = tail_avg * ta_sign;
  int   ta_int  = (int)ta_abs;
  int   ta_frac = (int)((ta_abs - ta_int) * 100.0f);

  int   tx_sign = (tail_max < 0.0f) ? -1 : 1;
  float tx_abs  = tail_max * tx_sign;
  int   tx_int  = (int)tx_abs;
  int   tx_frac = (int)((tx_abs - tx_int) * 100.0f);

  printk("INF[0V8] N=%u dur=%u.%03u ms avg=%s%d.%02d min=%s%d.%02d max=%s%d.%02d mA at=%u.%03u ms spike=%d | tail_N=%u tail_avg=%s%d.%02d tail_max=%s%d.%02d mA tail_spike=%d\n",
         (unsigned)n,
         (unsigned)(dur_us / 1000U), (unsigned)(dur_us % 1000U),
         (a_sign  < 0 ? "-" : ""), a_int,  a_frac,
         (mn_sign < 0 ? "-" : ""), mn_int, mn_frac,
         (mx_sign < 0 ? "-" : ""), mx_int, mx_frac,
         (unsigned)(max_us / 1000U), (unsigned)(max_us % 1000U),
         spike,
         (unsigned)tail_n,
         (ta_sign < 0 ? "-" : ""), ta_int, ta_frac,
         (tx_sign < 0 ? "-" : ""), tx_int, tx_frac,
         tail_spike);
}
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

  k_timer_init(&inf_period_timer,  inf_period_expiry,  NULL);
  k_timer_init(&inf_timeout_timer, inf_timeout_expiry, NULL);
  k_work_init (&inf_sample_work,   inf_sample_work_handler);

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
