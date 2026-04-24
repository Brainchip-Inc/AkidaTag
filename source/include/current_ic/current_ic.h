#ifndef CURRENT_IC_H
#define CURRENT_IC_H

#define CURRENT_STACK_SIZE 1024
#define CURRENT_PRIORITY 5
#define AVG_SAMPLES 20
#define ADC_MAX_VALUE 4095.0f
#define ADC_REF_MV_1v8 1800.0f
#define SHUNT_RESISTOR_GAIN_1V8 (25.0f * 1.0f) /*(GAIN * Resistor)*/
#define SHUNT_RESISTOR_GAIN_0V8 (25.0f * 0.2f) /*(GAIN * Resistor)*/

/* Initializes ADC channels for current monitoring IC */
int current_ic_init(void);

/* thread to reads and averages current measurements from monitoring IC */
void current_data_thread(void *a, void *b, void *c);

#endif