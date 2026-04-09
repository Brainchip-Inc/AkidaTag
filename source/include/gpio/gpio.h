#ifndef GPIO_H
#define GPIO_H

typedef enum { GPIO_DISABLE = 0, GPIO_ENABLE = 1 } device_state_t;

int gpio_init(void);
void spark_peripherals_power_enable(void);

#endif /* GPIO_H */