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
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
LOG_MODULE_REGISTER(current_ic, LOG_LEVEL_INF);

/* SAADC channel order follows the devicetree io-channels list:
 * index 0 = AIN0 = VDD_1V8 (U25), index 1 = AIN1 = VDD_0V8_AKD (U26).
 * This matches current_rail_t, so a rail enum indexes adc_channels directly. */
#define ADC_CH_1V8 CURRENT_RAIL_1V8
#define ADC_CH_0V8 CURRENT_RAIL_0V8

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

/* Per-rail conversion (mA per output mV) and nominal rail voltage (V). */
static const float i_per_mv[CURRENT_RAIL_COUNT] = {I_PER_MV_1V8, I_PER_MV_0V8};
static const float rail_voltage[CURRENT_RAIL_COUNT] = {RAIL_VOLTAGE_1V8,
                                                       RAIL_VOLTAGE_0V8};

/* --- Shared sampler state (guarded by snap_lock) ------------------------- */
static K_MUTEX_DEFINE(snap_lock);
static current_sense_reading_t latest;      /* latest current/power snapshot */
static float energy_uj[CURRENT_RAIL_COUNT]; /* integrated since last reset */
static int64_t energy_start_ms;             /* energy reset time base */
static volatile uint16_t sample_rate_hz = CONFIG_CURRENT_DEFAULT_RATE_HZ;

#define CHRG_STS1_NODE DT_NODELABEL(chgr_sts1)
#define CHRG_STS2_NODE DT_NODELABEL(chgr_sts2)

static const struct gpio_dt_spec chgr_sts1 =
    GPIO_DT_SPEC_GET(CHRG_STS1_NODE, gpios);

static const struct gpio_dt_spec chgr_sts2 =
    GPIO_DT_SPEC_GET(CHRG_STS2_NODE, gpios);

/* Single ADC result buffer; only the sampler thread performs ADC reads. */
static uint16_t buf;
static struct adc_sequence sequences[ARRAY_SIZE(adc_channels)];

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
    LOG_ERR("Charging status1 GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&chgr_sts2)) {
    LOG_ERR("Charging status2 GPIO not ready");
    return -ENODEV;
  }
  /* --- Configure control pins as input --- */
  err = gpio_pin_configure_dt(&chgr_sts1, GPIO_INPUT);
  if (err) {
    LOG_ERR("Failed to configure Charging status1 pin (err %d)", err);
    return err;
  }
  err = gpio_pin_configure_dt(&chgr_sts2, GPIO_INPUT);
  if (err) {
    LOG_ERR("Failed to configure Charging status2 pin (err %d)", err);
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
      LOG_ERR("ADC controller device %s not ready", adc_channels[i].dev->name);
      return -ENODEV;
    }

    err = adc_channel_setup_dt(&adc_channels[i]);
    if (err < 0) {
      LOG_ERR("Could not setup channel #%d (%d)", i, err);
      return -ENODEV;
    }
    sequences[i] = (struct adc_sequence){
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };
    err = adc_sequence_init_dt(&adc_channels[i], &sequences[i]);
    if (err < 0) {
      LOG_ERR("ADC sequence init failed for channel %d: %d", i, err);
      return err;
    }
  }
  return 0;
}

/**
 * @brief Get the current battery charger status
 *
 * Reads two GPIO status pins from the battery charger IC and decodes
 * the combined state to determine if the battery is charging, not charging,
 * or in a fault condition (recoverable or non-recoverable fault).
 */
bat_status check_bat_status(void) {
  int sts1 = gpio_pin_get_dt(&chgr_sts1);
  if (sts1 < 0) {
    LOG_ERR("Failed to read CHGR_STS1: %d", sts1);
    return BAT_READ_FAILED;
  }

  int sts2 = gpio_pin_get_dt(&chgr_sts2);
  if (sts2 < 0) {
    LOG_ERR("Failed to read CHGR_STS2: %d", sts2);
    return BAT_READ_FAILED;
  }

  if (sts1 == 1 && sts2 == 1) {
    return BAT_NOT_CHARGING;
  } else if (sts1 == 1 && sts2 == 0) {
    return BAT_CHARGING;
  } else if (sts1 == 0 && sts2 == 1) {
    LOG_INF("Charger Status: Recoverable fault");
    return BAT_FAULT_RECOVERABLE;
  } else {
    LOG_INF("Charger Status: Non-recoverable fault");
    return BAT_FAULT_NON_RECOVERABLE;
  }
}

