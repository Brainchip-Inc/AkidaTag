
#include "ble_services/ble_initialization.h"
#include "ble_services/battery_service.h"
#include "ble_services/ble_frame.h"

#include "kws_app.h"
#include "kws_config.h"
#include "led_init.h"

#include <stdlib.h>

#include <hal/nrf_ficr.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(ble_initialization, CONFIG_LOG_DEFAULT_LEVEL);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define CON_STATUS_LED DK_LED2
#define USER_LED DK_LED3
#define USER_BUTTON DK_BTN1_MSK
#define ACK_DONE 0xAA

/* ==================== FRAME PROTOCOL DEFINITIONS ====================
 * Frame types for single and multi-frame messages:
 *   FRAME_SINGLE   - Complete message fits in one frame
 *   FRAME_MF_START - First frame of multi-frame message
 *   FRAME_MF_MID   - Middle frame(s) of multi-frame message
 *   FRAME_MF_LAST  - Last frame of multi-frame message
 *
 * Frame format: "<frame_type>,<index>,<size>,<data>"
 *   - frame_type: 0-3 as defined above
 *   - index:      Sequence number (0-based)
 *   - size:       Length of data part
 *   - data:       Actual payload in format "<cmd>:<value>,\r"
 */

#define FRAME_SINGLE 0   // Single frame (complete message)
#define FRAME_MF_START 1 // Multi-frame start
#define FRAME_MF_MID 2   // Multi-frame middle
#define FRAME_MF_LAST 3  // Multi-frame last
#define FRAME_BUFFER_SIZE 128
#define MTU_FRAME_BUFFER_SIZE 244
#define DATA_PART_SIZE 96
#define SEND_FRAME_TIMEOUT_MS 100
#define MAX_NUS_RX_BUFFER_SIZE 245
#define NUM_STATES 3
#ifdef CONFIG_DK_BOARD
static bool app_button_state;

#endif

static struct bt_conn *current_conn = NULL;
static bool notifications_enabled = false;
static bool send_in_progress = false;

/* ==================== WAVEFORM STREAM STATE ====================
 * Tracks link quality for the binary PCM-envelope stream. If the peer
 * negotiates a small MTU or the NUS retry rate climbs, the firmware
 * auto-downshifts from 134-byte envelope frames to 70-byte decimation
 * frames. See send_pcm_wave() and wave_fallback_active(). */
#define WAVE_MAGIC 0x42
#define WAVE_HEADER_LEN 6
#define WAVE_ENV_N_SAMPLES 64 /* 32 min/max pairs */
#define WAVE_DEC_N_SAMPLES 32
#define WAVE_ENV_FRAME_LEN (WAVE_HEADER_LEN + WAVE_ENV_N_SAMPLES * 2)
#define WAVE_DEC_FRAME_LEN (WAVE_HEADER_LEN + WAVE_DEC_N_SAMPLES * 2)
#define WAVE_FALLBACK_RETRY_THRESHOLD 5
#define WAVE_FALLBACK_WINDOW 50
#define WAVE_RECOVER_CLEAN_FRAMES 100
#define WAVE_MTU_MIN_FOR_ENVELOPE 140

static bool wave_fallback = false;
static uint16_t wave_negotiated_mtu = 23; /* default ATT MTU until exchange */
static uint32_t nus_retry_counter = 0;

/* ==================== DEVICE INFORMATION ====================
 * FOR TESTING PURPOSES ONLY - Static data for initial development
 *
 * These values are currently hardcoded for testing and validation.
 * In future production versions,we have to change.
 */
static const char device_manufacturer[] = "AKIDA";
static const char device_type[] = "TYPE";
static const char device_version[] = "5.3";
static const char device_firmware[] = "1.2.3";

static const char app_name[] = "Keyword Spotting";
char description[244] =
    "Voice-activated wake word detection using microphone input";
uint16_t app_size = 128;

char processor[] = "AKIDA_1500";
char model_version[] = "v1.1.0";
float pwr_con = 2.3f;
/* Defined in kws_inputs.cpp. First 10 entries (0..9) are the trained keywords
 * advertised to the phone app; entries 10..11 are "silence"/"unknown" and
 * 12..14 are edge-learning slots. */
extern const char *const kws_new_tags[];

/* Flag indicating whether deployment mode is active.
 * Set when CMD_DEPLOY_START is received and cleared on CMD_DEPLOY_STOP.
 */
uint8_t event_flag = FLAG_DISABLE;
/* Flag indicating whether PDM audio streaming is enabled.
 * Used to control real-time audio data transmission to the phone.
 */
uint8_t pdm_stream_flag = FLAG_DISABLE;

/* Flag set when phone enters main app page.
 * Enables app-specific features that should only run when user is actively
 * using the main application interface.
 */
uint8_t app_start_flag = FLAG_DISABLE;

/* Flag indicating whether battery current streaming is active or not.
 * When set, current values are sent to the phone in real-time.
 */
