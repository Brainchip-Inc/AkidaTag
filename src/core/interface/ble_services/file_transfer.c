#include "ble_services/file_transfer.h"
#include "akd_spi_flash_handler.h"
#include "ble_services/ble_initialization.h"

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
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(file_transfer, CONFIG_LOG_DEFAULT_LEVEL);

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static ssize_t get_app_index(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* app,
                             uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t control_write(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                             uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t data_write(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                          uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_file_crc(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                            uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_input_shape(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                               const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_output_shape(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_flash_address(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                 const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_total_length(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_is_edge_learned(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                   const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_num_edge_classes(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                    const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_fs_name(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                           uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_mfcc_fs(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                           uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_silence_class(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                 const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_unknown_class(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                 const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t get_inference_mode(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                  const void* buf, uint16_t len, uint16_t offset, uint8_t flags);

/* -------------------------------------------------------------------------
 * UUID definitions – must match the protocol specification and the host tools
 * ---------------------------------------------------------------------- */
#define BT_UUID_FILE_TRANSFER_SERVICE_VAL \
    BT_UUID_128_ENCODE(0xf000aa00, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_TRANSFER_DATA_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa01, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_TRANSFER_STATUS_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa02, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_TRANSFER_CTRL_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa03, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define APP_CHAR_UUID_VAL BT_UUID_128_ENCODE(0xf000aa05, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_FILE_CRC_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa06, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
/* Metadata characteristics (aa08–aa0e) */
#define BT_UUID_INPUT_SHAPE_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa08, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_OUTPUT_SHAPE_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa09, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_FLASH_ADDRESS_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa0a, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_TOTAL_LENGTH_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa0b, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_IS_EDGE_LEARNED_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa0c, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_NUM_EDGE_CLASSES_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa0d, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_FS_NAME_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa0e, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
/* Model-specific inference parameters (aa0f–aa12) */
#define BT_UUID_MFCC_FS_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa0f, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_SILENCE_CLASS_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa10, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_UNKNOWN_CLASS_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa11, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)
#define BT_UUID_INFERENCE_MODE_CHAR_VAL \
    BT_UUID_128_ENCODE(0xf000aa12, 0x0451, 0x4000, 0xb000, 0x000000000000ULL)

/* UUID struct instances */
static struct bt_uuid_128 file_transfer_service_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_SERVICE_VAL);
static struct bt_uuid_128 transfer_data_uuid = BT_UUID_INIT_128(BT_UUID_TRANSFER_DATA_CHAR_VAL);
static struct bt_uuid_128 transfer_status_uuid = BT_UUID_INIT_128(BT_UUID_TRANSFER_STATUS_CHAR_VAL);
static struct bt_uuid_128 transfer_ctrl_uuid = BT_UUID_INIT_128(BT_UUID_TRANSFER_CTRL_CHAR_VAL);
static struct bt_uuid_128 app_char_uuid_struct = BT_UUID_INIT_128(APP_CHAR_UUID_VAL);
static struct bt_uuid_128 file_crc_uuid = BT_UUID_INIT_128(BT_UUID_FILE_CRC_CHAR_VAL);
static struct bt_uuid_128 input_shape_uuid = BT_UUID_INIT_128(BT_UUID_INPUT_SHAPE_CHAR_VAL);
static struct bt_uuid_128 output_shape_uuid = BT_UUID_INIT_128(BT_UUID_OUTPUT_SHAPE_CHAR_VAL);
static struct bt_uuid_128 flash_address_uuid = BT_UUID_INIT_128(BT_UUID_FLASH_ADDRESS_CHAR_VAL);
static struct bt_uuid_128 total_length_uuid = BT_UUID_INIT_128(BT_UUID_TOTAL_LENGTH_CHAR_VAL);
static struct bt_uuid_128 is_edge_learned_uuid = BT_UUID_INIT_128(BT_UUID_IS_EDGE_LEARNED_CHAR_VAL);
static struct bt_uuid_128 num_edge_classes_uuid =
    BT_UUID_INIT_128(BT_UUID_NUM_EDGE_CLASSES_CHAR_VAL);
static struct bt_uuid_128 fs_name_uuid = BT_UUID_INIT_128(BT_UUID_FS_NAME_CHAR_VAL);
static struct bt_uuid_128 mfcc_fs_uuid = BT_UUID_INIT_128(BT_UUID_MFCC_FS_CHAR_VAL);
static struct bt_uuid_128 silence_class_uuid = BT_UUID_INIT_128(BT_UUID_SILENCE_CLASS_CHAR_VAL);
static struct bt_uuid_128 unknown_class_uuid = BT_UUID_INIT_128(BT_UUID_UNKNOWN_CLASS_CHAR_VAL);
static struct bt_uuid_128 inference_mode_uuid = BT_UUID_INIT_128(BT_UUID_INFERENCE_MODE_CHAR_VAL);

/* UUID pointer macros */
#define FILE_SVC_UUID (&file_transfer_service_uuid.uuid)
#define TRANSFER_DATA_UUID (&transfer_data_uuid.uuid)
#define TRANSFER_STATUS_UUID (&transfer_status_uuid.uuid)
#define TRANSFER_CTRL_UUID (&transfer_ctrl_uuid.uuid)
#define APP_CHAR_UUID_PTR (&app_char_uuid_struct.uuid)
#define FILE_CRC_UUID (&file_crc_uuid.uuid)
#define INPUT_SHAPE_UUID (&input_shape_uuid.uuid)
#define OUTPUT_SHAPE_UUID (&output_shape_uuid.uuid)
#define FLASH_ADDRESS_UUID (&flash_address_uuid.uuid)
#define TOTAL_LENGTH_UUID (&total_length_uuid.uuid)
#define IS_EDGE_LEARNED_UUID (&is_edge_learned_uuid.uuid)
#define NUM_EDGE_CLASSES_UUID (&num_edge_classes_uuid.uuid)
#define FS_NAME_UUID (&fs_name_uuid.uuid)
#define MFCC_FS_UUID (&mfcc_fs_uuid.uuid)
#define SILENCE_CLASS_UUID (&silence_class_uuid.uuid)
#define UNKNOWN_CLASS_UUID (&unknown_class_uuid.uuid)
#define INFERENCE_MODE_UUID (&inference_mode_uuid.uuid)

/* Permission shorthand */
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
#define WRITE_PERM BT_GATT_PERM_WRITE_ENCRYPT
#else
#define WRITE_PERM BT_GATT_PERM_WRITE
#endif

/* -------------------------------------------------------------------------
 * Wire protocol
 * ---------------------------------------------------------------------- */
#define TRANSFER_TYPE_INFO 0x00
#define TRANSFER_TYPE_DATA 0x01

#define CTRL_OP_START 0x01
#define CTRL_OP_ABORT 0x02
#define CTRL_START_LEN 6
#define CTRL_ABORT_LEN 1

#define STATUS_OK 0x00
#define STATUS_DONE 0x01
#define STATUS_ERR_OFFSET 0x02
#define STATUS_ERR_INTEGRITY 0x03
#define STATUS_ERR_FLASH 0x04
#define STATUS_ERR_STATE 0x05
#define STATUS_ERR_PARAM 0x06
#define STATUS_ABORTED 0x07
#define STATUS_READY 0x08
#define STATUS_ERR_PROGRAM 0x09

#define STATUS_FRAME_LEN 14

/* Every data write is a 32-bit absolute offset followed by at least one byte. */
#define DATA_WRITE_HEADER_LEN 4

/* -------------------------------------------------------------------------
 * Transfer state
 *
 * received is both the number of bytes accepted so far and the offset the next
 * data write must carry. staged is how many of those are still in
 * model_transfer_buffer waiting for their block to fill.
 * ---------------------------------------------------------------------- */
static struct {
    bool active;
    uint8_t type;
    uint32_t total_length;
    uint32_t received;
    uint32_t staged;
    uint32_t file_crc_state;
    uint32_t first_4_bytes;
} transfer;

/* -------------------------------------------------------------------------
 * Metadata received via BLE characteristics (before the INFO transfer)
 * ---------------------------------------------------------------------- */
static uint32_t expected_crc = 0; /* CRC the host declared on aa06 */
static uint32_t meta_total_length = 0;
static uint32_t meta_input_shape[MAX_MODEL_INP_SHAPE_DIMS] = {0};
static uint32_t meta_output_shape[MAX_MODEL_OUTP_SHAPE_DIMS] = {0};
static uint32_t meta_flash_address = 0;
static uint32_t meta_is_edge_learned = 0;
static uint32_t meta_num_edge_classes = 0;
static uint32_t meta_mfcc_fs_bits = 0;
static uint32_t meta_silence_class = 0;
static uint32_t meta_unknown_class = 0;
static uint32_t meta_inference_mode = 0;
static char meta_fs_name[MAX_FS_NAME_LEN] = {0};

extern int infer(int app_index_l);

/* In-RAM metadata, populated when an INFO transfer completes. */
static model_meta_t current_meta;

/* LittleFS file 1: model_meta_t header struct.
 * Index == app slot ( APP_SLOT_KWS=0 / APP_SLOT_FALL = 1).
 */
static const char* meta_hdr_paths[MAX_APP_SLOTS] = {
    "/ext/kws_model_hdr",
    "/ext/fall_model_hdr",
};

/* LittleFS file 2: raw program_info binary */
static const char* model_info_paths[MAX_APP_SLOTS] = {
    "/ext/kws_model_info",
    "/ext/fall_model_info",
};

/* LittleFS file 3: model_data_meta_t (CRC, length, first bytes) */
static const char* model_data_meta_paths[MAX_APP_SLOTS] = {
    "/ext/kws_model_data_hdr",
    "/ext/fall_model_data_hdr",
};

/* -------------------------------------------------------------------------
 * Dynamic path construction from meta_fs_name (set via FS_NAME_CHAR aa0e)
 * ---------------------------------------------------------------------- */
static char dyn_hdr_path[80];  /* e.g. "/ext/kws_model_hdr"      */
static char dyn_info_path[80]; /* e.g. "/ext/kws_model_info"     */
static char dyn_data_path[84]; /* e.g. "/ext/kws_model_data_hdr" */
static int dyn_app_slot = -1;  /* index into meta_hdr_paths[], -1 = unknown */

/**
 * @brief Derive the three LittleFS paths for a model name and check it is known.
 *
 * Takes the text after the final '/' so both "kws" and "/model_meta/kws" work.
 * Sets dyn_app_slot to the matching slot, or -1 when the name is not one this
 * firmware serves.
 */
static bool build_fs_paths_from_name(const char* fs_name) {
    const char* name = strrchr(fs_name, '/');
    name = (name != NULL) ? name + 1 : fs_name;
    if (name[0] == '\0') {
        LOG_ERR("Empty model name in fs_name '%s'\n", fs_name);
        dyn_app_slot = -1;
        return false;
    }

    snprintf(dyn_hdr_path, sizeof(dyn_hdr_path), "/ext/%s_model_hdr", name);
    snprintf(dyn_info_path, sizeof(dyn_info_path), "/ext/%s_model_info", name);
    snprintf(dyn_data_path, sizeof(dyn_data_path), "/ext/%s_model_data_hdr", name);

    dyn_app_slot = -1;
    for (size_t i = 0; i < ARRAY_SIZE(meta_hdr_paths); i++) {
        if (strcmp(dyn_hdr_path, meta_hdr_paths[i]) == 0) {
            dyn_app_slot = (int)i;
            LOG_INF("Model name '%s' → slot %d (hdr=%s)\n", name, dyn_app_slot, dyn_hdr_path);
            return true;
        }
    }
    LOG_ERR("Unknown model name '%s' (hdr='%s') not in whitelist\n", name, dyn_hdr_path);
    return false;
}

/* Not zeroed at boot: nothing reads it before a transfer or a load fills it. */
__noinit uint8_t model_transfer_buffer[MODEL_TRANSFER_BLOCK_SIZE];

/* -------------------------------------------------------------------------
 * Status notification
 * ---------------------------------------------------------------------- */
static bool notify_enabled = false;

static void status_ccc_cfg_changed(const struct bt_gatt_attr* attr, uint16_t value) {
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Transfer status notify %s\n", notify_enabled ? "enabled" : "disabled");
}

/* -------------------------------------------------------------------------
 * GATT service definition
 * ---------------------------------------------------------------------- */
BT_GATT_SERVICE_DEFINE(
    file_transfer_svc, BT_GATT_PRIMARY_SERVICE(FILE_SVC_UUID),

    /* aa01 – data chunks, each prefixed with its absolute offset */
    BT_GATT_CHARACTERISTIC(TRANSFER_DATA_UUID, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           WRITE_PERM, NULL, data_write, NULL),

    /* aa02 – status notify + CCCD */
    BT_GATT_CHARACTERISTIC(TRANSFER_STATUS_UUID, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL,
                           NULL),
    BT_GATT_CCC(status_ccc_cfg_changed,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT
#else
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
#endif
                ),

    /* aa03 – control: START and ABORT */
    BT_GATT_CHARACTERISTIC(TRANSFER_CTRL_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL, control_write,
                           NULL),

    /* aa05 – app index (0 = KWS) */
    BT_GATT_CHARACTERISTIC(APP_CHAR_UUID_PTR, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL, get_app_index,
                           NULL),

    /* aa06 – CRC32 of the transfer that is about to start */
    BT_GATT_CHARACTERISTIC(FILE_CRC_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL, get_file_crc, NULL),

    /* aa08 – model input shape (N × 32-bit dims) */
    BT_GATT_CHARACTERISTIC(INPUT_SHAPE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL, get_input_shape,
                           NULL),

    /* aa09 – model output shape (N × 32-bit dims) */
    BT_GATT_CHARACTERISTIC(OUTPUT_SHAPE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_output_shape, NULL),

    /* aa0a – flash address for model data (32-bit) */
    BT_GATT_CHARACTERISTIC(FLASH_ADDRESS_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_flash_address, NULL),

    /* aa0b – combined info+data total byte count (32-bit) */
    BT_GATT_CHARACTERISTIC(TOTAL_LENGTH_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_total_length, NULL),

    /* aa0c – is_edge_learned flag (32-bit, 1=EL model) */
    BT_GATT_CHARACTERISTIC(IS_EDGE_LEARNED_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_is_edge_learned, NULL),

    /* aa0d – number of edge-learning classes (32-bit) */
    BT_GATT_CHARACTERISTIC(NUM_EDGE_CLASSES_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_num_edge_classes, NULL),

    /* aa0e – model name (UTF-8) */
    BT_GATT_CHARACTERISTIC(FS_NAME_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL, get_fs_name, NULL),

    /* aa0f – MFCC normalisation scalar (IEEE-754 float bits, 32-bit) */
    BT_GATT_CHARACTERISTIC(MFCC_FS_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL, get_mfcc_fs, NULL),

    /* aa10 – silence class output index (32-bit) */
    BT_GATT_CHARACTERISTIC(SILENCE_CLASS_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_silence_class, NULL),

    /* aa11 – unknown/garbage class output index (32-bit) */
    BT_GATT_CHARACTERISTIC(UNKNOWN_CLASS_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_unknown_class, NULL),

    /* aa12 – inference mode flag (0=sync, 1=async; 32-bit) */
    BT_GATT_CHARACTERISTIC(INFERENCE_MODE_UUID, BT_GATT_CHRC_WRITE, WRITE_PERM, NULL,
                           get_inference_mode, NULL));

/**
 * @brief The attribute status notifications are sent on.
 *
 * Resolved on first use and cached, not at init: bt_gatt_find_by_uuid() works
 * off attribute handles, and those are only assigned once the Bluetooth stack
 * is enabled, which happens after file_transfer_init() runs.
 *
 * @return The status value attribute, or NULL if the service is not yet live.
 */
static const struct bt_gatt_attr* status_attribute(void) {
    static const struct bt_gatt_attr* cached;

    if (cached == NULL) {
        cached = bt_gatt_find_by_uuid(file_transfer_svc.attrs, file_transfer_svc.attr_count,
                                      TRANSFER_STATUS_UUID);
    }
    return cached;
}

/**
 * @brief Notify the host of a result and the position the device stands at.
 *
 * The frame is the 14-byte record the protocol specifies: result, transfer
 * type, block size, position, total length. position is the offset the next
 * data write must carry, which for STATUS_OK and STATUS_DONE is also everything
 * committed and read back.
 */
static void send_status(uint8_t result) {
    const struct bt_gatt_attr* attr = status_attribute();
    if (!notify_enabled || attr == NULL) {
        LOG_ERR("Status 0x%02X not sent: host has not subscribed\n", result);
        return;
    }

    uint8_t frame[STATUS_FRAME_LEN];
    frame[0] = result;
    frame[1] = transfer.type;
    sys_put_le32(MODEL_TRANSFER_BLOCK_SIZE, &frame[2]);
    sys_put_le32(transfer.received, &frame[6]);
    sys_put_le32(transfer.total_length, &frame[10]);

    int err = bt_gatt_notify(NULL, attr, frame, sizeof(frame));
    if (err) {
        LOG_ERR("Failed to notify status 0x%02X (err %d)\n", result, err);
    } else {
        LOG_INF("Status 0x%02X at %u/%u\n", result, transfer.received, transfer.total_length);
    }
}

/**
 * @brief Forget the transfer in progress without telling the host.
 *
 * Used where there is nobody to tell, and as the first thing START does so that
 * a new transfer never inherits anything from the last one.
 */
static void reset_transfer(void) {
    memset(&transfer, 0, sizeof(transfer));
}

/**
 * @brief End the transfer with a failure and report it.
 *
 * Reports before discarding so the status carries the position the transfer
 * reached, which is the only diagnostic the host gets.
 */
static void fail_transfer(uint8_t result) {
    led_set_state(LED_STATE_UPDATE_FAILED);
    send_status(result);
    reset_transfer();
}

/* -------------------------------------------------------------------------
 * Characteristic write handlers – metadata
 * ---------------------------------------------------------------------- */
static ssize_t get_file_crc(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&expected_crc, buf, 4);
    LOG_INF("Expected CRC32: 0x%08X\n", expected_crc);
    return len;
}

static ssize_t get_app_index(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* app,
                             uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t app_index_local = *(const uint8_t*)app;
    if (app_index_local >= ARRAY_SIZE(meta_hdr_paths)) {
        LOG_ERR("Illegal app index: %d\n", app_index_local);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    app_index = app_index_local;
    LOG_INF("App index: %d\n", app_index);
    return len;
}

static ssize_t get_total_length(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_total_length, buf, 4);
    LOG_INF("Total length (info+data): %u bytes\n", meta_total_length);
    return len;
}

static ssize_t get_input_shape(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                               const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len == 0 || len % 4 != 0 || len > (uint16_t)(MAX_MODEL_INP_SHAPE_DIMS * sizeof(uint32_t))) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memset(meta_input_shape, 0, sizeof(meta_input_shape));
    memcpy(meta_input_shape, buf, len);
    LOG_INF("Input shape: %u dims  [%u %u %u]\n", len / 4, meta_input_shape[0], meta_input_shape[1],
            meta_input_shape[2]);
    return len;
}

static ssize_t get_output_shape(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
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

static ssize_t get_flash_address(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                 const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_flash_address, buf, 4);
    LOG_INF("Flash address: 0x%08X\n", meta_flash_address);
    return len;
}

static ssize_t get_is_edge_learned(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                   const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_is_edge_learned, buf, 4);
    LOG_INF("Is edge-learned: %u\n", meta_is_edge_learned);
    return len;
}

static ssize_t get_num_edge_classes(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                    const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_num_edge_classes, buf, 4);
    LOG_INF("Num edge classes: neurons=%u classes=%u (packed=0x%08X)\n",
            (uint32_t)((meta_num_edge_classes >> 16) & 0xFFFF),
            (uint32_t)(meta_num_edge_classes & 0xFFFF), meta_num_edge_classes);
    return len;
}

static ssize_t get_mfcc_fs(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                           uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_mfcc_fs_bits, buf, 4);
    LOG_INF("MFCC fs bits: 0x%08X", meta_mfcc_fs_bits);
    return len;
}

static ssize_t get_silence_class(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                 const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_silence_class, buf, 4);
    LOG_INF("Silence class index: %u", meta_silence_class);
    return len;
}

static ssize_t get_unknown_class(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                 const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_unknown_class, buf, 4);
    LOG_INF("Unknown class index: %u", meta_unknown_class);
    return len;
}

static ssize_t get_inference_mode(struct bt_conn* conn, const struct bt_gatt_attr* attr,
                                  const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (len != 4) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&meta_inference_mode, buf, 4);
    LOG_INF("Inference mode: %u (%s)", meta_inference_mode, meta_inference_mode ? "async" : "sync");
    return len;
}

static ssize_t get_fs_name(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                           uint16_t len, uint16_t offset, uint8_t flags) {
    if (len == 0 || len >= MAX_FS_NAME_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(meta_fs_name, buf, len);
    meta_fs_name[len] = '\0';
    LOG_INF("FS name: %s\n", meta_fs_name);

    /* Reject an unservable name here rather than at START, so the host learns
     * which write was wrong. */
    if (!build_fs_paths_from_name(meta_fs_name)) {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    return len;
}

/* -------------------------------------------------------------------------
 * Transfer
 * ---------------------------------------------------------------------- */

/**
 * @brief CRC32 over one buffer in the convention both sides of this protocol use.
 *
 * The running state starts at all-ones and the result is inverted, which is not
 * the plain CRC32 of the bytes. It is what the host computes and what the
 * stored records hold, so it must not be "corrected".
 */
static uint32_t transfer_crc32(const uint8_t* bytes, uint32_t length) {
    return crc32_ieee_update(0xFFFFFFFF, bytes, length) ^ 0xFFFFFFFF;
}

/**
 * @brief Check the START parameters for a DATA transfer.
 * @return A STATUS_* code; STATUS_OK when the transfer may begin.
 */
static uint8_t validate_data_start(uint32_t total_length) {
    if (dyn_app_slot < 0 || meta_flash_address == 0) {
        LOG_ERR("DATA start before a model name and flash address were accepted\n");
        return STATUS_ERR_STATE;
    }
    if (meta_flash_address % MODEL_TRANSFER_BLOCK_SIZE != 0) {
        LOG_ERR("Flash address 0x%08X is not sector aligned\n", meta_flash_address);
        return STATUS_ERR_PARAM;
    }
    if (total_length > (FLASH_MAX_16_MB_SIZE - meta_flash_address)) {
        LOG_ERR("DATA length %u does not fit above 0x%08X\n", total_length, meta_flash_address);
        return STATUS_ERR_PARAM;
    }
    return STATUS_OK;
}

/**
 * @brief Begin a transfer, discarding anything already in progress.
 * @return A STATUS_* code to report back to the host.
 */
static uint8_t start_transfer(uint8_t type, uint32_t total_length) {
    reset_transfer();
    transfer.type = type;
    transfer.total_length = total_length;

    if (type != TRANSFER_TYPE_INFO && type != TRANSFER_TYPE_DATA) {
        LOG_ERR("Unknown transfer type 0x%02X\n", type);
        return STATUS_ERR_PARAM;
    }
    if (total_length == 0) {
        LOG_ERR("Zero-length transfer refused\n");
        return STATUS_ERR_PARAM;
    }
    if (dyn_app_slot < 0) {
        LOG_ERR("Transfer start before a known model name was accepted\n");
        return STATUS_ERR_PARAM;
    }

    if (type == TRANSFER_TYPE_INFO) {
        if (total_length > MAX_MODEL_INFO_SIZE) {
            LOG_ERR("INFO length %u exceeds %u\n", total_length, MAX_MODEL_INFO_SIZE);
            return STATUS_ERR_PARAM;
        }
    } else {
        uint8_t result = validate_data_start(total_length);
        if (result != STATUS_OK) {
            return result;
        }
        /* The first sector erase makes any stored record a lie about what is in
         * flash, so drop it now. An abandoned transfer then leaves "no model",
         * which the boot path handles, rather than a record that fails its CRC. */
        int rc = fs_unlink(dyn_data_path);
        if (rc != 0 && rc != -ENOENT) {
            LOG_ERR("Could not remove stale data meta '%s' (err %d)\n", dyn_data_path, rc);
            return STATUS_ERR_FLASH;
        }
    }

    transfer.active = true;
    transfer.file_crc_state = 0xFFFFFFFF;
    led_set_state(LED_STATE_MODEL_RECEIVING);
    LOG_INF("Transfer start: type=0x%02X length=%u\n", type, total_length);
    return STATUS_OK;
}

/**
 * @brief Commit one staged block to SPI flash and prove it arrived.
 *
 * Erases the sector, programs the staged bytes into it, then reads the sector
 * back over the staging buffer and compares CRCs. Clobbering the staging buffer
 * is deliberate: its contents are already in flash by then, and reusing it
 * saves a second sector-sized buffer.
 *
 * @param flash_offset  Absolute SPI flash address of the sector.
 * @param length        Staged bytes, at most one sector.
 * @return 0 on success, -EIO if the erase, the program or the read-back failed.
 */
static int commit_block_to_flash(uint32_t flash_offset, uint32_t length) {
    uint32_t expected = transfer_crc32(model_transfer_buffer, length);

    led_set_state(LED_STATE_FLASH_WRITE);
    if (spi_flash_erase_helper_func(flash_offset, MODEL_TRANSFER_BLOCK_SIZE) != 0) {
        LOG_ERR("Sector erase failed at 0x%08X\n", flash_offset);
        return -EIO;
    }
    spi_flash_write_helper_func(model_transfer_buffer, flash_offset, length);

    spi_flash_read_helper_func(model_transfer_buffer, flash_offset, length);
    uint32_t read_back = transfer_crc32(model_transfer_buffer, length);
    if (read_back != expected) {
        LOG_ERR("Block read-back MISMATCH at 0x%08X: wrote 0x%08X read 0x%08X\n", flash_offset,
                expected, read_back);
        return -EIO;
    }
    return 0;
}

/**
 * @brief Write one LittleFS file, truncating anything already there.
 * @return 0 on success, negative errno otherwise.
 */
static int write_fs_file(const char* path, const void* bytes, size_t length) {
    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc != 0) {
        LOG_ERR("Failed to open '%s' (err %d)\n", path, rc);
        return rc;
    }

    ssize_t written = fs_write(&file, bytes, length);
    fs_close(&file);

    if (written != (ssize_t)length) {
        LOG_ERR("Short write to '%s': %d of %u bytes\n", path, (int)written, (unsigned)length);
        return -EIO;
    }
    LOG_INF("Saved %s (%u bytes)\n", path, (unsigned)length);
    return 0;
}

/**
 * @brief Assemble the model_meta_t header from the metadata characteristics.
 *
 * The staged program_info bytes are part of the CRC, so this runs only once the
 * whole INFO transfer is in model_transfer_buffer.
 */
static void build_model_meta(model_meta_t* meta, uint32_t info_data_len) {
    meta->total_length = meta_total_length;
    memcpy(meta->input_shape, meta_input_shape, sizeof(meta_input_shape));
    memcpy(meta->output_shape, meta_output_shape, sizeof(meta_output_shape));
    meta->flash_address = meta_flash_address;
    meta->is_edge_learned = meta_is_edge_learned;
    meta->num_edge_classes = meta_num_edge_classes;
    meta->info_data_len = info_data_len;
    meta->mfcc_fs_bits = meta_mfcc_fs_bits;
    meta->silence_class = meta_silence_class;
    meta->unknown_class = meta_unknown_class;
    meta->inference_mode = meta_inference_mode;

    memset(meta->model_name, 0, MAX_FS_NAME_LEN);
    const char* name = strrchr(meta_fs_name, '/');
    name = (name != NULL) ? name + 1 : meta_fs_name;
    strncpy(meta->model_name, name, MAX_FS_NAME_LEN - 1);

    uint32_t crc_state = 0xFFFFFFFF;
    crc_state = crc32_ieee_update(crc_state, (const uint8_t*)&meta->total_length,
                                  sizeof(model_meta_t) - offsetof(model_meta_t, total_length));
    crc_state = crc32_ieee_update(crc_state, model_transfer_buffer, info_data_len);
    meta->model_info_hdr_crc32 = crc_state ^ 0xFFFFFFFF;
}

/**
 * @brief Finish an INFO transfer: check the header CRC, then store both files.
 * @return A STATUS_* code to report back to the host.
 */
static uint8_t finish_info_transfer(void) {
    model_meta_t* meta = &current_meta;
    build_model_meta(meta, transfer.received);

    if (meta->model_info_hdr_crc32 != expected_crc) {
        LOG_ERR("INFO CRC FAIL: computed=0x%08X expected=0x%08X\n", meta->model_info_hdr_crc32,
                expected_crc);
        return STATUS_ERR_INTEGRITY;
    }
    LOG_INF("INFO CRC OK (0x%08X)\n", meta->model_info_hdr_crc32);

    if (write_fs_file(dyn_hdr_path, meta, sizeof(model_meta_t)) != 0 ||
        write_fs_file(dyn_info_path, model_transfer_buffer, transfer.received) != 0) {
        return STATUS_ERR_FLASH;
    }
    return STATUS_DONE;
}

/**
 * @brief Finish a DATA transfer: check the whole file, then store its record.
 *
 * The CRC accumulated over the air says the right bytes arrived; the read-back
 * says the flash took them. Both must pass before the record is written, so a
 * record never describes a model that is not there.
 *
 * @return A STATUS_* code to report back to the host.
 */
static uint8_t finish_data_transfer(void) {
    uint32_t file_crc = transfer.file_crc_state ^ 0xFFFFFFFF;
    LOG_INF("DATA CRC computed: 0x%08X  expected: 0x%08X\n", file_crc, expected_crc);
    if (file_crc != expected_crc) {
        LOG_ERR("DATA CRC FAIL: computed=0x%08X expected=0x%08X\n", file_crc, expected_crc);
        return STATUS_ERR_INTEGRITY;
    }

    LOG_INF("DATA CRC OK (0x%08X)\n", file_crc);

    model_data_meta_t record = {
        .data_crc32 = file_crc,
        .first_4_bytes = transfer.first_4_bytes,
        .data_length = transfer.total_length,
    };

    if (file_transfer_validate_flash_data(meta_flash_address, &record) != 0) {
        LOG_ERR("Flash readback FAILED at 0x%08X; data meta NOT saved\n", meta_flash_address);
        return STATUS_ERR_INTEGRITY;
    }
    if (write_fs_file(dyn_data_path, &record, sizeof(record)) != 0) {
        return STATUS_ERR_FLASH;
    }
    LOG_INF("Data meta saved: path=%s crc=0x%08X len=%u first4=0x%08X\n", dyn_data_path,
            record.data_crc32, record.data_length, record.first_4_bytes);
    return STATUS_DONE;
}

/**
 * @brief Commit the staged block, wherever this transfer's bytes belong.
 * @return A STATUS_* code: STATUS_OK mid-file, STATUS_DONE on the last block.
 */
static uint8_t commit_staged_block(void) {
    /* An INFO transfer never exceeds one block, so its LittleFS write is both
     * the block commit and the end of the transfer. */
    if (transfer.type == TRANSFER_TYPE_INFO) {
        return finish_info_transfer();
    }

    uint32_t block_start = transfer.received - transfer.staged;
    if (commit_block_to_flash(meta_flash_address + block_start, transfer.staged) != 0) {
        return STATUS_ERR_FLASH;
    }
    if (transfer.received < transfer.total_length) {
        return STATUS_OK;
    }
    return finish_data_transfer();
}

/**
 * @brief Program the AKD1500 with the model just stored and report the outcome.
 *
 * Runs after the host has been told the transfer is DONE, because it takes
 * seconds and is a separate thing from the file being safely stored. The record
 * is left in place on failure so a transient one fixes itself at the next boot.
 */
static void install_transferred_model(void) {
    if (infer(app_index) != 0) {
        LOG_ERR("Model stored but the AKD1500 would not run it\n");
        led_set_state(LED_STATE_UPDATE_FAILED);
        send_status(STATUS_ERR_PROGRAM);
        return;
    }
    led_set_state(LED_STATE_UPDATE_SUCCESS);
    send_status(STATUS_READY);
}

static ssize_t control_write(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                             uint16_t len, uint16_t offset, uint8_t flags) {
    const uint8_t* frame = buf;

    if (len < 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    switch (frame[0]) {
        case CTRL_OP_START: {
            if (len != CTRL_START_LEN) {
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
            }
            if (!notify_enabled) {
                LOG_ERR("START refused: host has not subscribed to the status\n");
                return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);
            }
            uint8_t result = start_transfer(frame[1], sys_get_le32(&frame[2]));
            if (result == STATUS_OK) {
                send_status(STATUS_OK);
            } else {
                fail_transfer(result);
            }
            return len;
        }
        case CTRL_OP_ABORT:
            if (len != CTRL_ABORT_LEN) {
                return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
            }
            LOG_INF("Transfer aborted by the host at %u/%u\n", transfer.received,
                    transfer.total_length);
            transfer.received = 0;
            send_status(STATUS_ABORTED);
            reset_transfer();
            return len;
        default:
            LOG_ERR("Unknown control opcode 0x%02X\n", frame[0]);
            return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
}

/**
 * @brief Decide whether a data write belongs where it says it does.
 * @return A STATUS_* code; STATUS_OK when the payload may be staged.
 */
static uint8_t check_data_write(uint32_t write_offset, uint32_t payload_len) {
    if (!transfer.active) {
        LOG_ERR("Data write with no transfer in progress\n");
        return STATUS_ERR_STATE;
    }
    if (write_offset != transfer.received) {
        LOG_ERR("Offset mismatch: host sent %u, expected %u\n", write_offset, transfer.received);
        return STATUS_ERR_OFFSET;
    }

    uint32_t to_block_end = MODEL_TRANSFER_BLOCK_SIZE - transfer.staged;
    uint32_t to_file_end = transfer.total_length - transfer.received;
    if (payload_len > MIN(to_block_end, to_file_end)) {
        LOG_ERR("Write of %u bytes at %u crosses a boundary\n", payload_len, write_offset);
        return STATUS_ERR_PARAM;
    }
    return STATUS_OK;
}

/*
 * Data writes never fail at ATT level, so a Write Command and a Write Request
 * behave identically and the host has one place to look for every outcome.
 */
static ssize_t data_write(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf,
                          uint16_t len, uint16_t offset, uint8_t flags) {
    if (len <= DATA_WRITE_HEADER_LEN) {
        LOG_ERR("Data write of %u bytes carries no payload\n", len);
        fail_transfer(STATUS_ERR_PARAM);
        return len;
    }

    const uint8_t* frame = buf;
    uint32_t write_offset = sys_get_le32(frame);
    const uint8_t* payload = frame + DATA_WRITE_HEADER_LEN;
    uint32_t payload_len = len - DATA_WRITE_HEADER_LEN;

    uint8_t result = check_data_write(write_offset, payload_len);
    if (result != STATUS_OK) {
        fail_transfer(result);
        return len;
    }

    if (transfer.received == 0 && payload_len >= 4) {
        transfer.first_4_bytes = sys_get_le32(payload);
    }
    transfer.file_crc_state = crc32_ieee_update(transfer.file_crc_state, payload, payload_len);

    memcpy(&model_transfer_buffer[transfer.staged], payload, payload_len);
    transfer.staged += payload_len;
    transfer.received += payload_len;

    bool block_full = (transfer.staged == MODEL_TRANSFER_BLOCK_SIZE);
    bool file_complete = (transfer.received == transfer.total_length);
    if (!block_full && !file_complete) {
        return len;
    }

    result = commit_staged_block();
    transfer.staged = 0;

    if (result == STATUS_OK) {
        send_status(STATUS_OK);
        return len;
    }
    if (result != STATUS_DONE) {
        fail_transfer(result);
        return len;
    }

    /* The file is stored and verified. Say so before installing it, which takes
     * seconds: the host shows honest progress and learns the outcome separately. */
    transfer.active = false;
    send_status(STATUS_DONE);
    if (transfer.type == TRANSFER_TYPE_DATA) {
        install_transferred_model();
    } else {
        led_set_state(LED_STATE_UPDATE_SUCCESS);
    }
    return len;
}

/**
 * @brief Abandon a transfer whose host has gone away.
 *
 * Without this the next connection would inherit a half-filled staging buffer
 * and a position nobody agrees on.
 */
static void on_disconnected(struct bt_conn* conn, uint8_t reason) {
    if (transfer.active) {
        LOG_WRN("Link dropped at %u/%u, transfer abandoned\n", transfer.received,
                transfer.total_length);
        led_set_state(LED_STATE_UPDATE_FAILED);
        reset_transfer();
    }
}

BT_CONN_CB_DEFINE(file_transfer_conn_callbacks) = {
    .disconnected = on_disconnected,
};

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
int file_transfer_init(void) {
    memset(&current_meta, 0, sizeof(current_meta));
    reset_transfer();
    LOG_INF("File transfer service initialised, block size %u\n", MODEL_TRANSFER_BLOCK_SIZE);
    return 0;
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
 *   - model_transfer_buffer[0..meta_out->info_data_len-1] holds the program_info
 *     binary ready to pass directly to akida_program_flash().
 *
 * Returns:
 *    0  both files read and info CRC verified
 *    1  header file not found – caller should use compiled defaults
 *   -1  read error or CRC mismatch – caller should use compiled defaults
 * ---------------------------------------------------------------------- */
int file_transfer_load_meta(int app_idx, model_meta_t* meta_out) {
    if (app_idx < 0 || app_idx >= MAX_APP_SLOTS || meta_out == NULL) {
        LOG_ERR("incorrect app_idx %d", app_idx);
        return -1;
    }

    struct fs_file_t file;
    ssize_t bytes;

    /* ---- Step 1: read model_meta_t header ---- */
    fs_file_t_init(&file);
    int rc = fs_open(&file, meta_hdr_paths[app_idx], FS_O_READ);
    if (rc != 0) {
        LOG_INF("No header file for app %d ('%s', err %d) using defaults\n", app_idx,
                meta_hdr_paths[app_idx], rc);
        return 1;
    }

    bytes = fs_read(&file, meta_out, sizeof(model_meta_t));
    fs_close(&file);

    if (bytes != (ssize_t)sizeof(model_meta_t)) {
        LOG_ERR("Header read error: got %d, expected %u bytes\n", (int)bytes,
                (unsigned)sizeof(model_meta_t));
        return -1;
    }
    if (meta_out->info_data_len == 0 || meta_out->info_data_len > MAX_MODEL_INFO_SIZE) {
        LOG_ERR("Invalid info_data_len: %u\n", meta_out->info_data_len);
        return -1;
    }

    /* ---- Step 2: read program_info binary into model_transfer_buffer ---- */
    fs_file_t_init(&file);
    rc = fs_open(&file, model_info_paths[app_idx], FS_O_READ);
    if (rc != 0) {
        LOG_ERR("No model info file for app %d ('%s', err %d)\n", app_idx,
                model_info_paths[app_idx], rc);
        return -1;
    }
    bytes = fs_read(&file, model_transfer_buffer, meta_out->info_data_len);
    fs_close(&file);

    if (bytes != (ssize_t)meta_out->info_data_len) {
        LOG_ERR("Model info read error: got %d, expected %u bytes\n", (int)bytes,
                meta_out->info_data_len);
        return -1;
    }

    /* ---- Step 3: verify model_info_hdr_crc32 ----
     *
     * model_info_hdr_crc32 = CRC32( struct_bytes[total_length..info_data_len]
     *                             || program_info_bytes_in_model_transfer_buffer )
     *
     * Both the header fields and the info bytes are already in memory,
     * so no SPI flash read is required here.
     */
    {
        uint32_t crc_state = 0xFFFFFFFF;
        crc_state = crc32_ieee_update(crc_state, (const uint8_t*)&meta_out->total_length,
                                      sizeof(model_meta_t) - offsetof(model_meta_t, total_length));
        crc_state = crc32_ieee_update(crc_state, model_transfer_buffer, meta_out->info_data_len);
        uint32_t computed = crc_state ^ 0xFFFFFFFF;
        if (computed != meta_out->model_info_hdr_crc32) {
            LOG_ERR("model_info_hdr CRC FAIL: computed=0x%08X stored=0x%08X\n", computed,
                    meta_out->model_info_hdr_crc32);
            return -1;
        }
        LOG_INF("model_info_hdr CRC OK (0x%08X)\n", computed);
    }

    {
        uint16_t neurons = (uint16_t)((meta_out->num_edge_classes >> 16) & 0xFFFF);
        uint16_t classes = (uint16_t)(meta_out->num_edge_classes & 0xFFFF);
        LOG_INF(
            "Meta loaded OK: app=%d flash=0x%08X info_len=%u el=%u "
            "neurons=%u classes=%u\n",
            app_idx, meta_out->flash_address, meta_out->info_data_len, meta_out->is_edge_learned,
            neurons, classes);
    }
    /* Caller: akida_program_flash(model_transfer_buffer,
     *                             meta_out->info_data_len,
     *                             meta_out->flash_address); */
    return 0;
}

/* -------------------------------------------------------------------------
 * file_transfer_read_meta_hdr_only
 *
 * Reads only the model_meta_t header struct from LittleFS without loading
 * program_info into model_transfer_buffer.  Used at boot before flash CRC
 * validation so the buffer is not clobbered before program_info is loaded.
 * ---------------------------------------------------------------------- */
int file_transfer_read_meta_hdr_only(int app_idx, model_meta_t* meta_out) {
    if (app_idx < 0 || app_idx >= MAX_APP_SLOTS || meta_out == NULL) {
        LOG_ERR("incorrect app_idx %d", app_idx);
        return -1;
    }

    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, meta_hdr_paths[app_idx], FS_O_READ);
    if (rc != 0) {
        LOG_INF("E: No header file for app %d ('%s', err %d)\n", app_idx, meta_hdr_paths[app_idx],
                rc);
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
int file_transfer_load_data_meta(int app_idx, model_data_meta_t* dm_out) {
    if (app_idx < 0 || app_idx >= MAX_APP_SLOTS || dm_out == NULL) {
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
    LOG_INF("Data meta loaded: crc=0x%08X len=%u first4=0x%08X\n", dm_out->data_crc32,
            dm_out->data_length, dm_out->first_4_bytes);
    return 0;
}

/* -------------------------------------------------------------------------
 * file_transfer_validate_flash_data
 *
 * Validates model data in SPI flash against the stored model_data_meta_t.
 * Reads dm->data_length bytes from flash in MODEL_TRANSFER_BLOCK_SIZE chunks
 * using model_transfer_buffer as scratch.
 *
 * WARNING: overwrites model_transfer_buffer — caller must reload program_info
 * via file_transfer_load_meta() before calling akida_program_flash().
 * ---------------------------------------------------------------------- */
int file_transfer_validate_flash_data(uint32_t flash_addr, const model_data_meta_t* dm) {
    if (dm == NULL || dm->data_length == 0) {
        LOG_ERR("Invalid data meta (NULL or zero length)\n");
        return -1;
    }

    /* Check 1: first 4 bytes */
    uint32_t flash_first4 = 0;
    spi_flash_read_helper_func((uint8_t*)&flash_first4, flash_addr, 4);
    if (flash_first4 != dm->first_4_bytes) {
        LOG_ERR("First-4-bytes MISMATCH: flash=0x%08X stored=0x%08X\n", flash_first4,
                dm->first_4_bytes);
        return -1;
    }
    LOG_INF("First 4 bytes OK (0x%08X)\n", flash_first4);

    /* Check 2: full CRC32 over data_length bytes from SPI flash */
    uint32_t crc_state = 0xFFFFFFFF;
    uint32_t remaining = dm->data_length;
    uint32_t offset = flash_addr;
    uint64_t t0 = k_uptime_get();

    while (remaining > 0) {
        uint32_t chunk = MIN(remaining, MODEL_TRANSFER_BLOCK_SIZE);
        spi_flash_read_helper_func(model_transfer_buffer, offset, chunk);
        crc_state = crc32_ieee_update(crc_state, model_transfer_buffer, chunk);
        offset += chunk;
        remaining -= chunk;
    }

    uint32_t computed_crc = crc_state ^ 0xFFFFFFFF;
    LOG_INF("Flash CRC validation took %lld ms\n", (long long)(k_uptime_get() - t0));

    if (computed_crc != dm->data_crc32) {
        LOG_ERR("Data CRC MISMATCH: computed=0x%08X stored=0x%08X\n", computed_crc, dm->data_crc32);
        return -1;
    }
    LOG_INF("Data CRC OK (0x%08X) over %u bytes\n", computed_crc, dm->data_length);
    return 0;
}

/* -------------------------------------------------------------------------
 * file_transfer_check_model_name
 *
 * Constructs "/ext/{model_name}_model_hdr" and checks it equals the
 * hardcoded path for app_idx.  Returns 0 if valid, -1 otherwise.
 * ---------------------------------------------------------------------- */
int file_transfer_check_model_name(int app_idx, const char* model_name) {
    if (app_idx < 0 || app_idx >= MAX_APP_SLOTS || model_name == NULL || model_name[0] == '\0') {
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
