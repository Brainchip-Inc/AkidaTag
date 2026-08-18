#ifndef IMU_BLE_H_
#define IMU_BLE_H_
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
struct imu_calibration_packet {
  int32_t acc_offset_x;
  int32_t acc_offset_y;
  int32_t acc_offset_z;

  int32_t gyro_offset_x;
  int32_t gyro_offset_y;
  int32_t gyro_offset_z;
} __packed;

struct imu_ble_packet {
  uint32_t timestamp;

  int16_t acc_x;
  int16_t acc_y;
  int16_t acc_z;

  int16_t gyro_x;
  int16_t gyro_y;
  int16_t gyro_z;
} __packed;
int imu_send_data(int32_t acc_x, int32_t acc_y, int32_t acc_z, int32_t gyr_x,
                  int32_t gyr_y, int32_t gyr_z);
bool imu_is_streaming(void);

#endif /* EDGE_LEARNING_H_ */