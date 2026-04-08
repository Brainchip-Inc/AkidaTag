#include "ble_services/edge_learning.h"
#include "ble_services/ble_initialization.h"

static void edge_ack_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                     uint16_t value);
static bool notify_enabled = false;
#define BT_UUID_EDGE_SERVICE_VAL                                               \
  BT_UUID_128_ENCODE(0xf000bb11, 0x0111, 0x9000, 0xc000, 0x000000000000)

#define BT_UUID_EDGE_CMD_VAL                                                   \
  BT_UUID_128_ENCODE(0xf000bb10, 0x0111, 0x9000, 0xc000, 0x000000000000)

#define BT_UUID_EDGE_ACK_VAL                                                   \
  BT_UUID_128_ENCODE(0xf000bb12, 0x0111, 0x9000, 0xc000, 0x000000000000)

/**
 * @brief BLE UUID structure for Edge Learning Service
 */
static struct bt_uuid_128 edge_service_uuid =
    BT_UUID_INIT_128(BT_UUID_EDGE_SERVICE_VAL);

/**
 * @brief BLE UUID structure for Edge Learning Command Characteristic
 */
static struct bt_uuid_128 edge_cmd_uuid =
    BT_UUID_INIT_128(BT_UUID_EDGE_CMD_VAL);

/**
 * @brief BLE UUID structure for Edge Learning ACK Characteristic
 */
static struct bt_uuid_128 edge_ack_uuid =
    BT_UUID_INIT_128(BT_UUID_EDGE_ACK_VAL);

/**
 * @brief Edge Learning BLE GATT Service
 *
 * This service exposes two characteristics:
 *
 * 1. Command Characteristic
 *      - Property: Write
 *      - Used by mobile application to send commands
 *
 * 2. ACK Characteristic
 *      - Property: Notify
 *      - Used by firmware to send acknowledgements
 *
 * Example commands:
 * 0 → Enter inference mode
 * 1 → Start learning
 * 2 → Delete trained class
 * 3 → Select next class index
 */
BT_GATT_SERVICE_DEFINE(
    edge_service, BT_GATT_PRIMARY_SERVICE(&edge_service_uuid),

    /* Command Write Characteristic */
    BT_GATT_CHARACTERISTIC(&edge_cmd_uuid.uuid, BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE, NULL, edge_cmd_write, NULL),

    /* ACK Notify Characteristic */
    BT_GATT_CHARACTERISTIC(&edge_ack_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),

    BT_GATT_CCC(edge_ack_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/**
 * @brief Handles enable/disable of ACK notifications.
 *
 * When the central subscribes to notifications, this callback
 * updates the internal flag so that the firmware can send ACK
 * notifications safely.
 */
static void edge_ack_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                     uint16_t value) {
  notify_enabled = (value == BT_GATT_CCC_NOTIFY);

  if (notify_enabled) {
    printk("ACK notifications enabled");
  } else {
    printk("ACK notifications disabled");
  }
}

/**
 * @brief Triggered when the edge learning process is completed.
 *
 * This function sends an acknowledgement to the BLE central
 * indicating that the training process has finished.
 */
void learning_completed() { send_ack(ACK_LEARNING_DONE, CMD_DEPLOY_START); }

/**
 * @brief Handles commands written by the BLE central device.
 *
 * This function is triggered when the mobile application
 * writes to the Edge Command characteristic.
 *
 * The received command is forwarded to the edge learning.
 *
 * @param conn   BLE connection object.
 * @param attr   GATT attribute.
 * @param buf    Data buffer containing the command.
 * @param len    Length of the received data.
 * @param offset Write offset.
 * @param flags  Write flags.
 *
 * @return Number of bytes processed.
 */
static ssize_t edge_cmd_write(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr, const void *buf,
                              uint16_t len, uint16_t offset, uint8_t flags) {
  uint8_t cmd = ((uint8_t *)buf)[0];

  printk("Edge learning cmd: %d\n", cmd);

  edge_learning_cmd_process(cmd);

  return len;
}