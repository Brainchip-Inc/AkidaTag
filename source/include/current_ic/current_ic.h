#ifndef CURRENT_IC_H
#define CURRENT_IC_H

#define ADC_CH_0 0
#define ADC_CH_1 1
#define AVG_SAMPLES 20
#define ADC_MAX_VALUE 4095.0f
#define ADC_REF_MV_1v8 1800.0f
#define SHUNT_RESISTOR_GAIN_1V8 (25.0f * 1.0f) /*(GAIN * Resistor)*/
#define SHUNT_RESISTOR_GAIN_0V8 (25.0f * 0.2f) /*(GAIN * Resistor)*/

typedef enum {
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

/* Reads and averages current measurements from monitoring IC */
int read_current_ic(void);

/* Gets current battery charger status from GPIO pins */
bat_status check_bat_status(void);

#endif