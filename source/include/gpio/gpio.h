#ifndef GPIO_H
#define GPIO_H
#include <zephyr/kernel.h>

typedef enum {
  BAT_READ_FAILED = -1,
  BAT_NOT_CHARGING = 0,
  BAT_CHARGING = 1,
  BAT_FAULT_RECOVERABLE = 2,
  BAT_FAULT_NON_RECOVERABLE = 3,
} bat_status;

typedef enum { GPIO_DISABLE = 0, GPIO_ENABLE = 1 } device_state_t;

int gpio_init(void);
/* Initializes GPIO pins for battery charger status monitoring */
int bat_sts_gpio_init(void);
void spark_peripherals_power_enable(void);
void akd_irq_enable(void);
void akd_irq_disable(void);
int akd_async_sem_take(k_timeout_t timeout);
bool akd_in_learning(void);
void schedule_akd_learning_wq(void);
/* Gets current battery charger status from GPIO pins */
bat_status check_bat_status(void);
#endif /* GPIO_H */