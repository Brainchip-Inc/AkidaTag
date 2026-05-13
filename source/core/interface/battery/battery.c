#include "battery/battery.h"
#include "current_ic/current_ic.h"
#include "fuel_gauge/fuel_gauge.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BATTERY_POLL_INTERVAL_MS 1000
#define BATTERY_THREAD_STACK 1024
#define BATTERY_THREAD_PRIO 10

static K_THREAD_STACK_DEFINE(battery_stack, BATTERY_THREAD_STACK);
static struct k_thread battery_tid;
static K_SEM_DEFINE(battery_wake, 0, 1);
static K_MUTEX_DEFINE(status_lock);

static battery_status_t cached = {
    .soc_pct = -1,
    .charger_sts = BAT_STATUS_UNKNOWN,
};
static bool pending = false;
static bool fg_ready = false;

/**
 * @brief Fast init: charger status GPIOs and current IC ADC channels
 *
 * Runs synchronously from main() but does not block for more than a few
 * milliseconds. The slow fuel gauge init is deferred to the battery thread.
 */
int battery_init(void) {
  int err;

  err = bat_sts_gpio_init();
  if (err) {
    printk("[battery] charger GPIO init failed (%d)\n", err);
    return err;
  }

  err = current_ic_init();
  if (err) {
    printk("[battery] current IC init failed (%d)\n", err);
    return err;
  }

  return 0;
}
/**
 * @brief Wake up the battery monitoring thread
 *
 * Called from fuel gauge interrupt context (SOC change) to signal that
 * a new battery status should be read immediately instead of waiting
 * for the next poll interval.
 *
 * @note This function is ISR-safe and simply gives a semaphore.
 */
void battery_isr_notify(void) { k_sem_give(&battery_wake); }
/**
 * @brief Battery monitoring thread function
 *
 * Performs deferred initialization of the fuel gauge (I2C operations)
 * to avoid blocking main boot. After init, it waits for either:
 *   - A semaphore from fuel gauge interrupt (SOC change)
 *   - Poll interval timeout (1 second)
 *
 * On wake, it reads current charging status (GPIO) and SOC percentage
 * (if fuel gauge ready), then updates cached values if changed.
 *
 * @param a, b, c Unused thread arguments
 */
static void battery_thread_fn(void *a, void *b, void *c) {
  int err;

  /* Deferred slow init — up to several seconds of I2C polls on first boot.
   * Running here instead of main() keeps boot non-blocking. */
  err = fuel_gauge_init();
  if (err) {
    printk("[battery] fuel gauge init failed (%d); running charger-only\n",
           err);
  } else {
    err = fuel_gauge_isr_init();
    if (err) {
      printk("[battery] fuel gauge ISR init failed (%d)\n", err);
    } else {
      fg_ready = true;
    }
  }

  while (1) {
    /* Wake on ISR (SOC change) or after poll interval — whichever first. */
    k_sem_take(&battery_wake, K_MSEC(BATTERY_POLL_INTERVAL_MS));

    battery_status_t now;
    now.charger_sts = check_bat_status();
    now.soc_pct = fg_ready ? (int8_t)fuel_gauge_get_soc() : -1;

    k_mutex_lock(&status_lock, K_FOREVER);
    if (now.charger_sts != cached.charger_sts ||
        now.soc_pct != cached.soc_pct) {
      cached = now;
      pending = true;
    }
    k_mutex_unlock(&status_lock);
  }
}
/**
 * @brief Create and start the battery monitoring thread
 *
 * Spawns the battery thread with predefined stack size and priority.
 * Must be called once after system initialization.
 */
void battery_start(void) {
  k_thread_create(&battery_tid, battery_stack, BATTERY_THREAD_STACK,
                  battery_thread_fn, NULL, NULL, NULL, BATTERY_THREAD_PRIO, 0,
                  K_NO_WAIT);
  k_thread_name_set(&battery_tid, "battery");
}
/**
 * @brief Get cached battery status and change flag
 *
 * Thread-safe function to retrieve the latest battery SOC and charger
 * status. The `changed` flag indicates if any value has been updated
 * since the last call.
 *
 * @param out     Pointer to store battery status (SOC and charger state)
 * @param changed Pointer to bool that receives true if status changed
 */
void battery_get_status(battery_status_t *out, bool *changed) {
  k_mutex_lock(&status_lock, K_FOREVER);
  *out = cached;
  *changed = pending;
  pending = false;
  k_mutex_unlock(&status_lock);
}
/**
 * @brief Manually clear the pending change flag
 *
 * Used after sending battery status to the mobile app to reset the
 * change detection.
 */
void battery_clear_pending(void) {
  k_mutex_lock(&status_lock, K_FOREVER);
  pending = false;
  k_mutex_unlock(&status_lock);
}