/**
 * @brief Sample one rail: average CURRENT_AVG_SAMPLES back-to-back reads.
 * @return 0 on success, -EIO if every read failed. Runs only in the sampler
 *         thread (sole owner of the shared ADC buffer).
 */
static int sample_rail_ma(current_rail_t rail, float *out_ma) {
  float sum = 0.0f;
  int valid = 0;

  for (int i = 0; i < CURRENT_AVG_SAMPLES; i++) {
    int err = adc_read_dt(&adc_channels[rail], &sequences[rail]);
    if (err < 0) {
      LOG_ERR("rail %d: ADC read error (%d)", rail, err);
      continue;
    }
    float val_mv = (float)(int32_t)buf * adc_scale;
    sum += val_mv * i_per_mv[rail];
    valid++;
  }

  if (valid == 0) {
    *out_ma = 0.0f;
    return -EIO;
  }
  *out_ma = sum / valid;
  return 0;
}

/* Format both rails and push a frame to the phone using the existing wire
 * format ("<1v8>,<0v8>" in mA, 2 decimals, explicit sign). */
static void stream_over_ble(float ma_1v8, float ma_0v8) {
  char data[DATA_BUFF_SIZE];

  int sign_1v8 = (ma_1v8 < 0) ? -1 : 1;
  int sign_0v8 = (ma_0v8 < 0) ? -1 : 1;
  float abs_1v8 = ma_1v8 * sign_1v8;
  float abs_0v8 = ma_0v8 * sign_0v8;
  int int_1v8 = (int)abs_1v8;
  int frac_1v8 = (int)((abs_1v8 - int_1v8) * 100);
  int int_0v8 = (int)abs_0v8;
  int frac_0v8 = (int)((abs_0v8 - int_0v8) * 100);

  snprintf(data, sizeof(data), "%s%d.%02d,%s%d.%02d",
           (sign_1v8 < 0 ? "-" : ""), int_1v8, frac_1v8,
           (sign_0v8 < 0 ? "-" : ""), int_0v8, frac_0v8);
  send_current_value(data);
}

/**
 * @brief Always-on current/power/energy sampler.
 *
 * Samples both rails at the configured rate, publishes the latest snapshot,
 * integrates energy (power * dt), and — while BLE current streaming is
 * enabled and connected — pushes readings to the phone. Sampling never stops,
 * so the read API always returns fresh values for UART/CLI use.
 */
void current_data_thread(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  current_sense_energy_reset();
  int64_t last = k_uptime_get();

  while (1) {
    float ma[CURRENT_RAIL_COUNT];
    float mw[CURRENT_RAIL_COUNT];

    for (int r = 0; r < CURRENT_RAIL_COUNT; r++) {
      sample_rail_ma((current_rail_t)r, &ma[r]);
      mw[r] = ma[r] * rail_voltage[r]; /* mA * V = mW */
    }

    int64_t now = k_uptime_get();
    uint32_t dt_ms = (uint32_t)(now - last);
    last = now;

    k_mutex_lock(&snap_lock, K_FOREVER);
    for (int r = 0; r < CURRENT_RAIL_COUNT; r++) {
      latest.current_ma[r] = ma[r];
      latest.power_mw[r] = mw[r];
      energy_uj[r] += mw[r] * (float)dt_ms; /* mW * ms = uJ */
    }
    k_mutex_unlock(&snap_lock);

    LOG_DBG("1V8 %.2f mA / %.2f mW | 0V8 %.2f mA / %.2f mW",
            (double)ma[CURRENT_RAIL_1V8], (double)mw[CURRENT_RAIL_1V8],
            (double)ma[CURRENT_RAIL_0V8], (double)mw[CURRENT_RAIL_0V8]);

    if (current_stream_flag == FLAG_ENABLE) {
      if (!is_ble_connected()) {
        LOG_INF("BLE disconnected, stopping current stream");
        current_stream_flag = FLAG_DISABLE;
      } else {
        stream_over_ble(ma[CURRENT_RAIL_1V8], ma[CURRENT_RAIL_0V8]);
      }
    }

    uint16_t rate = sample_rate_hz;
    k_msleep(1000U / rate);
  }
}

/* --- Read / energy / rate API -------------------------------------------- */

void current_sense_get_latest(current_sense_reading_t *out) {
  if (!out) {
    return;
  }
  k_mutex_lock(&snap_lock, K_FOREVER);
  *out = latest;
  k_mutex_unlock(&snap_lock);
}

