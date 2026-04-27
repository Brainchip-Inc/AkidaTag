#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

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

#define ADC_CH_0 0
#define ADC_CH_1 1

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) ||                                   \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

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
static struct adc_sequence sequence;

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
  float sum_1v8 = 0.0f;
  float sum_0v8 = 0.0f;

  // Channel 0 — 1V8 rail
  // Initialize sequence ONCE for this channel
  adc_sequence_init_dt(&adc_channels[ADC_CH_0], &sequence);
  sequence.buffer = &buf;
  sequence.buffer_size = sizeof(buf);

  for (int cnt = 0; cnt < AVG_SAMPLES; cnt++) {
    err = adc_read_dt(&adc_channels[ADC_CH_0], &sequence);
    if (err < 0) {
      printk("CH0: read error (%d)\n", err);
      return -1;
    }
    float val_mv = (float)(int32_t)buf * adc_scale;
    sum_1v8 += val_mv / SHUNT_RESISTOR_GAIN_1V8;
    k_msleep(10);
  }

  // Settling delay when switching channels
  k_msleep(20);

  // Channel 1 — 0V8 rail
  // Re-initialize sequence for the new channel
  adc_sequence_init_dt(&adc_channels[ADC_CH_1], &sequence);
  sequence.buffer = &buf;
  sequence.buffer_size = sizeof(buf);

  for (int cnt = 0; cnt < AVG_SAMPLES; cnt++) {
    err = adc_read_dt(&adc_channels[ADC_CH_1], &sequence);
    if (err < 0) {
      printk("CH1: read error (%d)\n", err);
      return -1;
    }
    float val_mv = (float)(int32_t)buf * adc_scale;
    sum_0v8 += val_mv / SHUNT_RESISTOR_GAIN_0V8;
    k_msleep(10);
  }

  float avg_1v8 = sum_1v8 / AVG_SAMPLES;
  float avg_0v8 = sum_0v8 / AVG_SAMPLES;
  printk("CH0 1V8 Avg Current: %.2f mA\n", (double)avg_1v8);
  printk("CH1 0V8 Avg Current: %.2f mA\n", (double)avg_0v8);
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
  if (read_current_ic() < 0) {
    return -1;
  }
  return 0;
}

SHELL_CMD_REGISTER(read_current_ic, NULL, "Read Current IC",
                   cli_read_current_ic);
