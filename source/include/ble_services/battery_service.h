#ifndef BATTERY_SERVICE_H
#define BATTERY_SERVICE_H

#include "ble_services/ble_initialization.h"

/**
 * @brief Send battery status as a NUS frame
 *
 * @param cmd CMD_BATTERY for on-demand phone request (always sends);
 *            CMD_STREAM_STS for streaming (only sends if status changed).
 */
void battery_service_send(command_type_t cmd);

/**
 * @brief BLE disconnect hook for the battery service
 *
 * Clears any pending "changed" flag in the battery module so a status
 * change that accumulated while disconnected does not fire a spurious
 * notification on the next reconnect.
 */
void battery_service_on_disconnect(void);

#endif
