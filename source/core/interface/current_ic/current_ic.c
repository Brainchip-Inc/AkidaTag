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

/* Per-inference 0V8 sampler state: dedicated buffer + sequence so it cannot
 * collide with the BLE-streaming current_data_thread that uses sequences[]. */
static uint16_t            inf_raw_buf;
static struct adc_sequence inf_seq;
static bool                inf_seq_inited;

static float    inf_samples[INF_SAMPLE_BUF_LEN];
static uint8_t  inf_head;
static uint8_t  inf_count;
static atomic_t inf_active = ATOMIC_INIT(0);

static struct k_timer inf_period_timer;
static struct k_timer inf_timeout_timer;
static struct k_work  inf_sample_work;

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
  inf_samples[inf_head] = mA;
  inf_head = (inf_head + 1U) % INF_SAMPLE_BUF_LEN;
  if (inf_count < INF_SAMPLE_BUF_LEN) {
    inf_count++;
  }
}

static void inf_period_expiry(struct k_timer *t) {
  ARG_UNUSED(t);
  if (atomic_get(&inf_active)) {
    k_work_submit(&inf_sample_work);
  }
}

static void inf_timeout_expiry(struct k_timer *t) {
  ARG_UNUSED(t);
  inference_current_stop();
}

void inference_current_start(void) {
  /* Cancel any in-flight session and reset state. */
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

  inf_head  = 0;
  inf_count = 0;
  atomic_set(&inf_active, 1);
  k_timer_start(&inf_period_timer,
                K_USEC(INF_SAMPLE_PERIOD_US),
                K_USEC(INF_SAMPLE_PERIOD_US));
  k_timer_start(&inf_timeout_timer,
                K_USEC(INF_SAMPLE_TIMEOUT_US),
                K_NO_WAIT);
}

void inference_current_stop(void) {
  if (!atomic_cas(&inf_active, 1, 0)) {
    return;
  }
  k_timer_stop(&inf_period_timer);
  k_timer_stop(&inf_timeout_timer);
}

void inference_current_dump(void) {
  uint8_t n = inf_count;
  if (n == 0U) {
    printk("INF[0V8]: no samples\n");
    return;
  }

  float sum = 0.0f;
  for (uint8_t i = 0; i < n; i++) {
    sum += inf_samples[i];
  }
  float avg = sum / (float)n;

  int spike = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (inf_samples[i] > avg + INF_SPIKE_THRESH_MA) {
      spike = 1;
      break;
    }
  }

  int avg_sign = (avg < 0.0f) ? -1 : 1;
  float avg_abs = avg * avg_sign;
  int   avg_int = (int)avg_abs;
  int   avg_frac = (int)((avg_abs - avg_int) * 100.0f);
  printk("INF[0V8] N=%u avg=%s%d.%02d mA spike=%d\n",
         (unsigned)n,
         (avg_sign < 0 ? "-" : ""), avg_int, avg_frac,
         spike);

  printk("INF[0V8] samples:");
  for (uint8_t i = 0; i < n; i++) {
    float s = inf_samples[i];
    int   s_sign = (s < 0.0f) ? -1 : 1;
    float s_abs  = s * s_sign;
    int   s_int  = (int)s_abs;
    int   s_frac = (int)((s_abs - s_int) * 100.0f);
    printk(" %s%d.%02d", (s_sign < 0 ? "-" : ""), s_int, s_frac);
  }
  printk("\n");
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
