#ifndef BLE_INITIALIZATION_H
#define BLE_INITIALIZATION_H
#include "ble_services/edge_learning.h"
#include "ble_services/file_transfer.h"
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
/**
 * @brief 128-bit unsigned integer using two 64-bit values.
 *
 * The 128-bit value is split into:
 * - high: most significant 64 bits
 * - low : least significant 64 bits
 *
 * Combined value = (high << 64) | low
 */
typedef struct {
  uint64_t high;
  uint64_t low;
} device_id_128_t;

/**
 * Enumeration of supported BLE commands.
 * Used to identify and handle commands received from the mobile application.
 */
typedef enum {
  CMD_BATTERY = 0,
  CMD_DEVICE_INFO = 1,
  CMD_APPS = 2,
  CMD_NOTIFY = 3,
  CMD_CONFIG = 4,
  CMD_APP_INFO = 5,
  CMD_UNINSTALL = 6,
  CMD_RESET = 7,
  CMD_DEPLOY_START = 8,
  CMD_STREAM_START = 9,
  CMD_DEPLOY_STOP = 10,
  CMD_STREAM_STOP = 11,
  CMD_ERROR = 12,
} command_type_t;

/* Structure representing a parsed command frame received from the host */
typedef struct {
  uint8_t frame_type;
  uint8_t index;
  uint8_t size;
  uint8_t command;
} parsed_frame_t;

/* Flag indicating whether deployment mode is active.
 * Set when CMD_DEPLOY_START is received and cleared on CMD_DEPLOY_STOP.
 */
extern uint8_t event_flag;

/* Flag indicating whether PDM audio streaming is enabled.
 * Used to control real-time audio data transmission to the phone.
 */
extern uint8_t pdm_stream_flag;

/* Access in main.c, and variable changes based on info.yaml */
extern model_meta_t kws_meta;
extern model_data_meta_t kws_data_meta;
extern uint32_t g_num_classes;
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
void send_event(int cmd, const char *label, float value);

/**
 * @brief Acknowledgement code sent when the Akida engine encounters an error.
 */
#define ACK_AKIDA_ERROR 0xAE

/**
 * @brief Send an acknowledgment response to the mobile app.
 *
 * @param ack_code Acknowledgment code
 * @param cmd The command type being acknowledged
 */
void send_ack(uint8_t ack_code, command_type_t cmd);

#endif /* BLE_INITIALIZATION_H */