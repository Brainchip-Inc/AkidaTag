#include "ble_services/imu_ble.h"
#include "imu_h/imu.h"

LOG_MODULE_REGISTER(imu_data, LOG_LEVEL_DBG);

/* -------------------------------------------------------------------------- */
/* UUIDs                                                                      */
/* -------------------------------------------------------------------------- */

#define BT_UUID_IMU_SERVICE_VAL                                                \
  BT_UUID_128_ENCODE(0x8A7E0001, 0x6B2A, 0x4F91, 0xA3C4, 0x1256789ABCDE)

#define BT_UUID_IMU_CMD_VAL                                                    \
  BT_UUID_128_ENCODE(0x8A7E0002, 0x6B2A, 0x4F91, 0xA3C4, 0x1256789ABCDE)

#define BT_UUID_IMU_DATA_VAL                                                   \
  BT_UUID_128_ENCODE(0x8A7E0003, 0x6B2A, 0x4F91, 0xA3C4, 0x1256789ABCDE)

/* -------------------------------------------------------------------------- */
/* UUID structures                                                            */
/* -------------------------------------------------------------------------- */

static struct bt_uuid_128 imu_service_uuid =
    BT_UUID_INIT_128(BT_UUID_IMU_SERVICE_VAL);

static struct bt_uuid_128 imu_cmd_uuid = BT_UUID_INIT_128(BT_UUID_IMU_CMD_VAL);

static struct bt_uuid_128 imu_data_uuid =
    BT_UUID_INIT_128(BT_UUID_IMU_DATA_VAL);

/* -------------------------------------------------------------------------- */
/* IMU commands                                                               */
/* -------------------------------------------------------------------------- */

#define IMU_CMD_START 0x01
#define IMU_CMD_STOP 0x02

/* -------------------------------------------------------------------------- */
/* Internal state                                                             */
/* -------------------------------------------------------------------------- */

static bool imu_notify_enabled = false;
static bool imu_streaming = false;

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                       */
/* -------------------------------------------------------------------------- */

static void imu_data_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                     uint16_t value);

static ssize_t imu_cmd_write(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags);
int imu_send_calibration();

/* -------------------------------------------------------------------------- */
/* GATT service                                                               */
/* -------------------------------------------------------------------------- */

BT_GATT_SERVICE_DEFINE(
    imu_service,

    /* Primary IMU Service */
    BT_GATT_PRIMARY_SERVICE(&imu_service_uuid),

    /* ---------------------------------------------------------------------- */
    /* IMU Command Characteristic                                             */
    /* ---------------------------------------------------------------------- */

    BT_GATT_CHARACTERISTIC(&imu_cmd_uuid.uuid, BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE, NULL, imu_cmd_write, NULL),

    /* ---------------------------------------------------------------------- */
    /* IMU Data Characteristic                                                */
    /* ---------------------------------------------------------------------- */

    BT_GATT_CHARACTERISTIC(&imu_data_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),

    /* Client Characteristic Configuration */
    BT_GATT_CCC(imu_data_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* -------------------------------------------------------------------------- */
/* Notification enable / disable                                             */
/* -------------------------------------------------------------------------- */

static void imu_data_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                     uint16_t value) {
  imu_notify_enabled = (value == BT_GATT_CCC_NOTIFY);

  if (imu_notify_enabled) {

    LOG_INF("IMU notifications enabled");

  } else {

    LOG_INF("IMU notifications disabled");

    /*
     * Stop streaming if the central unsubscribes.
     */
    imu_streaming = false;
  }
}

/* -------------------------------------------------------------------------- */
/* IMU command write callback                                                 */
/* -------------------------------------------------------------------------- */

static ssize_t imu_cmd_write(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags) {
  if (len < 1) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }

  const uint8_t *data = buf;

  uint8_t cmd = data[0];

  LOG_INF("IMU command received: 0x%02X", cmd);

  switch (cmd) {

  case IMU_CMD_START:

    if (!imu_notify_enabled) {

      LOG_WRN("Cannot start IMU logging: "
              "notifications not enabled");

      return len;
    }

    if (imu_streaming) {

      LOG_WRN("IMU logging already running");

      return len;
    }

    LOG_INF("Starting IMU logging");

    imu_streaming = true;
    imu_send_calibration();

    break;

  case IMU_CMD_STOP:

    LOG_INF("Stopping IMU logging");

    imu_streaming = false;

    break;

  default:

    LOG_WRN("Unknown IMU command: 0x%02X", cmd);

    break;
  }

  return len;
}

/* -------------------------------------------------------------------------- */
/* Send calibration data                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Send IMU calibration offsets to BLE central.
 *
 * This packet should be sent only once after calibration.
 *
 * @param acc_offset  Accelerometer offsets [X,Y,Z]
 * @param gyro_offset Gyroscope offsets [X,Y,Z]
 *
 * @return 0 on success, negative error code on failure.
 */
int imu_send_calibration(void) {
  struct imu_calibration_packet packet;

  /* Check whether BLE notification is enabled */
  if (!imu_notify_enabled) {
    LOG_WRN("IMU notification not enabled");
    return -ENOTCONN;
  }

  /* Copy global calibration offsets */
  packet.acc_offset_x = acc_offset[0];
  packet.acc_offset_y = acc_offset[1];
  packet.acc_offset_z = acc_offset[2];

  packet.gyro_offset_x = gyro_offset[0];
  packet.gyro_offset_y = gyro_offset[1];
  packet.gyro_offset_z = gyro_offset[2];

  /* Send calibration packet */
  int err =
      bt_gatt_notify(NULL, &imu_service.attrs[3], &packet, sizeof(packet));

  if (err) {
    LOG_ERR("Failed to send IMU calibration (err %d)", err);
  } else {
    LOG_INF("IMU calibration sent");
  }

  return err;
}

/* -------------------------------------------------------------------------- */
/* Send raw IMU data                                                          */
/* -------------------------------------------------------------------------- */

int imu_send_data(int32_t acc_x, int32_t acc_y, int32_t acc_z, int32_t gyr_x,
                  int32_t gyr_y, int32_t gyr_z) {
  struct imu_ble_packet packet;

  if (!imu_notify_enabled) {
    return -ENOTCONN;
  }

  if (!imu_streaming) {
    return -EACCES;
  }

  /*
   * Timestamp in milliseconds.
   */
  packet.timestamp = k_uptime_get_32();

  /*
   * Calibrated raw accelerometer data.
   */
  packet.acc_x = (int16_t)acc_x;
  packet.acc_y = (int16_t)acc_y;
  packet.acc_z = (int16_t)acc_z;

  /*
   * Calibrated raw gyroscope data.
   */
  packet.gyro_x = (int16_t)gyr_x;
  packet.gyro_y = (int16_t)gyr_y;
  packet.gyro_z = (int16_t)gyr_z;

  int err =
      bt_gatt_notify(NULL, &imu_service.attrs[3], &packet, sizeof(packet));

  if (err) {
    LOG_ERR("Failed to send IMU data (err %d)", err);
  }

  return err;
}

/* -------------------------------------------------------------------------- */
/* Get streaming state                                                        */
/* -------------------------------------------------------------------------- */

bool imu_is_streaming(void) { return imu_streaming; }

/* -------------------------------------------------------------------------- */
/* Get notification state                                                    */
/* -------------------------------------------------------------------------- */

bool imu_is_notify_enabled(void) { return imu_notify_enabled; }