uint8_t current_stream_flag = FLAG_DISABLE;
#ifdef CONFIG_SPARK_BOARD
K_SEM_DEFINE(current_stream_sem, 0, 1);
#endif
/*==================== ADVERTISING DATA ====================
 * Manufacturer data is encoded in ASCII (hex values of characters)
 * instead of raw numeric values. This allows the mobile phone BLE application
 * to directly read and display in text form, without needing to convert from
 * binary to human-readable format
 * Format:
 *   - Bytes 0-1: BLE Protocol Version (e.g., "53" for 5.3)
 *   - Bytes 2-4: Firmware Version (e.g., "241" for v2.4.1)
 *   - Bytes 5-11: Chip ID (e.g., "AKD1500")
 */
uint8_t adv_manufacturer_data[] = {

    /* BLE Protocol Version (2 bytes) - BLE 5.3 */
    0x35, // (decimal) 53 = ASCII '5'
    0x33, // (decimal) 51 = ASCII '3'

    /* Firmware Version (3 bytes) - v0.0.0 */
    0x30, // (decimal) 50 = ASCII '2'
    0x30, // (decimal) 52 = ASCII '4'
    0x30, // (decimal) 49 = ASCII '1'

    /* Chip ID - AKD1500 */
    0x41, // (decimal) 65 = ASCII 'A'
    0x4B, // (decimal) 75 = ASCII 'K'
    0x44, // (decimal) 68 = ASCII 'D'
    0x31, // (decimal) 49 = ASCII '1'
    0x35, // (decimal) 53 = ASCII '5'
    0x30, // (decimal) 48 = ASCII '0'
    0x30, // (decimal) 48 = ASCII '0'
};

/* Stores the unique 64-bit hardware device ID read from the MCU.
 * Used to uniquely identify the device during runtime or communication.
 */
static device_id_128_t device_id;

/* BLE advertising data including flags, device name, and manufacturer-specific
 * data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_manufacturer_data,
            sizeof(adv_manufacturer_data)),
};
/* BLE scan response data containing the 128-bit UUID of the Device ID */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_UUID128_ALL, &device_id, sizeof(device_id)),
};

/**
 * @brief Send a raw byte buffer over NUS with flow control.
 *
 * Owns the send_in_progress mutex and a bounded retry loop. Shared by the
 * ASCII send_frame() path and the binary send_pcm_wave() path.
 *
 * @param buf Pointer to bytes to send
 * @param len Number of bytes
 * @return int 0 on success, negative error code on failure
 */
static int nus_send_raw(const uint8_t *buf, size_t len) {
  const int max_retries = 10;

  if (!current_conn || !notifications_enabled) {
    return -ENOTCONN;
  }

  /* Wait for previous send to complete
     100 × 10 ms = 1000 ms */
  int wait_count = 0;
  while (send_in_progress && wait_count < SEND_FRAME_TIMEOUT_MS) {
    k_sleep(K_MSEC(10));
    wait_count++;
  }

  if (send_in_progress) {
    LOG_INF("Timeout waiting for previous send\n");
    return -EBUSY;
  }

  send_in_progress = true;

  for (int i = 0; i < max_retries; i++) {
    int err = bt_nus_send(current_conn, buf, len);

    if (err == 0) {
      return 0;
    } else if (err == -EAGAIN || err == -ENOMEM) {
      nus_retry_counter++;
      LOG_INF("Retry %d/%d (err=%d)\n", i + 1, max_retries, err);
      k_sleep(K_MSEC(50));
    } else {
      send_in_progress = false;
      LOG_ERR("Fatal error %d\n", err);
      return err;
    }
  }

  send_in_progress = false;
  LOG_ERR("Failed after %d retries\n", max_retries);
  return -ETIMEDOUT;
}

/**
 * @brief Send an ASCII frame to the connected phone.
 *
 * Thin wrapper around nus_send_raw() that computes strlen and casts.
 *
 * @param frame Null-terminated string to send
 * @return int 0 on success, negative error code on failure
 */
int send_frame(const char *frame) {
  return nus_send_raw((const uint8_t *)frame, strlen(frame));
}

/**
 * @brief Parse an incoming frame from the phone
 *
 * Expected format: "<frame_type>,<index>,<size>,<command>"
 *
 * @param data Raw received data
 * @param frame Pointer to parsed_frame_t structure to fill
 * @return bool true if parsing successful, false otherwise
 */
static bool parse_incoming_frame(const char *data, parsed_frame_t *frame) {
  int ft, idx, sz, cmd;
  int parsed = sscanf(data, "%d,%d,%d,%d", &ft, &idx, &sz, &cmd);

  if (parsed != 4) {
    LOG_ERR("Failed to parse frame (got %d fields)\n", parsed);
    return false;
  }

  frame->frame_type = (uint8_t)ft;
  frame->index = (uint8_t)idx;
  frame->size = (uint8_t)sz;
  frame->command = (uint8_t)cmd;

  /* Locate the command-specific payload, which begins after "<cmd>:". The
   * frame format is "<ft>,<idx>,<sz>,<cmd>:<payload>\r". We skip past the 4th
   * comma and the numeric cmd to find the ':'. */
  const char *colon = strchr(data, ':');
  frame->payload = colon ? (colon + 1) : NULL;

  LOG_INF("frame_type=%d, index=%d, size=%d, command=%d, payload=\"%s\"\n",
          frame->frame_type, frame->index, frame->size, frame->command,
          frame->payload ? frame->payload : "");

  return true;
}

