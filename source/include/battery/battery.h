#ifndef BATTERY_H
#define BATTERY_H

#include "current_ic/current_ic.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int8_t soc_pct; /* 0-100; -1 on read failure or fuel gauge not ready */
  bat_status charger_sts; /* charger GPIO-decoded status */
} battery_status_t;

/* Fast init: charger GPIOs + ADC channels. Fuel gauge init is deferred
 * into the battery thread (see battery_start) so main() is not blocked. */
int battery_init(void);

/* Spawns the battery polling thread. Thread does slow fuel gauge init
 * on first entry, then polls at BATTERY_POLL_INTERVAL_MS or on ISR wakeup. */
void battery_start(void);

/* Snapshot latest cached status. *changed is true if soc_pct or charger_sts
 * differs from the value observed at the previous call. Reading clears the
 * pending flag. */
void battery_get_status(battery_status_t *out, bool *changed);

/* Drop any pending "changed" state. Call from BLE disconnect handler so a
 * change that accumulated while disconnected does not fire a spurious
 * notification on reconnect. */
void battery_clear_pending(void);

/* Wake the battery thread. Called from the fuel gauge GPOUT/SOC_INT ISR. */
void battery_isr_notify(void);

#endif
