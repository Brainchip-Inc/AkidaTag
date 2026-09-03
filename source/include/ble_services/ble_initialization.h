#ifndef BLE_INITIALIZATION_H
#define BLE_INITIALIZATION_H
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
#include "ble_services/edge_learning.h"
#include "ble_services/file_transfer.h"

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
    CMD_STREAM_STS = -1,
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
    CMD_STREAM_WAVE = 12,
    CMD_CURRENT_START = 13,
    CMD_CURRENT_STOP = 14
} command_type_t;

/* Structure representing a parsed command frame received from the host.
 *
 * `payload` points into the caller's receive buffer at the first byte after
 * `<cmd>:` (i.e. the start of the command-specific data), or is NULL if the
 * frame has no ':' separator. Only valid until the receive buffer is reused.
 */
typedef struct {
    uint8_t frame_type;
    uint8_t index;
    uint8_t size;
    uint8_t command;
    const char* payload;
} parsed_frame_t;

/* Flag indicating whether deployment mode is active.
 * Set when CMD_DEPLOY_START is received and cleared on CMD_DEPLOY_STOP.
 */
extern uint8_t event_flag;

/* Flag indicating whether PDM audio streaming is enabled.
 * Used to control real-time audio data transmission to the phone.
 */
extern uint8_t pdm_stream_flag;

/* Flag set when phone enters main app page -
 * controls app-specific features and streaming battery status
 */
extern uint8_t app_start_flag;

/* Flag indicating whether battery current streaming is active or not.
 * When set, current values are sent to the phone in real-time.
 */
extern uint8_t current_stream_flag;

/* Access in main.c, and variable changes based on info.yaml */
extern model_meta_t kws_meta;
extern model_data_meta_t kws_data_meta;
extern uint32_t g_num_classes;
extern uint8_t adv_manufacturer_data[];

int ble_init(void);
/*
 * Send a binary PCM waveform frame to the phone over NUS.
 *
 * Wire format (little-endian throughout):
 *   byte 0    : magic 'B' (0x42)
 *   byte 1    : cmd CMD_STREAM_WAVE (0x0C)
 *   bytes 2-3 : uint16 seq (monotonic, wraps)
 *   bytes 4-5 : uint16 n_samples (64 = envelope, 32 = decimation fallback)
 *   bytes 6.. : n_samples * int16 LE
 *
 * Envelope mode (n_samples=64): 32 interleaved (min, max) pairs covering the
 *   60 ms block - 134 bytes total.
 * Decimation fallback (n_samples=32): every-30th-sample decimation of the same
 *   block - 70 bytes total. Firmware picks the mode via wave_fallback_active().
 */
void send_pcm_wave(const int16_t* samples, uint16_t n_samples);

/*
 * Returns true when the BLE link cannot sustain the full envelope payload
 * (small MTU or high retry rate) and the firmware should emit the smaller
 * decimation-fallback frame instead.
 */
bool wave_fallback_active(void);
#ifdef CONFIG_SPARK_BOARD
/**
 * @brief Send current value to phone for real-time monitoring
 * @param data Pointer to string containing current reading
 */
void send_current_value(char*);
#endif

typedef enum { FLAG_DISABLE = 0, FLAG_ENABLE = 1 } flag_state_t;
/*
 * Send a keyword spotting (KWS) detection event to the phone.
 * Includes the detected keyword and its confidence score.
 */
void send_event(int cmd, const char* label, float value);
#endif /* BLE_INITIALIZATION_H */