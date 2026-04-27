#ifndef BLE_FRAME_H
#define BLE_FRAME_H

/**
 * @brief Send a pre-formatted NUS frame to the connected phone
 *
 * Handles flow-control waits and retries internally. Thread-safe with respect
 * to the internal send_in_progress flag.
 *
 * @param frame Null-terminated string to transmit
 * @return 0 on success, -ENOTCONN if no active connection, -EBUSY on timeout,
 *         or a negative errno propagated from bt_nus_send
 */
int ble_send_frame(const char *frame);

#endif