/**
 * @brief Send device information as multi-frame response
 *
 * Splits device info into multiple frames:
 *   Frame 1 (MF_START): Manufacturer
 *   Frame 2 (MF_MID):   Type
 *   Frame 3 (MF_MID):   Version
 *   Frame 4 (MF_LAST):  Firmware
 *
 * Updates GATT characteristic with complete info.
 */
static void send_device_info_response(void) {
  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];
  int err;
  uint8_t frame_index = 0;

  LOG_INF("SENDING DEVICE INFO (MULTI)      \n");

  /* Frame 1: MF-START - Manufacturer */
  int data_len = snprintf(data_part, sizeof(data_part), "%d:%s,\r",
                          CMD_DEVICE_INFO, device_manufacturer);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_START, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 2: MF-MID - Type */
  data_len = snprintf(data_part, sizeof(data_part), "%d:%s,\r", CMD_DEVICE_INFO,
                      device_type);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_MID, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 3: MF-MID - Version */
  data_len = snprintf(data_part, sizeof(data_part), "%d:%s,\r", CMD_DEVICE_INFO,
                      device_version);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_MID, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 4: MF-LAST - Firmware */
  data_len = snprintf(data_part, sizeof(data_part), "%d:%s,\r", CMD_DEVICE_INFO,
                      device_firmware);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_LAST, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
}
bool wave_fallback_active(void) { return wave_fallback; }

/**
 * @brief Send a binary PCM waveform frame over NUS.
 *
 * Builds the 6-byte header + int16 LE payload described in the header doc for
 * send_pcm_wave(), then hands it to nus_send_raw(). Tracks retry rate and MTU
 * across calls to flip wave_fallback between envelope and decimation modes.
 *
 * Caller passes envelope data when n_samples=64 (32 interleaved min/max pairs)
 * or decimated samples when n_samples=32. The caller is responsible for
 * consulting wave_fallback_active() to decide which to send this block.
 */
void send_pcm_wave(const int16_t *samples, uint16_t n_samples) {
  if (!current_conn || !notifications_enabled) {
    return;
  }
  if (n_samples != WAVE_ENV_N_SAMPLES && n_samples != WAVE_DEC_N_SAMPLES) {
    LOG_WRN("send_pcm_wave: unsupported n_samples=%u", n_samples);
    return;
  }

  static uint8_t buf[WAVE_ENV_FRAME_LEN];
  static uint16_t seq = 0;
  static uint32_t last_retry_snapshot = 0;
  static uint16_t frames_in_window = 0;
  static uint16_t retries_in_window = 0;
  static uint16_t clean_frames = 0;

  size_t frame_len = WAVE_HEADER_LEN + (size_t)n_samples * sizeof(int16_t);

  buf[0] = WAVE_MAGIC;
  buf[1] = (uint8_t)CMD_STREAM_WAVE;
  buf[2] = (uint8_t)(seq & 0xFF);
  buf[3] = (uint8_t)((seq >> 8) & 0xFF);
  buf[4] = (uint8_t)(n_samples & 0xFF);
  buf[5] = (uint8_t)((n_samples >> 8) & 0xFF);
  seq++;

  memcpy(&buf[WAVE_HEADER_LEN], samples, (size_t)n_samples * sizeof(int16_t));

  int err = nus_send_raw(buf, frame_len);

  uint32_t retry_delta = nus_retry_counter - last_retry_snapshot;
  last_retry_snapshot = nus_retry_counter;
  frames_in_window++;
  retries_in_window += (uint16_t)retry_delta;
  if (err == 0 && retry_delta == 0) {
    clean_frames++;
  } else {
    clean_frames = 0;
  }

  if (frames_in_window >= WAVE_FALLBACK_WINDOW) {
    if (!wave_fallback && retries_in_window > WAVE_FALLBACK_RETRY_THRESHOLD) {
      LOG_WRN("wave: %u retries in last %u frames, downshift to decimation",
              retries_in_window, WAVE_FALLBACK_WINDOW);
      wave_fallback = true;
    }
    frames_in_window = 0;
    retries_in_window = 0;
  }

  if (wave_fallback && clean_frames >= WAVE_RECOVER_CLEAN_FRAMES &&
      wave_negotiated_mtu >= WAVE_MTU_MIN_FOR_ENVELOPE) {
    LOG_INF("wave: link recovered, restoring envelope mode");
    wave_fallback = false;
    clean_frames = 0;
  }
}
#ifdef CONFIG_SPARK_BOARD
/**
 * @brief Send current value data to phone for real-time monitoring
 *
 * Format: "<CMD_CURRENT_START>:<data_1_8>,<data_0_8>\r"
 * @param data Pointer to string containing current values to send
 */
void send_current_value(char *data) {
  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];

  snprintf(data_part, sizeof(data_part), "%d:%s\r", CMD_CURRENT_START, data);
  int data_len = strlen(data_part);

  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_SINGLE, 0, data_len,
           data_part);

  int err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send (err=%d)\n", err);
  }
}
#endif
/**
 * @brief Send a command event frame over the communication channel.
 *
 * Constructs a framed message with the given command ID, label, and numeric
 * value, then transmits it via send_frame().
 *
 * Format: "<FRAME_SINGLE>,<seq>,<data_len>,<cmd>:<label>,<value>\r"
 * Example: "1,0,12,11:cat,0.91\r"
 *
 * @param cmd     Command ID to embed in the data part (e.g. CMD_DEPLOY_START).
 * @param label   Null-terminated string label (e.g. keyword, class name).
 * @param value   Numeric confidence or strength value (0.0 – 1.0 typical).
 */
