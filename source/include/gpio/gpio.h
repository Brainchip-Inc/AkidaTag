#ifndef GPIO_H
#define GPIO_H

#include <zephyr/kernel.h>
typedef enum { GPIO_DISABLE = 0, GPIO_ENABLE = 1 } device_state_t;

int gpio_init(void);
void spark_peripherals_power_enable(void);
void akd_irq_enable(void);
void akd_irq_disable(void);
#ifdef CONFIG_SPARK_BOARD
/* Refcounted AKD1500 wake. gpio.c owns the SLEEP pin and is its only writer:
 * a caller that needs the chip running takes a reference and hands it back when
 * it is done, and the pin follows the count.
 *
 * Counted rather than set/cleared because the holders are spread across threads
 * and none of them can tell whether the chip is still needed by someone else:
 * the audio thread wakes the chip to enqueue an inference while akd_async_thread
 * hands the reference back after the matching fetch, and a BLE model update
 * holds its own reference across a flash access that can overlap an inference.
 * A single shared boolean (however it is locked) cannot express "still needed",
 * so it lets one holder clock-gate the chip out from under another. */
void akd_wake_get(void);
void akd_wake_put(void);
/* Outstanding wake references; 0 means the chip is being allowed to sleep.
 * Diagnostic only - never gate a wake on this, take a reference instead. */
unsigned int akd_wake_count(void);
#else
static inline void akd_wake_get(void) {}
static inline void akd_wake_put(void) {}
/* The DK has no SLEEP pin, so the AKD1500 is permanently running. Report that
 * as one standing reference, so an invariant check of the form "a reference is
 * held for the whole access" reads the same on both boards. */
static inline unsigned int akd_wake_count(void) { return 1; }
#endif
int akd_async_sem_take(k_timeout_t timeout);
bool akd_in_learning(void);
void schedule_akd_learning_wq(void);
#endif /* GPIO_H */