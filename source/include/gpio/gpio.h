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
/* Last state requested through akd_sleep(). Lets a caller that must wake the
 * chip for its own access restore the state its caller had, instead of forcing
 * the chip awake or asleep behind the KWS per-inference duty cycle. */
bool akd_sleep_get(void);
#else
static inline void akd_sleep(bool sleep) { (void)sleep; }
static inline bool akd_sleep_get(void) { return false; }
#endif
int akd_async_sem_take(k_timeout_t timeout);
bool akd_in_learning(void);
void schedule_akd_learning_wq(void);
#endif /* GPIO_H */