void send_event(int cmd, const char *label, float value) {

  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];

  /* Format data part: CMD:<label>,<value>\r */
  snprintf(data_part, sizeof(data_part), "%d:%s,%.2f\r", cmd, label,
           (double)value);
  int data_len = strlen(data_part);

  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_SINGLE, 0, data_len,
           data_part);

  int err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send event (cmd=%d label=%s err=%d)", cmd, label, err);
  } else {
    LOG_INF("Event sent: cmd=%d label=\"%s\" value=%.2f", cmd, label,
            (double)value);
  }
}
/**
 * @brief Sends an acknowledgment response to the mobile app
 *
 * @param ack_code  Acknowledgment code for success
 * @param cmd       The command type being acknowledged (from command_type_t
 * enum)
 *
 * Frame format: [frame_type],[index],[size],[command]:[ack_code]\r
 *
 * @return void (errors are logged internally)
 */
static void send_ack(uint8_t ack_code, command_type_t cmd) {
  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];

  snprintf(data_part, sizeof(data_part), "%d:%d\r", cmd, ack_code);
  int data_len = strlen(data_part);

  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_SINGLE, 0, data_len,
           data_part);

  int err = send_frame(frame);
  if (err) {
    LOG_ERR(" Failed to send ACK (err=%d)\n", err);
  } else {
    LOG_INF(" Data part: \"%s\" (len=%d)\n", data_part, data_len);
    LOG_INF(" ACK Sent successfully\n");
  }
}
/**
 * @brief Sends application display information over the communication
 * interface.
 *
 * This function prepares and sends formatted frames containing application
 * display metadata such as application name, description, and application size.
 *
 * The information is formatted into data payloads (`data_part`), then wrapped
 * into transmission frames (`frame`) following the defined protocol format:
 *
 *     FRAME_TYPE,FRAME_INDEX,DATA_LENGTH,DATA
 *
 * Frames are transmitted sequentially using the send_frame() function.
 *
 * Frame Structure:
 * - Frame 1: MF-START - Application name
 * - Frame 2: MF-MID  - Application description
 * - Frame 3: MF-LAST - Application size
 *
 * @note All application information values are retrieved from the info.yaml
 *       configuration at runtime.
 *
 * @retval None
 */
static void app_display(void) {
  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];
  int err;
  uint8_t frame_index = 0;

  LOG_INF("SENDING DEVICE INFO (MULTI)      \n");

  /* Frame 1: MF-START - app_name */
  int data_len =
      snprintf(data_part, sizeof(data_part), "%d:%s,\r", CMD_APPS, app_name);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_START, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 2: MF-MID - description */
  data_len =
      snprintf(data_part, sizeof(data_part), "%d:%s,\r", CMD_APPS, description);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_MID, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 3: MF-LAST - app_size */
  data_len =
      snprintf(data_part, sizeof(data_part), "%d:%d,\r", CMD_APPS, app_size);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_LAST, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
}
/**
 * @brief Sends application information over the communication interface.
 *
 * Emits a multi-frame CMD_APP_INFO burst containing model metadata and the
 * trained keyword list. Frames follow the protocol format:
 *
 *     FRAME_TYPE,FRAME_INDEX,DATA_LENGTH,DATA
 *
 * Frame Structure:
 * - Frame 1: MF-START - Model name ("DS_CNN")
 * - Frame 2: MF-MID   - Input shape ("X x Y x Z")
 * - Frame 3: MF-MID   - Number of user-visible classes
 * - Frame 4: MF-LAST  - Keyword list, ';'-delimited (kws_new_tags[0..9])
 *
 * @retval None
 */
static void app_info(void) {

  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];
  int err;
  uint8_t frame_index = 0;

  LOG_INF("SENDING DEVICE INFO (MULTI)      \n");

  /* Frame 1: MF-START - model name */
  int data_len = snprintf(data_part, sizeof(data_part), "%d:%s,\r",
                          CMD_APP_INFO, "DS_CNN");
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_START, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 2: MF-MID - input shape */
  data_len = snprintf(data_part, sizeof(data_part), "%d:%d x %d x %d,\r",
                      CMD_APP_INFO, kws_meta.input_shape[0],
                      kws_meta.input_shape[1], kws_meta.input_shape[2]);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_MID, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 3: MF-MID - number of user-visible classes (10 keywords + silence +
   * unknown, excluding the 3 edge-learning slots in kws_new_tags[]). */
  data_len =
      snprintf(data_part, sizeof(data_part), "%d:%d,\r", CMD_APP_INFO, 12);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_MID, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
  frame_index++;

  /* Frame 4: MF-LAST - keyword list "kw0;kw1;...;kw9" from kws_new_tags[0..9]
   */
  int off = snprintf(data_part, sizeof(data_part), "%d:", CMD_APP_INFO);
  for (int i = 0; i < 10 && off < (int)sizeof(data_part); i++) {
    off += snprintf(data_part + off, sizeof(data_part) - off, "%s%s",
                    kws_new_tags[i], (i == 9) ? ",\r" : ";");
  }
  data_len = off;
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_MF_LAST, frame_index,
           data_len, data_part);
  LOG_INF("  Frame %d: data=\"%s\" (len=%d)\n", frame_index + 1, data_part,
          data_len);
  err = send_frame(frame);
  if (err) {
    LOG_ERR("Failed to send frame %d (err %d)", frame_index + 1, err);
    return;
  }
}

