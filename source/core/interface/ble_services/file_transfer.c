#include "ble_services/file_transfer.h"
#include "akd_spi_flash_handler.h"

#include "led_init.h"

#include <stddef.h>

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

// Define an event object used for SRAM buffer synchronization
K_EVENT_DEFINE(sram_buf_event);

LOG_MODULE_REGISTER(file_transfer, CONFIG_LOG_DEFAULT_LEVEL);

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static void send_ack_to_host(uint8_t ack_code);

static ssize_t get_app_index(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *app,
                             uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t file_transfer_write(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags);
static ssize_t get_file_size(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_file_crc(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t set_transfer_type(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags);
/* New characteristic handlers */
static ssize_t get_input_shape(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr, const void *buf,
                               uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_output_shape(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags);
static ssize_t get_flash_address(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags);
static ssize_t get_total_length(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags);
static ssize_t get_is_edge_learned(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags);
static ssize_t get_num_edge_classes(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    const void *buf, uint16_t len,
                                    uint16_t offset, uint8_t flags);
static ssize_t get_fs_name(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr, const void *buf,
                           uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_column_usage(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags);

/* -------------------------------------------------------------------------
 * UUID definitions – must match Python send_model_via_ble.py
 * ---------------------------------------------------------------------- */
#define BT_UUID_FILE_TRANSFER_SERVICE_VAL                                      \
  BT_UUID_128_ENCODE(0xf000aa00, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_CHAR_VAL                                         \
  BT_UUID_128_ENCODE(0xf000aa01, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_ACK_CHAR_VAL                                     \
  BT_UUID_128_ENCODE(0xf000aa02, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_CTRL_CHAR_VAL                                    \
  BT_UUID_128_ENCODE(0xf000aa03, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_SIZE_CHAR_VAL                                    \
  BT_UUID_128_ENCODE(0xf000aa04, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define APP_CHAR_UUID_VAL                                                      \
  BT_UUID_128_ENCODE(0xf000aa05, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_CRC_CHAR_VAL                                              \
  BT_UUID_128_ENCODE(0xf000aa06, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_TRANSFER_TYPE_CHAR_VAL                                         \
  BT_UUID_128_ENCODE(0xf000aa07, 0x0451, 0x4000, 0xb000, 0x000000000000)
/* New metadata characteristics (aa08–aa0e) */
#define BT_UUID_INPUT_SHAPE_CHAR_VAL                                           \
  BT_UUID_128_ENCODE(0xf000aa08, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_OUTPUT_SHAPE_CHAR_VAL                                          \
  BT_UUID_128_ENCODE(0xf000aa09, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FLASH_ADDRESS_CHAR_VAL                                         \
  BT_UUID_128_ENCODE(0xf000aa0a, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_TOTAL_LENGTH_CHAR_VAL                                          \
  BT_UUID_128_ENCODE(0xf000aa0b, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_IS_EDGE_LEARNED_CHAR_VAL                                       \
  BT_UUID_128_ENCODE(0xf000aa0c, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_NUM_EDGE_CLASSES_CHAR_VAL                                      \
  BT_UUID_128_ENCODE(0xf000aa0d, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FS_NAME_CHAR_VAL                                               \
  BT_UUID_128_ENCODE(0xf000aa0e, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_COLUMN_USAGE_CHAR_VAL                                          \
  BT_UUID_128_ENCODE(0xf000aa0f, 0x0451, 0x4000, 0xb000, 0x000000000000)

/* UUID struct instances */
static struct bt_uuid_128 file_transfer_service_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_SERVICE_VAL);
static struct bt_uuid_128 file_transfer_char_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_CHAR_VAL);
static struct bt_uuid_128 file_transfer_ack_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_ACK_CHAR_VAL);
static struct bt_uuid_128 file_transfer_size_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_SIZE_CHAR_VAL);
static struct bt_uuid_128 app_char_uuid_struct =
    BT_UUID_INIT_128(APP_CHAR_UUID_VAL);
static struct bt_uuid_128 file_crc_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_CRC_CHAR_VAL);
static struct bt_uuid_128 transfer_type_uuid =
    BT_UUID_INIT_128(BT_UUID_TRANSFER_TYPE_CHAR_VAL);
static struct bt_uuid_128 input_shape_uuid =
    BT_UUID_INIT_128(BT_UUID_INPUT_SHAPE_CHAR_VAL);
static struct bt_uuid_128 output_shape_uuid =
    BT_UUID_INIT_128(BT_UUID_OUTPUT_SHAPE_CHAR_VAL);
static struct bt_uuid_128 flash_address_uuid =
    BT_UUID_INIT_128(BT_UUID_FLASH_ADDRESS_CHAR_VAL);
static struct bt_uuid_128 total_length_uuid =
    BT_UUID_INIT_128(BT_UUID_TOTAL_LENGTH_CHAR_VAL);
static struct bt_uuid_128 is_edge_learned_uuid =
    BT_UUID_INIT_128(BT_UUID_IS_EDGE_LEARNED_CHAR_VAL);
static struct bt_uuid_128 num_edge_classes_uuid =
    BT_UUID_INIT_128(BT_UUID_NUM_EDGE_CLASSES_CHAR_VAL);
static struct bt_uuid_128 fs_name_uuid =
    BT_UUID_INIT_128(BT_UUID_FS_NAME_CHAR_VAL);
static struct bt_uuid_128 column_usage_uuid =
    BT_UUID_INIT_128(BT_UUID_COLUMN_USAGE_CHAR_VAL);

/* UUID pointer macros */
#define FILE_SVC_UUID (&file_transfer_service_uuid.uuid)
#define FILE_CHAR_UUID (&file_transfer_char_uuid.uuid)
#define FILE_ACK_UUID (&file_transfer_ack_uuid.uuid)
#define FILE_SIZE_UUID (&file_transfer_size_uuid.uuid)
#define APP_CHAR_UUID_PTR (&app_char_uuid_struct.uuid)
#define FILE_CRC_UUID (&file_crc_uuid.uuid)
#define TRANSFER_TYPE_UUID (&transfer_type_uuid.uuid)
#define INPUT_SHAPE_UUID (&input_shape_uuid.uuid)
#define OUTPUT_SHAPE_UUID (&output_shape_uuid.uuid)
#define FLASH_ADDRESS_UUID (&flash_address_uuid.uuid)
#define TOTAL_LENGTH_UUID (&total_length_uuid.uuid)
#define IS_EDGE_LEARNED_UUID (&is_edge_learned_uuid.uuid)
#define NUM_EDGE_CLASSES_UUID (&num_edge_classes_uuid.uuid)
#define FS_NAME_UUID (&fs_name_uuid.uuid)
#define COLUMN_USAGE_UUID (&column_usage_uuid.uuid)

/* Permission shorthand */
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
#define WRITE_PERM BT_GATT_PERM_WRITE_ENCRYPT
#else
#define WRITE_PERM BT_GATT_PERM_WRITE
#endif

/* -------------------------------------------------------------------------
 * Transfer state
 * ---------------------------------------------------------------------- */
#define ACK_FLASH_ERASE_DONE 0xEE
#define ACK_FLASH_WRITE_DONE 0xCC
#define ACK_CRC_FAIL 0xBB
#define TRANSFER_TYPE_INFO 0x00
#define TRANSFER_TYPE_DATA 0x01

volatile static size_t total_pgm_size = 0;
volatile static size_t total_received = 0;
static size_t ble_pgm_offset = 0;   /* write cursor inside SRAM buffer  */
static size_t sram_info_offset = 0; /* write cursor inside info_data    */

static uint8_t transfer_type = TRANSFER_TYPE_DATA;
static uint32_t expected_crc = 0; /* CRC received from host via aa06  */
static uint32_t data_crc_state = 0xFFFFFFFF; /* running CRC for DATA transfer */
static uint32_t data_first_4_bytes = 0; /* first 4 raw bytes of model_data */

/* -------------------------------------------------------------------------
 * Metadata received via BLE characteristics (before INFO data stream)
 * ---------------------------------------------------------------------- */
static uint32_t meta_total_length = 0;
static uint32_t meta_input_shape[MAX_MODEL_INP_SHAPE_DIMS] = {0};
static uint32_t meta_output_shape[MAX_MODEL_OUTP_SHAPE_DIMS] = {0};
static uint32_t meta_flash_address = 0;
static uint32_t meta_is_edge_learned = 0;
static uint32_t meta_num_edge_classes = 0;
static char meta_fs_name[MAX_FS_NAME_LEN] = {0};
uint32_t g_column_usage_mask =
    0x0E; /* Bitmask of columns in use (bit i = col i is on) */

extern int infer(int app_index_l);

/* -------------------------------------------------------------------------
 * In-RAM metadata – one per app slot.
 * Populated when an INFO BLE transfer completes; also loaded from FS at boot.
 * ---------------------------------------------------------------------- */
static model_meta_t current_meta;

/* LittleFS file 1: model_meta_t header struct (app 0 = KWS/EL)
 */
static const char *meta_hdr_paths[] = {
    "/ext/kws_model_hdr",
};

/* LittleFS file 2: raw program_info binary (loaded into sram_upload_buffer) */
static const char *model_info_paths[] = {
    "/ext/kws_model_info",
};

/* LittleFS file 3: model_data_meta_t (CRC,  length, name) */
static const char *model_data_meta_paths[] = {
    "/ext/kws_model_data_hdr",
};

/* -------------------------------------------------------------------------
 * Dynamic path construction from meta_fs_name (set via FS_NAME_CHAR aa0e)
 *
 * build_fs_paths_from_name() extracts the model name (last component after
 * the final '/') from meta_fs_name and constructs the three LittleFS paths.
 * It validates the constructed hdr path against the hardcoded whitelist and
 * stores the matching slot index in dyn_app_slot (-1 if unknown).
 * ---------------------------------------------------------------------- */
static char dyn_hdr_path[80];  /* e.g. "/ext/kws_model_hdr"      */
static char dyn_info_path[80]; /* e.g. "/ext/kws_model_info"     */
static char dyn_data_path[80]; /* e.g. "/ext/kws_model_data_hdr" */
static int dyn_app_slot = -1;  /* index into meta_hdr_paths[]    */

static bool build_fs_paths_from_name(const char *fs_name) {
  /* Extract last path component: "kws" from "/model_meta/kws" */
  const char *name = strrchr(fs_name, '/');
  name = (name != NULL) ? name + 1 : fs_name;
  if (name[0] == '\0') {
    LOG_ERR("Empty model name in fs_name '%s'\n", fs_name);
    dyn_app_slot = -1;
    return false;
  }

  snprintf(dyn_hdr_path, sizeof(dyn_hdr_path), "/ext/%s_model_hdr", name);
  snprintf(dyn_info_path, sizeof(dyn_info_path), "/ext/%s_model_info", name);
  snprintf(dyn_data_path, sizeof(dyn_data_path), "/ext/%s_model_data_hdr",
           name);

  /* Validate against hardcoded whitelist */
  dyn_app_slot = -1;
  for (int i = 0; i < 1; i++) {
    if (strcmp(dyn_hdr_path, meta_hdr_paths[i]) == 0) {
      dyn_app_slot = i;
      LOG_INF("Model name '%s' → slot %d (hdr=%s)\n", name, i, dyn_hdr_path);
      return true;
    }
  }
  LOG_ERR("Unknown model name '%s' (hdr='%s') not in whitelist\n", name,
          dyn_hdr_path);
  return false;
}

/* -------------------------------------------------------------------------
 * SRAM upload buffer – used by DATA transfers (chunk staging)
 * ---------------------------------------------------------------------- */
__attribute__((section(".sram_upload_buf"), used))
uint8_t sram_upload_buffer[BUFFER_SIZE];

/* Flash write cursor – updated as chunks are flushed to SPI flash */
static uint32_t app_flash_offset = AKD_FLASH_OFFSET;

/* -------------------------------------------------------------------------
 * ACK notification
 * ---------------------------------------------------------------------- */
static bool notify_enabled = false;

static void ack_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                uint16_t value) {
  notify_enabled = (value == BT_GATT_CCC_NOTIFY);
  LOG_INF("ACK notify %s\n", notify_enabled ? "enabled" : "disabled");
}

/* -------------------------------------------------------------------------
 * GATT service definition
 * ---------------------------------------------------------------------- */
BT_GATT_SERVICE_DEFINE(
    file_transfer_svc, BT_GATT_PRIMARY_SERVICE(FILE_SVC_UUID),

    /* aa04 – file size (triggers flash erase on DATA; just acks on INFO) */
    BT_GATT_CHARACTERISTIC(FILE_SIZE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_file_size, NULL),

    /* aa01 – data chunk stream */
    BT_GATT_CHARACTERISTIC(FILE_CHAR_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           file_transfer_write, NULL),

    /* aa02 – ACK notify + CCCD */
    BT_GATT_CHARACTERISTIC(FILE_ACK_UUID, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(ack_ccc_cfg_changed,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT
#else
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
#endif
                ),

    /* aa05 – app index (0KWS) */
    BT_GATT_CHARACTERISTIC(APP_CHAR_UUID_PTR, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, get_app_index, NULL),

    /* aa06 – combined CRC32 (info+data) */
    BT_GATT_CHARACTERISTIC(FILE_CRC_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_file_crc, NULL),

    /* aa07 – transfer type (0=INFO, 1=DATA) */
    BT_GATT_CHARACTERISTIC(TRANSFER_TYPE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, set_transfer_type, NULL),

    /* aa08 – model input shape (N × 32-bit dims) */
    BT_GATT_CHARACTERISTIC(INPUT_SHAPE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, get_input_shape, NULL),

    /* aa09 – model output shape (N × 32-bit dims) */
    BT_GATT_CHARACTERISTIC(OUTPUT_SHAPE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, get_output_shape, NULL),

    /* aa0a – flash address for model data (32-bit) */
    BT_GATT_CHARACTERISTIC(FLASH_ADDRESS_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, get_flash_address, NULL),

    /* aa0b – combined info+data total byte count (32-bit) */
    BT_GATT_CHARACTERISTIC(TOTAL_LENGTH_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, get_total_length, NULL),

    /* aa0c – is_edge_learned flag (32-bit, 1=EL model) */
    BT_GATT_CHARACTERISTIC(IS_EDGE_LEARNED_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, get_is_edge_learned, NULL),

    /* aa0d – number of edge-learning classes (32-bit) */
    BT_GATT_CHARACTERISTIC(NUM_EDGE_CLASSES_UUID, BT_GATT_CHRC_WRITE,
                           WRITE_PERM, NULL, get_num_edge_classes, NULL),

    /* aa0e – LittleFS metadata path (UTF-8, optional) */
    BT_GATT_CHARACTERISTIC(FS_NAME_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_fs_name, NULL),

    /* aa0f – column usage bitmask (32-bit, bit i = col i is in use) */
    BT_GATT_CHARACTERISTIC(COLUMN_USAGE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM,
                           NULL, get_column_usage, NULL));

static void send_ack_to_host(uint8_t ack_code) {
  if (!notify_enabled) {
    LOG_ERR("ACK notification skipped: notify not enabled by central\n");
    return;
  }
  uint8_t ack_data[1] = {ack_code};
  /* attrs[5] = ACK characteristic – index stable as new chars are appended */
  int err = bt_gatt_notify(NULL, &file_transfer_svc.attrs[5], ack_data,
                           sizeof(ack_data));
  if (err) {
    LOG_ERR("Failed to send ACK (err %d)\n", err);
  } else {
    LOG_INF("ACK 0x%02X sent\n", ack_code);
  }
}

/* -------------------------------------------------------------------------
 * Characteristic write handlers – existing
 * ---------------------------------------------------------------------- */
static ssize_t set_transfer_type(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags) {
  if (len != 1) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  transfer_type = *(const uint8_t *)buf;
  LOG_INF("Transfer type → 0x%02X\n", transfer_type);
  return len;
}

static ssize_t get_file_crc(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
  if (len != 4) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memcpy(&expected_crc, buf, 4);
  LOG_INF("Combined CRC32: 0x%08X\n", expected_crc);
  return len;
}

ssize_t get_app_index(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                      const void *app, uint16_t len, uint16_t offset,
                      uint8_t flags) {
  if (app == NULL) {
    LOG_ERR("app index is NULL\n");
    return -1;
  }
  uint8_t app_index_local = *(const uint8_t *)app;
  if (app_index_local > 0) {
    LOG_ERR("Illegal app index: %d\n", app_index_local);
    return -1;
  }
  app_index = app_index_local;
  /* Default flash address from compile-time table; overridden by aa0a */
  // meta_flash_address = flash_offsets[app_index];
  // app_flash_offset   = flash_offsets[app_index];
  LOG_INF("App index: %d  default flash: 0x%08X\n", app_index,
          meta_flash_address);
  return len;
}

static ssize_t get_total_length(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags) {
  if (len != 4) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memcpy(&meta_total_length, buf, 4);
  LOG_INF("Total length (info+data): %u bytes\n", meta_total_length);
  return len;
}

static ssize_t get_input_shape(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr, const void *buf,
                               uint16_t len, uint16_t offset, uint8_t flags) {
  if (len == 0 || len % 4 != 0 ||
      len > (uint16_t)(MAX_MODEL_INP_SHAPE_DIMS * sizeof(uint32_t))) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memset(meta_input_shape, 0, sizeof(meta_input_shape));
  memcpy(meta_input_shape, buf, len);
  LOG_INF("Input shape: %u dims  [%u %u %u]\n", len / 4, meta_input_shape[0],
          meta_input_shape[1], meta_input_shape[2]);
  return len;
}

static ssize_t get_output_shape(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags) {
  if (len == 0 || len % 4 != 0 ||
      len > (uint16_t)(MAX_MODEL_OUTP_SHAPE_DIMS * sizeof(uint32_t))) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memset(meta_output_shape, 0, sizeof(meta_output_shape));
  memcpy(meta_output_shape, buf, len);
  LOG_INF("Output shape: %u dims  [%u %u %u]\n", len / 4, meta_output_shape[0],
          meta_output_shape[1], meta_output_shape[2]);
  return len;
}

static ssize_t get_flash_address(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags) {
  if (len != 4) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memcpy(&meta_flash_address, buf, 4);
  LOG_INF("Flash address: 0x%08X\n", meta_flash_address);
  return len;
}

static ssize_t get_is_edge_learned(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags) {
  if (len != 4) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memcpy(&meta_is_edge_learned, buf, 4);
  LOG_INF("Is edge-learned: %u\n", meta_is_edge_learned);
  return len;
}

static ssize_t get_num_edge_classes(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    const void *buf, uint16_t len,
                                    uint16_t offset, uint8_t flags) {
  if (len != 4) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memcpy(&meta_num_edge_classes, buf, 4);
  LOG_INF("Num edge classes: neurons=%u classes=%u (packed=0x%08X)\n",
          (uint32_t)((meta_num_edge_classes >> 16) & 0xFFFF),
          (uint32_t)(meta_num_edge_classes & 0xFFFF), meta_num_edge_classes);
  return len;
}

static ssize_t get_fs_name(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr, const void *buf,
                           uint16_t len, uint16_t offset, uint8_t flags) {
  if (len == 0 || len >= MAX_FS_NAME_LEN) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memcpy(meta_fs_name, buf, len);
  meta_fs_name[len] = '\0';
  LOG_INF("FS name: %s\n", meta_fs_name);
  return len;
}

static ssize_t get_column_usage(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags) {
  if (len != 4) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  memcpy(&g_column_usage_mask, buf, 4);
  LOG_INF("Column usage mask: 0x%02X (col0=%s col1=%s col2=%s col3=%s)\n",
          g_column_usage_mask, (g_column_usage_mask & (1 << 0)) ? "ON" : "OFF",
          (g_column_usage_mask & (1 << 1)) ? "ON" : "OFF",
          (g_column_usage_mask & (1 << 2)) ? "ON" : "OFF",
          (g_column_usage_mask & (1 << 3)) ? "ON" : "OFF");
  return len;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
int file_transfer_init(void) {
  LOG_INF("File transfer service initialised\n");
  memset(&current_meta, 0, sizeof(current_meta));
  return 0;
}

static void reset_data_buffer(void) {
  ble_pgm_offset = 0;
  memset(sram_upload_buffer, 0, sizeof(sram_upload_buffer));
}

/* -------------------------------------------------------------------------
 * get_file_size – triggers flash erase (DATA) or just ACKs (INFO)
 * ---------------------------------------------------------------------- */
ssize_t get_file_size(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                      const void *buf, uint16_t len, uint16_t offset,
                      uint8_t flags) {
  memcpy((void *)&total_pgm_size, buf, len);
  total_received = 0;

  LOG_INF("File size = %u  transfer_type = 0x%02X\n", total_pgm_size,
          transfer_type);

  /* Build dynamic paths from meta_fs_name (set via FS_NAME_CHAR before this) */
  if (meta_fs_name[0] != '\0') {
    build_fs_paths_from_name(meta_fs_name);
  }

  if (transfer_type == TRANSFER_TYPE_INFO) {
    if (total_pgm_size == 0 || total_pgm_size > MAX_MODEL_INFO_SIZE) {
      LOG_ERR("Invalid info size %u\n", total_pgm_size);
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    sram_info_offset = 0;
    /* Clear sram_upload_buffer so info chunks accumulate from a clean state */
    memset(sram_upload_buffer, 0, MAX_MODEL_INFO_SIZE);
    send_ack_to_host(ACK_FLASH_ERASE_DONE);
    return len;
  }

  /* DATA path – erase SPI flash at the address received via aa0a */
  if (total_pgm_size == 0 ||
      total_pgm_size > (FLASH_MAX_16_MB_SIZE - meta_flash_address)) {
    LOG_ERR("Invalid data size %u\n", total_pgm_size);
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  if (spi_flash_erase_helper_func(meta_flash_address, total_pgm_size)) {
    LOG_ERR("Flash erase failed: addr=0x%08X size=%u\n", meta_flash_address,
            total_pgm_size);
    return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
  }
  /* --- Acquire SRAM upload buffer for model update ---
   *
   * Wait until the shared buffer becomes FREE before writing model data.
   *
   * Behavior:
   *  - If the camera is currently using the buffer (BUF_EVENT_BUSY),
   *    this call blocks until the camera releases it.
   *  - If the buffer is already FREE, execution continues immediately.
   *
   * After the FREE event is received:
   *  1. Clear the BUF_EVENT_FREE flag so other components know the buffer
   *     is no longer available.
   *  2. Post BUF_EVENT_BUSY to take ownership of the buffer.
   *
   * While BUF_EVENT_BUSY is set, the camera thread will block in
   * k_event_wait() and cannot access sram_upload_buffer.
   */
  k_event_wait(&sram_buf_event, BUF_EVENT_FREE, false, K_FOREVER);
  k_event_clear(&sram_buf_event, BUF_EVENT_FREE);
  k_event_post(&sram_buf_event, BUF_EVENT_BUSY);
  LOG_INF("sram_upload_buffer claimed by model update");

  /* Reset write cursor to the metadata flash address */
  app_flash_offset = meta_flash_address;
  /* Reset running CRC and first-4-bytes capture for this DATA transfer */
  data_crc_state = 0xFFFFFFFF;
  data_first_4_bytes = 0;

  send_ack_to_host(ACK_FLASH_ERASE_DONE);
  return len;
}

/* -------------------------------------------------------------------------
 * file_transfer_write – receives data chunks for both INFO and DATA
 * ---------------------------------------------------------------------- */
ssize_t file_transfer_write(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
  total_received += len;
  LOG_INF("Rx %u / %u bytes", (unsigned)total_received,
          (unsigned)total_pgm_size);
  led_set_state(LED_STATE_MODEL_RECEIVING);

  /* ------------------------------------------------------------------ */
  /* INFO path – stage raw program_info bytes into sram_upload_buffer   */
  /* ------------------------------------------------------------------ */
  if (transfer_type == TRANSFER_TYPE_INFO) {
    if (sram_info_offset + len > MAX_MODEL_INFO_SIZE) {
      LOG_ERR("Info buffer overflow\n");
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    /* Accumulate directly at the start of sram_upload_buffer */
    memcpy(&sram_upload_buffer[sram_info_offset], buf, len);
    sram_info_offset += len;

    if (total_received < total_pgm_size) {
      return len; /* more chunks expected */
    }

    /* All INFO chunks received – populate the in-RAM header */
    model_meta_t *m = &current_meta;
    m->total_length = meta_total_length;
    memcpy(m->input_shape, meta_input_shape, sizeof(meta_input_shape));
    memcpy(m->output_shape, meta_output_shape, sizeof(meta_output_shape));
    m->flash_address = meta_flash_address;
    m->is_edge_learned = meta_is_edge_learned;
    m->num_edge_classes = meta_num_edge_classes;
    m->info_data_len = (uint32_t)sram_info_offset;
    m->column_usage_mask = g_column_usage_mask;
    /* Extract and store model name from meta_fs_name (e.g. "kws" from
     * "/model_meta/kws") */
    memset(m->model_name, 0, MAX_FS_NAME_LEN);
    const char *nm = strrchr(meta_fs_name, '/');
    nm = (nm != NULL) ? nm + 1 : meta_fs_name;
    strncpy(m->model_name, nm, MAX_FS_NAME_LEN - 1);

    /* Compute model_info_hdr_crc32 = CRC32(header bytes
     * [total_length..info_data_len]
     * || program_info bytes).  Stored in the header and verified at transfer
     * time and at boot by file_transfer_load_meta(). */
    uint32_t crc_s = 0xFFFFFFFF;
    crc_s = crc32_ieee_update(crc_s, (const uint8_t *)&m->total_length,
                              sizeof(model_meta_t) -
                                  offsetof(model_meta_t, total_length));
    crc_s = crc32_ieee_update(crc_s, sram_upload_buffer, m->info_data_len);
    m->model_info_hdr_crc32 = crc_s ^ 0xFFFFFFFF;
    LOG_INF("Computed model_info_hdr_crc32: 0x%08X\n", m->model_info_hdr_crc32);

    /* Validate INFO CRC against value received from host via aa06 */
    if (expected_crc != 0 && m->model_info_hdr_crc32 != expected_crc) {
      led_set_state(LED_STATE_UPDATE_FAILED);
      LOG_ERR("INFO CRC FAIL: computed=0x%08X expected=0x%08X\n",
              m->model_info_hdr_crc32, expected_crc);
      total_received = 0;
      sram_info_offset = 0;
      memset(sram_upload_buffer, 0, BUFFER_SIZE);
      send_ack_to_host(ACK_CRC_FAIL);
      k_event_clear(&sram_buf_event, BUF_EVENT_BUSY);
      k_event_post(&sram_buf_event, BUF_EVENT_FREE);
      return len;
    }
    LOG_INF("INFO CRC OK (0x%08X)\n", m->model_info_hdr_crc32);

    /* Select paths: prefer dynamic (name-validated) paths, fall back to index
     */
    const char *hdr_path =
        (dyn_app_slot >= 0) ? dyn_hdr_path : meta_hdr_paths[0];
    const char *info_path =
        (dyn_app_slot >= 0) ? dyn_info_path : model_info_paths[0];

    /* --- File 1: write model_meta_t header --- */
    struct fs_file_t hdr_file;
    fs_file_t_init(&hdr_file);
    int rc =
        fs_open(&hdr_file, hdr_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc == 0) {
      fs_write(&hdr_file, m, sizeof(model_meta_t));
      fs_close(&hdr_file);
      LOG_INF("Header saved to %s (%u bytes)\n", hdr_path,
              (unsigned)sizeof(model_meta_t));
    } else {
      LOG_ERR("Failed to open header file '%s' (err %d)\n", hdr_path, rc);
    }

    /* --- File 2: write raw program_info from sram_upload_buffer --- */
    struct fs_file_t info_file;
    fs_file_t_init(&info_file);
    rc = fs_open(&info_file, info_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc == 0) {
      fs_write(&info_file, sram_upload_buffer, m->info_data_len);
      fs_close(&info_file);
      LOG_INF("Model info saved to %s (%u bytes)\n", info_path,
              m->info_data_len);
    } else {
      LOG_ERR("Failed to open info file '%s' (err %d)\n", info_path, rc);
    }

    total_received = 0;
    sram_info_offset = 0;
    /* Clear buffer so DATA transfer starts from a clean state */
    memset(sram_upload_buffer, 0, BUFFER_SIZE);
    send_ack_to_host(ACK_FLASH_WRITE_DONE);
    led_set_state(LED_STATE_UPDATE_SUCCESS);
    k_event_clear(&sram_buf_event, BUF_EVENT_BUSY);
    k_event_post(&sram_buf_event, BUF_EVENT_FREE);
    return len;
  }

  /* ------------------------------------------------------------------ */
  /* DATA path – buffer in SRAM, flush to SPI flash in BUFFER_SIZE chunks */
  /* ------------------------------------------------------------------ */
  /* Accumulate running CRC over each incoming chunk */
  data_crc_state = crc32_ieee_update(data_crc_state, (const uint8_t *)buf, len);

  /* Capture first 4 bytes of model_data on the very first chunk */
  if (total_received == (uint32_t)len && len >= 4) {
    memcpy(&data_first_4_bytes, buf, 4);
    LOG_INF("First 4 bytes of model_data: 0x%08X\n", data_first_4_bytes);
  }

  memcpy(&sram_upload_buffer[ble_pgm_offset], buf, len);
  ble_pgm_offset += len;

  bool buffer_full = (ble_pgm_offset == BUFFER_SIZE);
  bool last_chunk = (total_received == total_pgm_size);

  if (buffer_full || last_chunk) {
    led_set_state(LED_STATE_FLASH_WRITE);
    spi_flash_write_helper_func((uint8_t *)sram_upload_buffer, app_flash_offset,
                                ble_pgm_offset);
    app_flash_offset += ble_pgm_offset;
    reset_data_buffer();
    send_ack_to_host(ACK_FLASH_WRITE_DONE);

    if (last_chunk) {
      /* Validate DATA CRC against value received from host via aa06 */
      uint32_t data_crc = data_crc_state ^ 0xFFFFFFFF;
      LOG_INF("DATA CRC computed: 0x%08X  expected: 0x%08X\n", data_crc,
              expected_crc);
      if (expected_crc != 0 && data_crc != expected_crc) {
        led_set_state(LED_STATE_UPDATE_FAILED);
        LOG_ERR("DATA CRC FAIL: computed=0x%08X expected=0x%08X\n", data_crc,
                expected_crc);
        total_received = 0;
        app_flash_offset = meta_flash_address;
        ble_pgm_offset = 0;
        send_ack_to_host(ACK_CRC_FAIL);
        k_event_clear(&sram_buf_event, BUF_EVENT_BUSY);
        k_event_post(&sram_buf_event, BUF_EVENT_FREE);
        return len;
      }
      LOG_INF("DATA CRC OK (0x%08X)\n", data_crc);

      /* --- File 3: write model_data_meta_t to LittleFS --- */
      {
        model_data_meta_t dm;
        dm.data_crc32 = data_crc;
        dm.first_4_bytes = data_first_4_bytes;
        dm.data_length = (uint32_t)total_pgm_size;
        /* model_name is now stored in model_meta_t (file 1), not here */

        const char *data_path =
            (dyn_app_slot >= 0) ? dyn_data_path : model_data_meta_paths[0];
        struct fs_file_t dm_file;
        fs_file_t_init(&dm_file);
        int rc =
            fs_open(&dm_file, data_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
        if (rc == 0) {
          fs_write(&dm_file, &dm, sizeof(model_data_meta_t));
          fs_close(&dm_file);
          LOG_INF("Data meta saved: path=%s crc=0x%08X len=%u first4=0x%08X\n",
                  data_path, dm.data_crc32, dm.data_length, dm.first_4_bytes);
        } else {
          LOG_ERR("Failed to open data meta file '%s' (err %d)\n", data_path,
                  rc);
        }
      }

      /* Success: release buffer — unblocks */
      k_event_clear(&sram_buf_event, BUF_EVENT_BUSY);
      k_event_post(&sram_buf_event, BUF_EVENT_FREE);
      /* Trigger Akida programming + test inference */
      if (infer(app_index) != 0) {
        led_set_state(LED_STATE_UPDATE_FAILED);
        LOG_ERR("akida_program_infer failed\n");
        return 1;
      }
      total_received = 0;
      app_flash_offset = meta_flash_address;
      ble_pgm_offset = 0;
      led_set_state(LED_STATE_UPDATE_SUCCESS);
    }
  } else if (ble_pgm_offset > BUFFER_SIZE) {
    led_set_state(LED_STATE_UPDATE_FAILED);
    k_event_clear(&sram_buf_event, BUF_EVENT_BUSY);
    k_event_post(&sram_buf_event, BUF_EVENT_FREE);
    ble_pgm_offset = 0;
    LOG_ERR("Data exceeds BUFFER_SIZE (%d)\n", BUFFER_SIZE);
  }

  return len;
}

/* -------------------------------------------------------------------------
 * file_transfer_load_meta
 *
 * Reads two LittleFS files for the given app slot:
 *   1. meta_hdr_paths[app_idx]    – model_meta_t header struct
 *   2. model_info_paths[app_idx]  – raw program_info binary
 *
 * On success (return 0):
 *   - @p meta_out contains the validated header (flash_address, info_data_len,
 * …)
 *   - sram_upload_buffer[0..meta_out->info_data_len-1] holds the program_info
 *     binary ready to pass directly to akida_program_flash().
 *
 * Returns:
 *    0  both files read and info CRC verified
 *    1  header file not found – caller should use compiled defaults
 *   -1  read error or CRC mismatch – caller should use compiled defaults
 * ---------------------------------------------------------------------- */
int file_transfer_load_meta(int app_idx, model_meta_t *meta_out) {
  if (app_idx < 0 || app_idx > 0 || meta_out == NULL) {
    printk("E: incorrect app_idx %d \n\r", app_idx);
    return -1;
  }

  struct fs_file_t file;
  ssize_t bytes;

  /* ---- Step 1: read model_meta_t header ---- */
  fs_file_t_init(&file);
  int rc = fs_open(&file, meta_hdr_paths[app_idx], FS_O_READ);
  if (rc != 0) {
    LOG_INF("No header file for app %d ('%s', err %d) using defaults\n",
            app_idx, meta_hdr_paths[app_idx], rc);
    return 1;
  }

  bytes = fs_read(&file, meta_out, sizeof(model_meta_t));
  fs_close(&file);

  if (bytes != (ssize_t)sizeof(model_meta_t)) {
    LOG_ERR("Header read error: got %d, expected %u bytes\n", (int)bytes,
            (unsigned)sizeof(model_meta_t));
    return -1;
  }
  if (meta_out->info_data_len == 0 ||
      meta_out->info_data_len > MAX_MODEL_INFO_SIZE) {
    LOG_ERR("Invalid info_data_len: %u\n", meta_out->info_data_len);
    return -1;
  }

  /* ---- Step 2: read program_info binary into sram_upload_buffer ---- */
  fs_file_t_init(&file);
  rc = fs_open(&file, model_info_paths[app_idx], FS_O_READ);
  if (rc != 0) {
    LOG_ERR("No model info file for app %d ('%s', err %d)\n", app_idx,
            model_info_paths[app_idx], rc);
    return -1;
  }
  bytes = fs_read(&file, sram_upload_buffer, meta_out->info_data_len);
  fs_close(&file);

  if (bytes != (ssize_t)meta_out->info_data_len) {
    LOG_ERR("Model info read error: got %d, expected %u bytes\n", (int)bytes,
            meta_out->info_data_len);
    return -1;
  }

  /* ---- Step 3: verify model_info_hdr_crc32 ----
   *
   * model_info_hdr_crc32 = CRC32( struct_bytes[total_length..info_data_len]
   *                             || program_info_bytes_in_sram_upload_buffer )
   *
   * Both the header fields and the info bytes are already in memory,
   * so no SPI flash read is required here.
   */
  {
    uint32_t crc_state = 0xFFFFFFFF;
    crc_state = crc32_ieee_update(
        crc_state, (const uint8_t *)&meta_out->total_length,
        sizeof(model_meta_t) - offsetof(model_meta_t, total_length));
    crc_state = crc32_ieee_update(crc_state, sram_upload_buffer,
                                  meta_out->info_data_len);
    uint32_t computed = crc_state ^ 0xFFFFFFFF;
    if (computed != meta_out->model_info_hdr_crc32) {
      LOG_ERR("model_info_hdr CRC FAIL: computed=0x%08X stored=0x%08X\n",
              computed, meta_out->model_info_hdr_crc32);
      return -1;
    }
    LOG_INF("model_info_hdr CRC OK (0x%08X)\n", computed);
  }

  {
    uint16_t neurons = (uint16_t)((meta_out->num_edge_classes >> 16) & 0xFFFF);
    uint16_t classes = (uint16_t)(meta_out->num_edge_classes & 0xFFFF);
    LOG_INF("Meta loaded OK: app=%d flash=0x%08X info_len=%u el=%u "
            "neurons=%u classes=%u\n",
            app_idx, meta_out->flash_address, meta_out->info_data_len,
            meta_out->is_edge_learned, neurons, classes);
  }
  /* Caller: akida_program_flash(sram_upload_buffer,
   *                             meta_out->info_data_len,
   *                             meta_out->flash_address); */
  return 0;
}

/**
 * @brief Initialize the shared SRAM buffer event.
 *
 * This function initializes the event flags used for synchronizing access
 * to the shared SRAM buffer between camera and model update.
 *
 * The function performs the following steps:
 * - Clears any previously set (stale) event flags.
 * - Sets the initial state of the buffer as FREE.
 *
 * After initialization, the camera thread is allowed to access and use
 * the buffer until another module marks it as BUSY.
 */
void shared_buf_init(void) {
  /* Clear any stale bits */
  k_event_clear(&sram_buf_event, BUF_EVENT_FREE | BUF_EVENT_BUSY);

  /* Initial state: buffer is FREE — camera is allowed to proceed */
  k_event_post(&sram_buf_event, BUF_EVENT_FREE);

  printk("sram_buf: initialized, buffer is FREE\n");
}

/* -------------------------------------------------------------------------
 * file_transfer_read_meta_hdr_only
 *
 * Reads only the model_meta_t header struct from LittleFS without loading
 * program_info into sram_upload_buffer.  Used at boot before flash CRC
 * validation so the buffer is not clobbered before program_info is loaded.
 * ---------------------------------------------------------------------- */
int file_transfer_read_meta_hdr_only(int app_idx, model_meta_t *meta_out) {
  if (app_idx < 0 || app_idx > 0 || meta_out == NULL) {
    printk("E: incorrect app_idx %d \n\r", app_idx);
    return -1;
  }

  struct fs_file_t file;
  fs_file_t_init(&file);
  int rc = fs_open(&file, meta_hdr_paths[app_idx], FS_O_READ);
  if (rc != 0) {
    LOG_INF("E: No header file for app %d ('%s', err %d)\n", app_idx,
            meta_hdr_paths[app_idx], rc);
    return 1;
  }

  ssize_t bytes = fs_read(&file, meta_out, sizeof(model_meta_t));
  fs_close(&file);

  if (bytes != (ssize_t)sizeof(model_meta_t)) {
    LOG_ERR("E: Header read error: got %d, expected %u\n", (int)bytes,
            (unsigned)sizeof(model_meta_t));
    return -1;
  }
  return 0;
}

/* -------------------------------------------------------------------------
 * file_transfer_load_data_meta
 *
 * Reads the model_data_meta_t file (3rd LittleFS file) for the given slot.
 * ---------------------------------------------------------------------- */
int file_transfer_load_data_meta(int app_idx, model_data_meta_t *dm_out) {
  if (app_idx < 0 || app_idx > 0 || dm_out == NULL) {
    return -1;
  }

  struct fs_file_t file;
  fs_file_t_init(&file);
  int rc = fs_open(&file, model_data_meta_paths[app_idx], FS_O_READ);
  if (rc != 0) {
    LOG_INF("No data meta file for app %d ('%s', err %d)\n", app_idx,
            model_data_meta_paths[app_idx], rc);
    return 1;
  }

  ssize_t bytes = fs_read(&file, dm_out, sizeof(model_data_meta_t));
  fs_close(&file);

  if (bytes != (ssize_t)sizeof(model_data_meta_t)) {
    LOG_ERR("Data meta read error: got %d, expected %u\n", (int)bytes,
            (unsigned)sizeof(model_data_meta_t));
    return -1;
  }
  LOG_INF("Data meta loaded: crc=0x%08X len=%u first4=0x%08X\n",
          dm_out->data_crc32, dm_out->data_length, dm_out->first_4_bytes);
  return 0;
}

/* -------------------------------------------------------------------------
 * file_transfer_validate_flash_data
 *
 * Validates model data in SPI flash against the stored model_data_meta_t.
 * Reads dm->data_length bytes from flash in BUFFER_SIZE chunks using
 * sram_upload_buffer as scratch.
 *
 * WARNING: overwrites sram_upload_buffer — caller must reload program_info
 * via file_transfer_load_meta() before calling akida_program_flash().
 * ---------------------------------------------------------------------- */
int file_transfer_validate_flash_data(uint32_t flash_addr,
                                      const model_data_meta_t *dm) {
  if (dm == NULL || dm->data_length == 0) {
    LOG_ERR("Invalid data meta (NULL or zero length)\n");
    return -1;
  }

  /* Check 1: first 4 bytes */
  uint32_t flash_first4 = 0;
  spi_flash_read_helper_func((uint8_t *)&flash_first4, flash_addr, 4);
  if (flash_first4 != dm->first_4_bytes) {
    LOG_ERR("First-4-bytes MISMATCH: flash=0x%08X stored=0x%08X\n",
            flash_first4, dm->first_4_bytes);
    return -1;
  }
  LOG_INF("First 4 bytes OK (0x%08X)\n", flash_first4);

  /* Check 2: full CRC32 over data_length bytes from SPI flash */
  uint32_t crc_state = 0xFFFFFFFF;
  uint32_t remaining = dm->data_length;
  uint32_t offset = flash_addr;
  uint64_t t0 = k_uptime_get();

  while (remaining > 0) {
    uint32_t chunk =
        (remaining > (uint32_t)BUFFER_SIZE) ? (uint32_t)BUFFER_SIZE : remaining;
    spi_flash_read_helper_func(sram_upload_buffer, offset, chunk);
    crc_state = crc32_ieee_update(crc_state, sram_upload_buffer, chunk);
    offset += chunk;
    remaining -= chunk;
  }

  uint32_t computed_crc = crc_state ^ 0xFFFFFFFF;
  LOG_INF("Flash CRC validation took %lld ms\n",
          (long long)(k_uptime_get() - t0));

  if (computed_crc != dm->data_crc32) {
    LOG_ERR("Data CRC MISMATCH: computed=0x%08X stored=0x%08X\n", computed_crc,
            dm->data_crc32);
    return -1;
  }
  LOG_INF("Data CRC OK (0x%08X) over %u bytes\n", computed_crc,
          dm->data_length);
  return 0;
}

/* -------------------------------------------------------------------------
 * file_transfer_check_model_name
 *
 * Constructs "/ext/{model_name}_model_hdr" and checks it equals the
 * hardcoded path for app_idx.  Returns 0 if valid, -1 otherwise.
 * ---------------------------------------------------------------------- */
int file_transfer_check_model_name(int app_idx, const char *model_name) {
  if (app_idx < 0 || app_idx > 0 || model_name == NULL ||
      model_name[0] == '\0') {
    return -1;
  }
  char constructed[80];
  snprintf(constructed, sizeof(constructed), "/ext/%s_model_hdr", model_name);
  if (strcmp(constructed, meta_hdr_paths[app_idx]) == 0) {
    return 0;
  }
  LOG_ERR("Name check FAIL: constructed='%s' expected='%s'\n", constructed,
          meta_hdr_paths[app_idx]);
  return -1;
}
