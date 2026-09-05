#ifndef __WATCHDOG_H
#define __WATCHDOG_H

#include <stdbool.h>
#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>

/**
 * @brief Enumeration of threads monitored by the Watchdog (WDT).
 *
 * Each enum value represents a thread whose health status is periodically
 * updated.
 */
typedef enum {
  DMIC_CAPTURE = 0, /**< DMIC audio capture thread */
  AUDIO_PROCESS,    /**< Audio processing thread */
#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
  IMU, /**< IMU data acquisition thread */
#endif
  WDT_THREAD_HEALTH_COUNT /**< Total number of monitored threads */
} wdt_thread_health;

/* Initial startup grace period for WDT (in milliseconds) */
#define WAIT_TIME 100

/* Total number of threads monitored by the Watchdog */
#define NUM_THREADS WDT_THREAD_HEALTH_COUNT

/* Thread health status array */
extern atomic_t thread_health[NUM_THREADS];
/** Runtime flag array indicating which threads are actively monitored by the
 * Watchdog. */
extern bool thread_enabled[NUM_THREADS];
/* Watchdog timeout in milliseconds. */
#define WDT_TIMEOUT_MS CONFIG_APP_WDT_TIMEOUT_MS

/**
 * @brief Initializes the watchdog device and installs a timeout channel.
 */
void watchdog_init(const struct device **wdt_dev, int *channel_id);
/**
 * @brief Checks whether all monitored threads have reported healthy status.
 */
bool all_threads_healthy(void);

/** Enables Watchdog monitoring for the specified thread ID. */
void wdt_enable_thread(wdt_thread_health id);
/** Disables Watchdog monitoring for the specified thread ID. */
void wdt_disable_thread(wdt_thread_health id);
#endif /* __WATCHDOG_H */