/* Performs a software-triggered cold reboot of the device.
 * This resets the system and restarts firmware execution from boot.
 */
static void restart_device(void) { sys_reboot(SYS_REBOOT_COLD); }

/**
 * @brief Retrieve device ID from FICR.
 *
 * Reads the hardware DEVICEID registers and combines them into a
 * 128-bit structure. The 64-bit device identifier is stored in the
 * high field, while the low field is set to 0.
 *
 * @return uint128_t Device identifier.
 */
static void get_device_id(void) {
  uint32_t id0 = NRF_FICR->INFO.DEVICEID[0];
  uint32_t id1 = NRF_FICR->INFO.DEVICEID[1];

  device_id.high = ((uint64_t)id1 << 32) | id0;
  device_id.low = 0;
}

/* Emit the 6-frame CMD_CONFIG snapshot burst — shared by GET and post-RESET
 * replies. Matches the multi-frame style used by send_device_info_response()
 * / app_info(): MF_START, four MF_MID, MF_LAST, one param per frame. */
static void send_config_response(void) {
  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];
  char value_part[48];

  static const uint8_t frame_types[KWS_PARAM_COUNT] = {
      FRAME_MF_START, FRAME_MF_MID, FRAME_MF_MID,
      FRAME_MF_MID,   FRAME_MF_MID, FRAME_MF_LAST,
  };

  LOG_INF("SENDING CONFIG SNAPSHOT (MULTI)\n");

  for (uint8_t i = 0; i < KWS_PARAM_COUNT; i++) {
    int vlen =
        kws_config_format((kws_param_id_t)i, value_part, sizeof(value_part));
    if (vlen < 0) {
      LOG_ERR("kws_config_format(%u) failed", i);
      return;
    }
    int data_len = snprintf(data_part, sizeof(data_part), "%d:%s\r", CMD_CONFIG,
                            value_part);
    snprintf(frame, sizeof(frame), "%d,%u,%d,%s", frame_types[i], i, data_len,
             data_part);
    int err = send_frame(frame);
    if (err) {
      LOG_ERR("send_config_response frame %u err %d", i, err);
      return;
    }
  }
}

/* Single-frame ACK for a CMD_CONFIG SET result. err_reason is NULL on success
 * (→ "4:<id>:OK"), otherwise it's a short token ("ID","RANGE","PARSE","NVS").
 */
static void send_config_ack(kws_param_id_t id, const char *err_reason) {
  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];

  int data_len;
  if (err_reason == NULL) {
    data_len = snprintf(data_part, sizeof(data_part), "%d:%d:OK\r", CMD_CONFIG,
                        (int)id);
  } else {
    data_len = snprintf(data_part, sizeof(data_part), "%d:%d:ERR:%s\r",
                        CMD_CONFIG, (int)id, err_reason);
  }
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_SINGLE, 0, data_len,
           data_part);
  int err = send_frame(frame);
  if (err) {
    LOG_ERR("send_config_ack err %d", err);
  }
}

/* Single-frame ACK for a CMD_CONFIG RESET — followed by the 6-frame snapshot
 * so the phone can refresh without a separate GET. */
static void send_config_reset_ack(void) {
  char frame[FRAME_BUFFER_SIZE];
  char data_part[DATA_PART_SIZE];

  int data_len =
      snprintf(data_part, sizeof(data_part), "%d:RESET:OK\r", CMD_CONFIG);
  snprintf(frame, sizeof(frame), "%d,%d,%d,%s", FRAME_SINGLE, 0, data_len,
           data_part);
  int err = send_frame(frame);
  if (err) {
    LOG_ERR("send_config_reset_ack err %d", err);
  }
}

/* Map kws_cfg_err_t to the short wire token the phone switches on. */
static const char *cfg_err_to_str(kws_cfg_err_t err) {
  switch (err) {
  case KWS_CFG_OK:
    return NULL;
  case KWS_CFG_ERR_ID:
    return "ID";
  case KWS_CFG_ERR_RANGE:
    return "RANGE";
  case KWS_CFG_ERR_PARSE:
    return "PARSE";
  case KWS_CFG_ERR_NVS:
    return "NVS";
  default:
    return "UNKNOWN";
  }
}

/* CMD_CONFIG payload dispatcher. Payload is everything after "4:" in the
 * incoming frame (null-terminated, may have a trailing '\r').
 *
 * Shapes:
 *   "GET"                → reply with 6-frame snapshot
 *   "RESET"              → reset all to defaults, ACK + 6-frame snapshot
 *   "<param_id>:<value>" → set single param, single-frame ACK
 */
