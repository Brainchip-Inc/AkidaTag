#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "ble_services/ble_initialization.h"
#include "current_ic/current_ic.h"
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) ||                                   \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx)                                  \
  ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)};

static uint8_t previous_bat_status = 0xFF; // Initialize to invalid value

static const float adc_scale = ADC_REF_MV_1v8 / (float)ADC_MAX_VALUE;

#define CHRG_STS1_NODE DT_NODELABEL(chgr_sts1)
#define CHRG_STS2_NODE DT_NODELABEL(chgr_sts2)

static const struct gpio_dt_spec chgr_sts1 =
    GPIO_DT_SPEC_GET(CHRG_STS1_NODE, gpios);

static const struct gpio_dt_spec chgr_sts2 =
    GPIO_DT_SPEC_GET(CHRG_STS2_NODE, gpios);

static uint16_t buf;
static struct adc_sequence sequence = {
    .buffer = &buf,
    /* buffer size in bytes, not number of samples */
    .buffer_size = sizeof(buf),
};

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
  }
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
uint8_t check_bat_status(void) {
  int sts1 = gpio_pin_get_dt(&chgr_sts1);
  int sts2 = gpio_pin_get_dt(&chgr_sts2);
  uint8_t current_status;

  if (sts1 == 1 && sts2 == 1) {
    current_status = BAT_NOT_CHARGING;
  } else if (sts1 == 1 && sts2 == 0) {
    current_status = BAT_CHARGING;
  } else if (sts1 == 0 && sts2 == 1) {
    printk("Charger Status: Recoverable fault\n");
    current_status = BAT_FAULT_RECOVERABLE;
  } else {
    printk("Charger Status: Non-recoverable fault\n");
    current_status = BAT_FAULT_NON_RECOVERABLE;
  }
  // Check if status changed
  if (current_status != previous_bat_status) {
    previous_bat_status = current_status;
    if (is_ble_connected()) {
      // Send BLE notification on status change
      send_battery_response();
      printk("Battery status changed to: %d, BLE notification sent\n",
             current_status);
    }
  }

  return current_status;
}
/**
 * @brief Read and average current measurements from the current monitoring IC
 *
 * Samples all ADC channels, converts raw ADC values to millivolts,
 * then to milliamps using shunt resistor gain. Accumulates samples
 * per channel until AVG_SAMPLES is reached, then prints the average
 * current for each channel. When all channels have reported their
 * averages, prints the total combined average current.
 *
 * @return int Always returns 0 (read errors are logged but don't stop
 * execution)
 */
int read_current_ic(void) {
  int err;
  int32_t raw_val;
  float val_mv;
  float current_ma;
  /* Reset per-channel accumulators */
  float sum_1v8 = 0.0f;
  float sum_0v8 = 0.0f;
  for (int cnt = 0; cnt < AVG_SAMPLES; cnt++) {
    for (size_t ch = 0U; ch < NUM_CHANNELS; ch++) {

      adc_sequence_init_dt(&adc_channels[ch], &sequence);

      err = adc_read_dt(&adc_channels[ch], &sequence);
      if (err < 0) {
        printk("CH%zu: read error (%d)\n", ch, err);
        continue;
      }

      /* RAW value */
      raw_val = (int32_t)buf;

      // /* RAW → mV */
      val_mv = (float)raw_val * adc_scale;
      /* mV → mA */
      if (ch == ADC_CH_0) {
        current_ma = val_mv / SHUNT_RESISTOR_GAIN_1V8;
        sum_1v8 += current_ma;
      } else {
        current_ma = val_mv / SHUNT_RESISTOR_GAIN_0V8;
        sum_0v8 += current_ma;
      }
    }
    k_msleep(50);
  }
  float avg_1v8 = sum_1v8 / AVG_SAMPLES;
  float avg_0v8 = sum_0v8 / AVG_SAMPLES;
  printk("CH0 1V8 Avg Current: %.3f mA\n", avg_1v8);
  printk("CH1 0V8 Avg Current: %.3f mA\n", avg_0v8);
  return 0;
}
/**
 * @brief Read current measurements from the current monitoring IC
 *
 * Samples both ADC channels (1V8 and 0V8 rails) multiple times and
 * calculates the average current for each rail.
 *
 * Usage:
 *   uart:~$ read_current_ic
 *
 * The function performs AVG_SAMPLES (default 20) iterations, reading
 * both ADC channels each iteration with a 50ms delay between iterations.
 * After collecting all samples, it calculates and displays the average
 * current for each rail:
 *
 *   CH0 1V8 Avg Current: XXX.XXX mA
 *   CH1 0V8 Avg Current: XXX.XXX mA
 *
 * @param shell Shell structure pointer
 * @param argc Argument count (should be 1 for no arguments)
 * @param argv Argument vector
 * @return 0 on success, -EINVAL if invalid arguments provided
 */
static int cli_read_current_ic(const struct shell *shell, size_t argc,
                               char **argv) {
  if (argc > 1) {
    printk("invalid command\n");
    return -EINVAL;
  }
  read_current_ic();
}

SHELL_CMD_REGISTER(read_current_ic, NULL, "Read Current IC",
                   cli_read_current_ic);
