#include "ble_services/battery_service.h"
#include "battery/battery.h"
#include "ble_services/ble_frame.h"
#include "ble_services/ble_initialization.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery_service, CONFIG_LOG_DEFAULT_LEVEL);

#define FRAME_SINGLE 0
#define FRAME_BUFFER_SIZE 128
#define DATA_PART_SIZE 96
/**
 * @brief Send battery status to mobile app via BLE
 *
 * Formats battery SOC and charger status into a data frame and sends
 * it over BLE. If CONFIG_SPARK_BOARD is defined, it uses real values
 * from fuel gauge; otherwise uses dummy values (90% SOC, not charging).
 *
 * The function checks `changed` flag: if no change since last send
 * and command is not forced (CMD_BATTERY), the send is skipped to
 * reduce BLE traffic.
 *
 * @param cmd Command type (CMD_BATTERY) to determine if forced send
 */
void battery_service_send(command_type_t cmd) {
  battery_status_t sts;
  bool changed;
  char data_part[DATA_PART_SIZE];
#ifdef CONFIG_SPARK_BOARD
  battery_get_status(&sts, &changed);
  if (cmd != CMD_BATTERY && !changed) {
    return;
  }
  snprintf(data_part, sizeof(data_part), "%d:%d,%d\r", CMD_BATTERY, sts.soc_pct,
           sts.charger_sts);
#else
  int battery_soc = 90;
  int charging_status = 0;
  snprintf(data_part, sizeof(data_part), "%d:%d,%d\r", CMD_BATTERY, battery_soc,
           charging_status);
#endif
  char frame[FRAME_BUFFER_SIZE];
  int data_len = strlen(data_part);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_SINGLE, 0, data_len,
           data_part);

  int err = ble_send_frame(frame);
  if (err) {
    LOG_ERR("battery send failed (%d)", err);
  } else {
    LOG_INF("battery sent [cmd=%d]: %s", cmd, data_part);
  }
}
/**
 * @brief Handle BLE disconnection by clearing pending flag
 *
 * Called when the BLE connection to the mobile app is lost.
 * Clears the pending change flag to prevent stale status from
 * being sent on next reconnection.
 */
void battery_service_on_disconnect(void) { battery_clear_pending(); }