static void handle_config_command(const char *payload) {
  if (!payload) {
    LOG_ERR("CMD_CONFIG with empty payload");
    return;
  }

  if (strncmp(payload, "GET", 3) == 0) {
    send_config_response();
    return;
  }

  if (strncmp(payload, "RESET", 5) == 0) {
    kws_cfg_err_t err = kws_config_reset_to_defaults();
    if (err != KWS_CFG_OK && err != KWS_CFG_ERR_NVS) {
      LOG_ERR("kws_config_reset_to_defaults err %d", err);
    }
    send_config_reset_ack();
    send_config_response();
    return;
  }

  /* SET: "<param_id>:<value>" */
  char *sep = strchr(payload, ':');
  if (!sep) {
    LOG_ERR("CMD_CONFIG SET missing ':' in payload \"%s\"", payload);
    send_config_ack((kws_param_id_t)0, "PARSE");
    return;
  }

  char id_buf[8];
  size_t id_len = (size_t)(sep - payload);
  if (id_len == 0 || id_len >= sizeof(id_buf)) {
    send_config_ack((kws_param_id_t)0, "PARSE");
    return;
  }
  memcpy(id_buf, payload, id_len);
  id_buf[id_len] = '\0';

  char *endp = NULL;
  long id_l = strtol(id_buf, &endp, 10);
  if (endp == id_buf || *endp != '\0' || id_l < 0 || id_l >= KWS_PARAM_COUNT) {
    send_config_ack((kws_param_id_t)0, "ID");
    return;
  }
  kws_param_id_t id = (kws_param_id_t)id_l;

  /* The value runs from after ':' to '\r' / '\0'. kws_config_set_from_string
   * also strips trailing whitespace, so we don't need to copy — but we do
   * need a NUL-terminated buffer without the '\r'. */
  char value_buf[32];
  const char *val = sep + 1;
  size_t val_len = strlen(val);
  while (val_len > 0 && (val[val_len - 1] == '\r' || val[val_len - 1] == '\n' ||
                         val[val_len - 1] == ' ')) {
    val_len--;
  }
  if (val_len == 0 || val_len >= sizeof(value_buf)) {
    send_config_ack(id, "PARSE");
    return;
  }
  memcpy(value_buf, val, val_len);
  value_buf[val_len] = '\0';

  kws_cfg_err_t rc = kws_config_set_from_string(id, value_buf);
  send_config_ack(id, cfg_err_to_str(rc));
}

/**
 * @brief Callback when data is received via NUS
 *
 * Parses the received frame and executes the appropriate command.
 *
 * @param conn Connection object
 * @param data Received data
 * @param len Length of received data
 */
static void nus_received_cb(struct bt_conn *conn, const uint8_t *const data,
                            uint16_t len) {
  char received_data[MAX_NUS_RX_BUFFER_SIZE];

  if (len < sizeof(received_data)) {
    memcpy(received_data, data, len);
    received_data[len] = '\0';
  } else {
    memcpy(received_data, data, sizeof(received_data) - 1);
    received_data[sizeof(received_data) - 1] = '\0';
  }

  LOG_INF("\nReceived %u bytes: \"%s\"\n", len, received_data);

  /* Parse the frame */
  parsed_frame_t frame;
  if (!parse_incoming_frame(received_data, &frame)) {
    LOG_ERR("Invalid frame format\n");
    return;
  }

  /* Handle based on command type */
  switch (frame.command) {
  case CMD_APPS:
    LOG_INF("APPS command received\n");
    app_display();
    app_start_flag = FLAG_ENABLE;
    break;

  case CMD_BATTERY:
    LOG_INF("BATTERY command received\n");
    battery_service_send(CMD_BATTERY);
    break;

  case CMD_DEVICE_INFO:
    LOG_INF("DEVICE_INFO command received\n");
    send_device_info_response();
    break;
  case CMD_APP_INFO:
    LOG_INF("APP INFO command received\n");
    app_info();
    break;
  case CMD_CONFIG:
    LOG_INF("CONFIG command received\n");
    handle_config_command(frame.payload);
    break;
  case CMD_DEPLOY_START:
    LOG_INF("DEPLOY START command received\n");
    kws_app_start();
    event_flag = FLAG_ENABLE;
    send_ack(ACK_DONE, CMD_DEPLOY_START);
    break;
  case CMD_STREAM_START:
    LOG_INF("STREAM START command received\n");
    current_stream_flag = FLAG_DISABLE;
    pdm_stream_flag = FLAG_ENABLE;
    break;
  case CMD_DEPLOY_STOP:
    LOG_INF("DEPLOY STOP command received\n");
    event_flag = FLAG_DISABLE;
    kws_app_stop();
    send_ack(ACK_DONE, CMD_DEPLOY_STOP);
    break;
  case CMD_STREAM_STOP:
    LOG_INF("STREAM STOP command received\n");
    pdm_stream_flag = FLAG_DISABLE;
    send_ack(ACK_DONE, CMD_STREAM_STOP);
    break;
  case CMD_CURRENT_START:
    LOG_INF("STREAM CURRENT START command received\n");
    pdm_stream_flag = FLAG_DISABLE;
    current_stream_flag = FLAG_ENABLE;
#ifdef CONFIG_SPARK_BOARD
    k_sem_give(&current_stream_sem);
#endif
    break;
  case CMD_CURRENT_STOP:
    LOG_INF("STREAM CURRENT STOP command received\n");
    current_stream_flag = FLAG_DISABLE;
    send_ack(ACK_DONE, CMD_CURRENT_STOP);
    break;
  case CMD_RESET:
    LOG_INF("RESET command received\n");
    send_ack(ACK_DONE, CMD_RESET);
    /* Delay to ensure ACK is transmitted over BLE */
    k_sleep(K_MSEC(200));
    restart_device();
    break;
  default:
    LOG_INF("Command %d not implemented\n", frame.command);
    break;
  }
}

