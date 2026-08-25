#ifndef GPIO_H
#define GPIO_H

#include <zephyr/kernel.h>
typedef enum { GPIO_DISABLE = 0, GPIO_ENABLE = 1 } device_state_t;

int gpio_init(void);
void spark_peripherals_power_enable(void);
void akd_irq_enable(void);
void akd_irq_disable(void);
#ifdef CONFIG_SPARK_BOARD
void akd_sleep(bool sleep);
#else
static inline void akd_sleep(bool sleep) { (void)sleep; }
#endif
int akd_async_sem_take(k_timeout_t timeout);
bool akd_in_learning(void);
void schedule_akd_learning_wq(void);
#endif /* GPIO_H */