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

#define INF_SAMPLE_PERIOD_US  40
#define INF_SAMPLE_TIMEOUT_US 400
#define INF_SAMPLE_BUF_LEN    32
#define INF_SPIKE_THRESH_MA   50.0f

#ifdef __cplusplus
extern "C" {
#endif

/* Arm the per-inference 0V8 sampler. Re-arming cancels any in-flight session. */
void inference_current_start(void);

/* Stop the sampler. IRQ-safe and idempotent. */
void inference_current_stop(void);

/* Print every captured sample, the average, and a spike flag. */
void inference_current_dump(void);

#ifdef __cplusplus
}
#endif

#endif