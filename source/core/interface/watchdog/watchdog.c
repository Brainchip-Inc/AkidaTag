#include "watchdog_h/watchdog.h"
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_DBG);

/* Thread health status array */
atomic_t thread_health[NUM_THREADS];
/** Runtime flag array indicating which threads are actively monitored by the
 * Watchdog. */
bool thread_enabled[NUM_THREADS];
/**
 * @brief Initialize and start the watchdog timer.
 *
 * This function configures and enables the SoC watchdog using the Zephyr
 * watchdog driver. It installs a watchdog timeout that triggers a full
 * system reset if the watchdog is not fed within the configured timeout
 * window.
 *
 * The watchdog device handle and installed channel ID are returned to the
 * caller via output parameters, allowing the application to feed or manage
 * the watchdog later.
 *
 * @param wdt_dev     Pointer to store the watchdog device handle.
 * @param channel_id  Pointer to store the installed watchdog channel ID.
 *
 * @note The watchdog is configured to reset the SoC on timeout.
 * @note The watchdog is paused automatically when the debugger halts
 *       execution.
 */
void watchdog_init(const struct device **wdt_dev, int *channel_id) {
  /*wait time to start all the threads*/
  k_sleep(K_MSEC(WAIT_TIME));
  struct wdt_timeout_cfg wdt_config = {
      .window.min = 0,
      .window.max = WDT_TIMEOUT_MS,
      .flags = WDT_FLAG_RESET_SOC,
      .callback = NULL,
  };

  *wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog));

  if (!device_is_ready(*wdt_dev)) {
    LOG_ERR("WDT not ready");
    return;
  }

  *channel_id = wdt_install_timeout(*wdt_dev, &wdt_config);
  if (*channel_id < 0) {
    LOG_ERR("WDT install failed");
    return;
  }

  // Pause WDT while CPU sleeps and during debug halt
  if (wdt_setup(*wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG) < 0) {
    LOG_ERR("WDT setup failed");
    return;
  }
  LOG_INF("Watchdog started (%d ms timeout)", WDT_TIMEOUT_MS);
}

/**
 * @brief Verify health status of all enabled worker threads.
 *
 * This function checks only the threads that are currently enabled
 * in the thread_enabled[] array.
 *
 * For each enabled thread, it verifies that the corresponding
 * thread_health flag is set (non-zero). If any enabled thread has
 * not reported health during the current supervision cycle, the
 * function immediately returns false.
 *
 * If no threads are enabled, the function also returns true.
 *
 * When all enabled threads are healthy, their health flags are
 * reset to 0 for the next supervision cycle.
 *
 * @return true  All enabled threads reported healthy.
 * @return false At least one enabled thread is unhealthy,
 *               or no threads are enabled.
 */

bool all_threads_healthy(void) {

  bool at_least_one = false;

  for (int i = 0; i < NUM_THREADS; i++) {

    if (!thread_enabled[i])
      continue; // SKIP inactive threads

    at_least_one = true;

    if (atomic_get(&thread_health[i]) == 0) {
      LOG_WRN("Thread %d NOT healthy", i);
      return false;
    }
  }

  if (!at_least_one) {
    return true; // no active threads
  }

  // Reset health flags
  for (int i = 0; i < NUM_THREADS; i++) {
    if (thread_enabled[i])
      atomic_set(&thread_health[i], 0);
  }

  return true;
}

/**
 * @brief Force a system freeze to trigger Watchdog reset.
 *
 * This CLI command intentionally disables all interrupts and
 * enters an infinite loop. Since the scheduler and all other
 * threads are stopped, the watchdog is no longer fed and will
 * expire after the configured timeout, causing a system reset.
 *
 * This command is intended only for watchdog testing purposes.
 *
 * Usage:
 *   wdt_disable
 *
 * @return This function does not return under normal execution.
 */
static int cmd_wdt_disable(const struct shell *shell, size_t argc,
                           char **argv) {
  int timeout_sec = WDT_TIMEOUT_MS / 1000;
  shell_print(shell, "Watchdog feeding DISABLED - reset in ~%d s", timeout_sec);

  (void)irq_lock();
  while (1) {
    /* Spin with interrupts disabled; WDT hardware fires after timeout */
  }

  CODE_UNREACHABLE;
  return 0;
}

/**
 * @brief Enable Watchdog monitoring for a specific thread.
 *
 * This function marks the given thread ID as active for Watchdog supervision.
 * Once enabled, the thread is expected to periodically update its health
 * status. If the thread fails to report its health within the configured
 * timeout window, the Watchdog will trigger a system reset.
 *
 * This function should be called after the thread is fully initialized
 * and ready to run.
 *
 * @param id Fixed Watchdog thread identifier (wdt_thread_health).
 */
void wdt_enable_thread(wdt_thread_health id) {
  thread_enabled[id] = true;
  atomic_set(&thread_health[id], 1);
}

/**
 * @brief Disable Watchdog monitoring for a specific thread.
 *
 * This function removes the given thread from Watchdog supervision.
 * The Watchdog will skip this thread during health checks.
 *
 * This is useful when a feature or thread is conditionally
 * disabled at runtime.
 *
 * @param id Fixed Watchdog thread identifier (wdt_thread_health).
 */
void wdt_disable_thread(wdt_thread_health id) { thread_enabled[id] = false; }

SHELL_CMD_REGISTER(wdt_disable, NULL, "Disable WDT feeding", cmd_wdt_disable);
