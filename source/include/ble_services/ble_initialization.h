#ifndef BLE_INITIALIZATION_H
#define BLE_INITIALIZATION_H
#include "ble_services/edge_learning.h"
#include <bluetooth/services/lbs.h>
#include <bluetooth/services/nus.h>
#include <dk_buttons_and_leds.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>

/* Flag indicating whether deployment mode is active.
 * Set when CMD_DEPLOY_START is received and cleared on CMD_DEPLOY_STOP.
 */
extern uint8_t event_flag;

/* Flag indicating whether PDM audio streaming is enabled.
 * Used to control real-time audio data transmission to the phone.
 */
extern uint8_t pdm_stream_flag;

int ble_init(void);
void prcess_led(void);

/*
 * Send PDM audio data to the phone.
 * Used for real-time audio level visualization over BLE.
 */
void send_pdm_data(uint32_t data);
/*
 * Send a keyword spotting (KWS) detection event to the phone.
 * Includes the detected keyword and its confidence score.
 */
void send_kws_event(const char *word, uint8_t strength);
#endif /* BLE_INITIALIZATION_H */