/**
 * @brief Callback when data has been sent via NUS
 *
 * Clears the send_in_progress flag.
 *
 * @param conn Connection object
 */
static void nus_sent_cb(struct bt_conn *conn) { send_in_progress = false; }

/**
 * @brief Callback when NUS notifications are enabled/disabled
 *
 * @param status New notification status
 */
static void nus_send_enabled_cb(enum bt_nus_send_status status) {
  if (status == BT_NUS_SEND_STATUS_ENABLED) {
    notifications_enabled = true;
    LOG_INF("  NUS NOTIFICATIONS ENABLED       \n");
    LOG_INF("  Ready to receive commands!\n\n");
  } else {
    notifications_enabled = false;
    LOG_INF("\nNotifications DISABLED\n\n");
  }
}

static struct bt_nus_cb nus_callbacks = {
    .received = nus_received_cb,
    .sent = nus_sent_cb,
    .send_enabled = nus_send_enabled_cb,
};

/**
 * @brief MTU exchange completion callback.
 *
 * Records the negotiated MTU and proactively forces wave_fallback=true if the
 * resulting payload is too small for the 134-byte envelope frame. Peer can
 * reject the request, in which case we stay at the default 23-byte ATT MTU.
 */
static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
                            struct bt_gatt_exchange_params *params) {
  ARG_UNUSED(params);
  if (err) {
    LOG_WRN("MTU exchange failed (err %u)", err);
    wave_fallback = true;
    return;
  }
  wave_negotiated_mtu = bt_gatt_get_mtu(conn);
  LOG_INF("MTU exchanged: %u bytes", wave_negotiated_mtu);
  if (wave_negotiated_mtu < WAVE_MTU_MIN_FOR_ENVELOPE) {
    LOG_WRN("MTU %u < %u, starting wave stream in decimation fallback",
            wave_negotiated_mtu, WAVE_MTU_MIN_FOR_ENVELOPE);
    wave_fallback = true;
  } else {
    wave_fallback = false;
  }
}

static struct bt_gatt_exchange_params mtu_exchange_params;

/* Link-param update callbacks: diagnostic only. Auto-updates are enabled in
 * prj.conf (CONFIG_BT_AUTO_PHY_UPDATE, CONFIG_BT_AUTO_DATA_LEN_UPDATE) so the
 * requests are issued by the stack; we just log the outcome here. */
static void le_param_updated_cb(struct bt_conn *conn, uint16_t interval,
                                uint16_t latency, uint16_t timeout) {
  ARG_UNUSED(conn);
  LOG_INF("Conn params updated: interval=%u (%u.%02u ms), latency=%u, "
          "timeout=%u",
          interval, (interval * 125) / 100, (interval * 125) % 100, latency,
          timeout);
}

static void le_phy_updated_cb(struct bt_conn *conn,
                              struct bt_conn_le_phy_info *param) {
  ARG_UNUSED(conn);
  LOG_INF("PHY updated: tx=%u, rx=%u", param->tx_phy, param->rx_phy);
}

static void le_data_len_updated_cb(struct bt_conn *conn,
                                   struct bt_conn_le_data_len_info *info) {
  ARG_UNUSED(conn);
  LOG_INF("Data length updated: tx_max_len=%u, rx_max_len=%u", info->tx_max_len,
          info->rx_max_len);
}

static void connected_ble(struct bt_conn *conn, uint8_t err) {

  if (err) {
    LOG_ERR("Connection failed (err 0x%02x)\n", err);
    return;
  }
  led_set_state(LED_STATE_BLE_CONNECTED);
  ble_connection_callback(BLE_CONNECTED);
  LOG_INF("connected_ble\n");
  current_conn = bt_conn_ref(conn);

  /* Reset waveform stream link-quality state for this fresh connection.
   * The real values land once MTU exchange / link-param updates complete. */
  wave_fallback = false;
  wave_negotiated_mtu = 23;

  /* Peripheral-initiated MTU exchange. Centrals usually initiate this on their
   * own on Android/iOS, but asking here guarantees it happens before we start
   * streaming waveform data, and lets mtu_exchange_cb set wave_fallback
   * proactively if the negotiated MTU is too small. */
  mtu_exchange_params.func = mtu_exchange_cb;
  int mtu_err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
  if (mtu_err) {
    LOG_WRN("bt_gatt_exchange_mtu() request failed (err %d)", mtu_err);
  }

#ifdef CONFIG_BT_ENCRYPTION_EN
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  // FORCE SECURITY UPGRADE TO LEVEL 4
  err = bt_conn_set_security(conn, BT_SECURITY_L4);
  if (err) {
    LOG_ERR("Failed to set security (err %d)\n", err);
  } else {
    LOG_INF("Security level 4 requested - pairing should start\n");
  }
#endif
}

