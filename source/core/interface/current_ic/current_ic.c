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

static uint16_t buf;
static struct adc_sequence sequences[ARRAY_SIZE(adc_channels)];

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
  return 0;
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
      for (int cnt = 0; cnt < AVG_SAMPLES; cnt++) {
        // Channel 0 — 1V8 rail
        err = adc_read_dt(&adc_channels[ADC_CH_0], &sequences[ADC_CH_0]);
        if (err < 0) {
          printk("CH0: read error (%d)\n", err);
          continue;
        }
        float val_mv = (float)(int32_t)buf * adc_scale;
        sum_1v8 += val_mv / SHUNT_RESISTOR_GAIN_1V8;
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
        k_msleep(10);
      }

      float avg_1v8 = sum_1v8 / AVG_SAMPLES;
      float avg_0v8 = sum_0v8 / AVG_SAMPLES;
      printk("CH0 1V8 Avg Current: %.2f mA\n", (double)avg_1v8);
      printk("CH1 0V8 Avg Current: %.2f mA\n", (double)avg_0v8);
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
