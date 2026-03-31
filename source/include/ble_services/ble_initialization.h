#ifndef BLE_INITIALIZATION_H
#define BLE_INITIALIZATION_H
#include "ble_services/edge_learning.h"
#include <bluetooth/services/lbs.h>
#include <bluetooth/services/nus.h>
#include <dk_buttons_and_leds.h>
#include <hal/nrf_ficr.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
/* Flag indicating whether deployment mode is active.
 * Set when CMD_DEPLOY_START is received and cleared on CMD_DEPLOY_STOP.
 */
extern uint8_t event_flag;

/* Flag indicating whether PDM audio streaming is enabled.
 * Used to control real-time audio data transmission to the phone.
 */
extern uint8_t pdm_stream_flag;

/* Access in main.c, and variable changes based on info.yaml */
extern char model_name[64]; 
extern uint32_t no_of_class;
extern char input_shape[3] ;
extern uint8_t model_size;

int ble_init(void);
void prcess_led(void);

/*
 * Send PDM audio data to the phone.
 * Used for real-time audio level visualization over BLE.
 */
void send_pdm_data(uint32_t data);

typedef enum { FLAG_DISABLE = 0, FLAG_ENABLE = 1 } flag_state_t;
/*
 * Send a keyword spotting (KWS) detection event to the phone.
 * Includes the detected keyword and its confidence score.
 */
void send_kws_event(const char *word, float strength);
#endif /* BLE_INITIALIZATION_H */