static void disconnected_ble(struct bt_conn *conn, uint8_t reason) {
  LOG_INF("Disconnected_ble (reason %u)\n", reason);

  if (current_conn) {
    bt_conn_unref(current_conn);
    current_conn = NULL;
  }
  dk_set_led_off(CON_STATUS_LED);
  led_set_state(LED_STATE_NORMAL_APP);
  ble_connection_callback(BLE_NOT_CONNECTED);
  app_start_flag = FLAG_DISABLE;
  send_in_progress = false;
  pdm_stream_flag = FLAG_DISABLE;
  event_flag = FLAG_DISABLE;
#ifdef CONFIG_SPARK_BOARD
  battery_service_on_disconnect();
#endif
}

#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  if (!err) {
    LOG_INF("Security changed: %s level %u\n", addr, level);
  } else {
    LOG_ERR("Security failed: %s level %u err %d\n", addr, level, err);
  }
}
#endif

static void recycled_cb(void) {
  int err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,
                                            BT_GAP_ADV_FAST_INT_MIN_2,
                                            BT_GAP_ADV_FAST_INT_MAX_2, NULL),
                            ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err) {
    LOG_ERR("Advertising restart failed (err %d)\n", err);
  } else {
    LOG_INF("Advertising restarted\n");
  }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected_ble,
    .disconnected = disconnected_ble,
    .recycled = recycled_cb,
    .le_param_updated = le_param_updated_cb,
    .le_phy_updated = le_phy_updated_cb,
    .le_data_len_updated = le_data_len_updated_cb,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
    .security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_LBS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
    .passkey_display = auth_passkey_display,
    .cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
    .pairing_complete = pairing_complete, .pairing_failed = pairing_failed};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif
#ifdef CONFIG_DK_BOARD
static void app_led_cb(bool led_state) { dk_set_led(USER_LED, led_state); }

static bool app_button_cb(void) { return app_button_state; }

static struct bt_lbs_cb lbs_callbacs = {
    .led_cb = app_led_cb,
    .button_cb = app_button_cb,
};

static void button_changed(uint32_t button_state, uint32_t has_changed) {
  if (has_changed & USER_BUTTON) {
    uint32_t user_button_state = button_state & USER_BUTTON;

    bt_lbs_send_button_state(user_button_state);
    app_button_state = user_button_state ? true : false;
  }
}

static int init_button(void) {
  int err;

  err = dk_buttons_init(button_changed);
  if (err) {
    LOG_ERR("Cannot init buttons (err: %d)\n", err);
  }

  return err;
}

#endif

int ble_init(void)

{

  int err;

  LOG_INF("BLE Initialization \n");

#ifdef CONFIG_DK_BOARD

  err = dk_leds_init();
  if (err) {
    LOG_ERR("LEDs init failed (err %d)\n", err);
    return -1;
  }

  err = init_button();
  if (err) {
    LOG_INF("Button init failed (err %d)\n", err);
    return -1;
  }

#endif

  if (IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED)) {
    err = bt_conn_auth_cb_register(&conn_auth_callbacks);
    if (err) {
      LOG_ERR("Failed to register authorization callbacks.\n");
      return -1;
    }

    err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
    if (err) {
      LOG_ERR("Failed to register authorization info callbacks.\n");
      return -1;
    }
    LOG_INF("BLE Security is enabled");

    LOG_INF("with Passkey");

  } else {
    LOG_INF("without Passkey");
  }

  err = bt_enable(NULL);
  if (err) {
    LOG_ERR("Bluetooth init failed (err %d)\n", err);
    return -1;
  }

  LOG_INF("Bluetooth initialized\n");

  err = bt_nus_init(&nus_callbacks);
  if (err) {
    LOG_ERR("NUS init failed (err %d)\n", err);
    return -1;
  }
  LOG_INF("NUS initialized\n");
  if (IS_ENABLED(CONFIG_SETTINGS)) {
    settings_load_subtree("bt");
    settings_load_subtree("boot");
  }

#ifdef CONFIG_DK_BOARD
  err = bt_lbs_init(&lbs_callbacs);
  if (err) {
    LOG_ERR("Failed to init LBS (err:%d)\n", err);
    return -1;
  }
#endif
  get_device_id();
  err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,
                                        BT_GAP_ADV_FAST_INT_MIN_2,
                                        BT_GAP_ADV_FAST_INT_MAX_2, NULL),
                        ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err) {
    LOG_ERR("Advertising failed to start (err %d)\n", err);
    return -1;
  }

  LOG_INF("Advertising successfully started\n");
  return 0;
}

static int cmd_get_device_id(const struct shell *shell, size_t argc,
                             char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  get_device_id();

  shell_print(shell, "Device ID: 0x%016llx%016llx", device_id.high,
              device_id.low);

  return 0;
}
SHELL_CMD_REGISTER(device_id, NULL, "Print device ID", cmd_get_device_id);