int current_sense_read_ma(current_rail_t rail, float *current_ma) {
  if (rail >= CURRENT_RAIL_COUNT || !current_ma) {
    return -EINVAL;
  }
  k_mutex_lock(&snap_lock, K_FOREVER);
  *current_ma = latest.current_ma[rail];
  k_mutex_unlock(&snap_lock);
  return 0;
}

int current_sense_read_all(float *ma_1v8, float *ma_0v8) {
  k_mutex_lock(&snap_lock, K_FOREVER);
  if (ma_1v8) {
    *ma_1v8 = latest.current_ma[CURRENT_RAIL_1V8];
  }
  if (ma_0v8) {
    *ma_0v8 = latest.current_ma[CURRENT_RAIL_0V8];
  }
  k_mutex_unlock(&snap_lock);
  return 0;
}

int current_sense_read_power_mw(current_rail_t rail, float *power_mw) {
  if (rail >= CURRENT_RAIL_COUNT || !power_mw) {
    return -EINVAL;
  }
  k_mutex_lock(&snap_lock, K_FOREVER);
  *power_mw = latest.power_mw[rail];
  k_mutex_unlock(&snap_lock);
  return 0;
}

void current_sense_energy_reset(void) {
  k_mutex_lock(&snap_lock, K_FOREVER);
  for (int r = 0; r < CURRENT_RAIL_COUNT; r++) {
    energy_uj[r] = 0.0f;
  }
  energy_start_ms = k_uptime_get();
  k_mutex_unlock(&snap_lock);
}

void current_sense_energy_get(current_energy_t *out) {
  if (!out) {
    return;
  }
  k_mutex_lock(&snap_lock, K_FOREVER);
  uint32_t dur = (uint32_t)(k_uptime_get() - energy_start_ms);
  for (int r = 0; r < CURRENT_RAIL_COUNT; r++) {
    out->energy_uj[r] = energy_uj[r];
    out->avg_power_mw[r] = (dur > 0) ? (energy_uj[r] / (float)dur) : 0.0f;
  }
  out->duration_ms = dur;
  k_mutex_unlock(&snap_lock);
}

void current_sense_set_rate(uint16_t rate_hz) {
  if (rate_hz < CURRENT_RATE_MIN_HZ) {
    rate_hz = CURRENT_RATE_MIN_HZ;
  } else if (rate_hz > CURRENT_RATE_MAX_HZ) {
    rate_hz = CURRENT_RATE_MAX_HZ;
  }
  sample_rate_hz = rate_hz;
}

uint16_t current_sense_get_rate(void) { return sample_rate_hz; }

/* --- Shell: `power` ------------------------------------------------------- */

static int cmd_power_read(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  current_sense_reading_t r;
  current_sense_get_latest(&r);
  shell_print(sh, "1V8: %.2f mA  %.2f mW", (double)r.current_ma[CURRENT_RAIL_1V8],
              (double)r.power_mw[CURRENT_RAIL_1V8]);
  shell_print(sh, "0V8: %.2f mA  %.2f mW", (double)r.current_ma[CURRENT_RAIL_0V8],
              (double)r.power_mw[CURRENT_RAIL_0V8]);
  return 0;
}

static int cmd_power_rate(const struct shell *sh, size_t argc, char **argv) {
  if (argc > 1) {
    current_sense_set_rate((uint16_t)atoi(argv[1]));
  }
  shell_print(sh, "sample rate = %u Hz", current_sense_get_rate());
  return 0;
}

static int cmd_power_energy(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  current_energy_t e;
  current_sense_energy_get(&e);
  shell_print(sh, "over %u ms:", e.duration_ms);
  shell_print(sh, "  1V8: %.1f uJ  avg %.2f mW",
              (double)e.energy_uj[CURRENT_RAIL_1V8],
              (double)e.avg_power_mw[CURRENT_RAIL_1V8]);
  shell_print(sh, "  0V8: %.1f uJ  avg %.2f mW",
              (double)e.energy_uj[CURRENT_RAIL_0V8],
              (double)e.avg_power_mw[CURRENT_RAIL_0V8]);
  return 0;
}

static int cmd_power_reset(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  current_sense_energy_reset();
  shell_print(sh, "energy accumulator reset");
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    power_cmds,
    SHELL_CMD(read, NULL, "Print current & power for both rails", cmd_power_read),
    SHELL_CMD(rate, NULL, "Get/set sampler rate in Hz: rate [hz]",
              cmd_power_rate),
    SHELL_CMD(energy, NULL, "Print accumulated energy & average power",
              cmd_power_energy),
    SHELL_CMD(reset, NULL, "Reset the energy accumulator", cmd_power_reset),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(power, &power_cmds, "INA190 current/power/energy", NULL);
