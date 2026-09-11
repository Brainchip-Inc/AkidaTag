#include "imu_h/imu.h"
#include <stdlib.h>
#if IS_ENABLED(CONFIG_WDT_ENABLE)
#include "watchdog_h/watchdog.h"
#endif
#include <math.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(imu, LOG_LEVEL_DBG);

/* ================= CONFIG ================= */

#define I2C_NODE DT_NODELABEL(mysensor)
static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);

#if CONFIG_IMU_USE_INTERRUPT
#define IMUI DT_ALIAS(imui)
static const struct gpio_dt_spec imu_int = GPIO_DT_SPEC_GET(IMUI, gpios);
static uint32_t interrupt_count = 0;
static uint32_t overflow_count = 0;
static uint32_t error_count = 0;
#endif

static atomic_t is_imu_start;

struct imu_sample {
  float acc[SEN_DATA_COUNT];
  float gyro[SEN_DATA_COUNT];
};

static float acc_sens;
static float gyr_sens;

/* ================= OFFSETS ================= */

static float acc_offset[SEN_DATA_COUNT];
static float gyro_offset[SEN_DATA_COUNT];
#if CONFIG_IMU_USE_INTERRUPT
static float acc_filt[SEN_DATA_COUNT];
static float gyro_filt[SEN_DATA_COUNT];
#endif

/* Accelerometer sensitivity (mg/LSB)
 * Source: ISM330 datasheet, Table 2 – Mechanical characteristics (page 10)
 */
static const float acc_sens_mg_per_lsb[] = {
    [ISM_XL_FS_2G] = 0.061f,
    [ISM_XL_FS_4G] = 0.122f,
    [ISM_XL_FS_8G] = 0.244f,
    [ISM_XL_FS_16G] = 0.488f,
};

/* Gyroscope sensitivity (mdps/LSB)
 * Source: ISM330 datasheet, Table 2 – Mechanical characteristics (page 10)
 */
static const float gyr_sens_mdps_per_lsb[] = {
    [ISM_G_FS_250DPS] = 8.75f,
    [ISM_G_FS_500DPS] = 17.50f,
    [ISM_G_FS_1000DPS] = 35.0f,
    [ISM_G_FS_2000DPS] = 70.0f,
};
#if CONFIG_IMU_USE_INTERRUPT
/* ================= FIFO INTERRUPT ================= */

static struct gpio_callback imu_gpio_cb;
K_SEM_DEFINE(fifo_sem, 0, 1);
#endif

/*
 * Values can also be updated at runtime using the imu_start shell command.
 * Encoding for ODR and FS is defined in Kconfig.
 */
static uint8_t imu_acc_odr = CONFIG_IMU_ACC_ODR;
static uint8_t imu_acc_fs = CONFIG_IMU_ACC_FS;
static uint8_t imu_gyro_odr = CONFIG_IMU_GYRO_ODR;
static uint8_t imu_gyro_fs = CONFIG_IMU_GYRO_FS;
static uint8_t imu_fifo_acc_odr = CONFIG_IMU_FIFO_ACC_ODR;
static uint8_t imu_fifo_gyro_odr = CONFIG_IMU_FIFO_GYRO_ODR;
static uint8_t imu_fifo_watermark = CONFIG_IMU_FIFO_WATERMARK;

#if CONFIG_IMU_USE_INTERRUPT
/**
 * @brief IMU FIFO interrupt callback
 *
 * This function is called when the IMU asserts the FIFO threshold interrupt.
 * Releases the FIFO semaphore
 * to wake up the IMU processing thread.
 *
 * @param dev  GPIO device pointer (unused)
 * @param cb   GPIO callback structure (unused)
 * @param pins Triggered GPIO pin mask (unused)
 */
static void imu_int_cb(const struct device *dev, struct gpio_callback *cb,
                       uint32_t pins) {
  ARG_UNUSED(dev);
  ARG_UNUSED(cb);
  ARG_UNUSED(pins);

  interrupt_count++;
  k_sem_give(&fifo_sem);
}
#endif /* CONFIG_IMU_USE_INTERRUPT */

/**
 * @brief Update accelerometer and gyroscope sensitivity values
 *
 * Sets the global accelerometer and gyroscope sensitivity scaling factors
 * based on the configured full-scale range.
 */
void imu_update_sensitivity(void) {
  acc_sens = acc_sens_mg_per_lsb[CONFIG_IMU_ACC_FS];
  gyr_sens = gyr_sens_mdps_per_lsb[CONFIG_IMU_GYRO_FS];
}

/**
 * @brief Verify ISM330 device identity
 *
 * Reads the WHO_AM_I register and checks it against the expected value
 * to confirm that the correct IMU device is connected.
 *
 * @return 0 on success, negative error code on failure
 */
static int ism330_verify_id(void) {
  uint8_t id;
  uint8_t reg = ISM_WHOAMI;

  if (i2c_write_read_dt(&dev_i2c, &reg, 1, &id, 1)) {
    return -EIO;
  }

  if (id != ISM_WHOAMI_EXPECTED) {
    return -ENODEV;
  }

  LOG_INF("ISM330 ID verified: 0x%02X", id);
  return 0;
}

/**
 * @brief Configure ISM330 accelerometer and gyroscope
 *
 * Configures:
 *  - Accelerometer ODR and full-scale range
 *  - Gyroscope ODR and full-scale range
 *  - Enables block data update (BDU)
 *  - Enables auto-increment for multi-byte I2C reads
 *
 * @return 0 on success, negative error code on failure
 */
static int ism330_configure(void) {
  uint8_t buf[REG_BUF_SIZE];
  int ret;

  /* Configure Accelerometer: 208 Hz, ±8g */
  uint8_t accel_val = ((imu_acc_odr & 0x0F) << 4) | ((imu_acc_fs & 0x03) << 2) |
                      (ISM_XL_LPF2_ENABLE << 1);

  /* Configure Gyroscope: 208 Hz, ±500 dps */
  uint8_t gyro_val = ((imu_gyro_odr & 0x0F) << 4) |
                     ((imu_gyro_fs & 0x03) << 2) | (ISM_G_FS_125_DISABLE << 1) |
                     (ISM_G_FS_4000_DISABLE << 0);

  buf[0] = ISM_CTRL1_XL;
  buf[1] = accel_val;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("Accel config failed: %d", ret);
    return -EIO;
  }

  buf[0] = ISM_CTRL2_G;
  buf[1] = gyro_val;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("Gyro config failed: %d", ret);
    return -EIO;
  }

  /* CRITICAL: Configure CTRL3_C with BDU and auto-increment */
  buf[0] = ISM_CTRL3_C;
  buf[1] = ISM_CTRL3_BDU | ISM_CTRL3_IF_INC; // (1<<6) | (1<<2) BDU=1, IF_INC=1
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("CTRL3_C config failed: %d", ret);
    return -EIO;
  }

  /* Verify CTRL3_C */
  uint8_t ctrl3_verify;
  ret = i2c_reg_read_byte_dt(&dev_i2c, ISM_CTRL3_C, &ctrl3_verify);
  if (ret == 0) {
    LOG_INF("CTRL3_C: 0x%02X (IF_INC=%s, BDU=%s)", ctrl3_verify,
            (ctrl3_verify & ISM_CTRL3_IF_INC) ? "ON" : "OFF",
            (ctrl3_verify & ISM_CTRL3_BDU) ? "ON" : "OFF");

    if (!(ctrl3_verify & ISM_CTRL3_IF_INC)) {
      LOG_ERR("Auto-increment NOT enabled!");
      return -EIO;
    }
  }

  LOG_INF("ISM330 configured successfully");
  return 0;
}

#if CONFIG_IMU_USE_INTERRUPT
/**
 * @brief Reset the IMU FIFO
 *
 * Puts the FIFO into bypass mode and then re-enables continuous mode
 * to clear all stored samples.
 *
 * @return 0 on success, negative error code on failure
 */
static int ism330_fifo_reset(void) {
  uint8_t buf[REG_BUF_SIZE];
  int ret;

  /* Put FIFO in bypass/reset mode */
  buf[0] = ISM_FIFO_CTRL4;
  buf[1] = REG_RESET;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("FIFO bypass/reset failed (FIFO_CTRL4, ret=%d)", ret);
    return -EIO;
  }

  k_msleep(10);

  /* Re-enable continuous mode */
  buf[0] = ISM_FIFO_CTRL4;
  buf[1] = ISM_FIFO_MODE_CONTINUOUS;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("FIFO continuous re-enable failed (FIFO_CTRL4, ret=%d)", ret);
    return -EIO;
  }

  LOG_INF("FIFO reset successful");
  return 0;
}

/**
 * @brief Enable and configure IMU FIFO
 *
 * Configures:
 *  - FIFO watermark level
 *  - FIFO batch data rates for accelerometer and gyroscope
 *  - FIFO continuous mode
 *  - FIFO threshold interrupt on INT1
 *
 * @return 0 on success, negative error code on failure
 */
static int ism330_fifo_enable(void) {
  uint8_t buf[REG_BUF_SIZE];
  int ret;

  /* Reset FIFO */
  buf[0] = ISM_FIFO_CTRL4;
  buf[1] = REG_RESET;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("FIFO reset failed (FIFO_CTRL4, ret=%d)", ret);
    return -EIO;
  }

  k_msleep(10);

  /* FIFO watermark */
  buf[0] = ISM_FIFO_CTRL1;
  buf[1] = imu_fifo_watermark;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("FIFO watermark set failed (FIFO_CTRL1, ret=%d)", ret);
    return -EIO;
  }

  /* FIFO_CTRL2 */
  buf[0] = ISM_FIFO_CTRL2;
  buf[1] = REG_RESET;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("FIFO_CTRL2 write failed (ret=%d)", ret);
    return -EIO;
  }

  /* FIFO_CTRL3: sensor batch data rates */
  buf[0] = ISM_FIFO_CTRL3;
  buf[1] = (imu_fifo_gyro_odr << 4) | imu_fifo_acc_odr;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("FIFO_CTRL3 write failed (gyro_bdr=%d acc_bdr=%d ret=%d)",
            imu_fifo_gyro_odr, imu_fifo_acc_odr, ret);
    return -EIO;
  }

  /* FIFO_CTRL4: Continuous mode */
  buf[0] = ISM_FIFO_CTRL4;
  buf[1] = ISM_FIFO_MODE_CONTINUOUS;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("FIFO continuous mode set failed (FIFO_CTRL4, ret=%d)", ret);
    return -EIO;
  }

  k_msleep(10);

  /* Enable FIFO threshold interrupt on INT1 */
  buf[0] = ISM_INT1_CTRL;
  buf[1] = ISM_INT1_FIFO_TH;
  ret = i2c_write_dt(&dev_i2c, buf, sizeof(buf));
  if (ret) {
    LOG_ERR("INT1 FIFO threshold enable failed (INT1_CTRL, ret=%d)", ret);
    return -EIO;
  }

  LOG_INF("FIFO enabled successfully (watermark=%d)", FIFO_WATERMARK);
  return 0;
}

/**
 * @brief Read and process data from the IMU FIFO
 *
 * Reads FIFO status, detects overflow, and processes available FIFO entries.
 * Accelerometer and gyroscope samples are decoded using FIFO tags,
 * converted to physical units, and printed.
 *
 * Handles FIFO overflow recovery and maintains runtime statistics.
 */
static void imu_fifo_read(void) {
  uint8_t s1, s2;
  uint16_t current_level;
  uint8_t raw[RAW_DATA_INTR_SIZE];
  int ret;
  int samples_read = 0;

  /* Read FIFO status */
  ret = i2c_reg_read_byte_dt(&dev_i2c, ISM_FIFO_STATUS1, &s1);
  if (ret) {
    error_count++;
    LOG_ERR("FIFO_STATUS1 read failed (ret=%d)", ret);
    return;
  }

  ret = i2c_reg_read_byte_dt(&dev_i2c, ISM_FIFO_STATUS2, &s2);
  if (ret) {
    error_count++;
    LOG_ERR("FIFO_STATUS2 read failed (ret=%d)", ret);
    return;
  }

  current_level = ((s2 & 0x03) << 8) | s1;

  /* Handle overflow */
  if (s2 & BIT(6)) {
    overflow_count++;
    LOG_WRN("FIFO OVERFLOW #%u (level=%u)", overflow_count, current_level);
    ism330_fifo_reset();
    return;
  }

  /* If FIFO empty, done */
  if (current_level == 0) {
    return;
  }

  /* Read all available samples */
  int batch_limit = current_level;

  for (int i = 0; i < batch_limit; i++) {
    /* Read TAG + DATA (7 bytes) */
    ret = i2c_burst_read_dt(&dev_i2c, ISM_FIFO_DATA_OUT_TAG, raw,
                            RAW_DATA_INTR_SIZE);
    if (ret) {
      error_count++;
      LOG_ERR("FIFO burst read failed (ret=%d, i=%d)", ret, i);
      break;
    }
    uint8_t tag = raw[0];
    uint8_t tag_sensor = (tag >> 3) & 0x1F;

    if (tag_sensor == ACCELEROMETER) {
      int16_t ax = (raw[2] << 8) | raw[1];
      int16_t ay = (raw[4] << 8) | raw[3];
      int16_t az = (raw[6] << 8) | raw[5];

      acc_filt[0] = (ax * acc_sens) / MG_PER_G;
      acc_filt[1] = (ay * acc_sens) / MG_PER_G;
      acc_filt[2] = (az * acc_sens) / MG_PER_G;
    } else if (tag_sensor == GYROSCOPE) {
      int16_t gx = (raw[2] << 8) | raw[1];
      int16_t gy = (raw[4] << 8) | raw[3];
      int16_t gz = (raw[6] << 8) | raw[5];

      gyro_filt[0] = (gx * gyr_sens) / MDPS_PER_DPS;
      gyro_filt[1] = (gy * gyr_sens) / MDPS_PER_DPS;
      gyro_filt[2] = (gz * gyr_sens) / MDPS_PER_DPS;

      // LOG_INF("%f,%f,%f,%f,%f,%f", acc_filt[0], acc_filt[1], acc_filt[2], //
      // gyro_filt[0], gyro_filt[1], gyro_filt[2]);

    } else {
      LOG_WRN("Unknown FIFO tag 0x%02X (sensor=%u)", tag, tag_sensor);
    }

    samples_read++;

    /* Safety limit */
    if (samples_read >= SAFETY_LIMIT) {
      LOG_WRN("SAFETY_LIMIT reached (%d samples)", samples_read);
      break;
    }
  }
}
#endif /* CONFIG_IMU_USE_INTERRUPT */

/**
 * @brief Initialize IMU hardware and interrupt configuration
 *
 * Verifies I2C readiness, checks IMU identity, and configures
 * the GPIO interrupt pin if interrupt mode is enabled.
 *
 * @return 0 on success, negative error code on failure
 */
int32_t imu_init(void) {
  if (!device_is_ready(dev_i2c.bus)) {
    LOG_ERR("I2C not ready");
    return -ENODEV;
  }

  if (ism330_verify_id()) {
    LOG_ERR("ISM330 verify failed");
    return -EIO;
  }
#if CONFIG_IMU_USE_INTERRUPT
  int ret;
  /* Setup GPIO interrupt */
  if (!device_is_ready(imu_int.port)) {
    LOG_ERR("GPIO not ready");
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(&imu_int, GPIO_INPUT);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_interrupt_configure_dt(&imu_int, GPIO_INT_EDGE_TO_ACTIVE);
  if (ret) {
    return ret;
  }

  gpio_init_callback(&imu_gpio_cb, imu_int_cb, BIT(imu_int.pin));
  ret = gpio_add_callback(imu_int.port, &imu_gpio_cb);
  if (ret) {
    return ret;
  }
#endif
  LOG_INF("IMU init done (%s mode)",
          IS_ENABLED(CONFIG_IMU_USE_INTERRUPT) ? "INTERRUPT" : "POLLING");
  return 0;
}

/**
 * @brief Read raw accelerometer and gyroscope data
 *
 * Reads raw sensor data directly from output registers
 * without using the FIFO.
 *
 * @param d Pointer to structure where raw data will be stored
 */

static void imu_read_raw(struct ism330_data *d) {
  uint8_t raw[RAW_DATA_POLL_SIZE];

  i2c_burst_read_dt(&dev_i2c, ISM_OUTX_L_XL, raw, RAW_DATA_POLL_SIZE);
  d->accel[0] = (int16_t)(raw[1] << 8 | raw[0]);
  d->accel[1] = (int16_t)(raw[3] << 8 | raw[2]);
  d->accel[2] = (int16_t)(raw[5] << 8 | raw[4]);

  i2c_burst_read_dt(&dev_i2c, ISM_OUTX_L_G, raw, RAW_DATA_POLL_SIZE);
  d->gyro[0] = (int16_t)(raw[1] << 8 | raw[0]);
  d->gyro[1] = (int16_t)(raw[3] << 8 | raw[2]);
  d->gyro[2] = (int16_t)(raw[5] << 8 | raw[4]);
}

/**
 * @brief Perform IMU calibration
 *
 * Collects multiple samples while the device is stationary
 * to calculate accelerometer and gyroscope offsets.
 * Gravity is compensated on the Z-axis of the accelerometer.
 */
static void imu_calibrate(void) {
  struct ism330_data d;
  int32_t acc_sum[SEN_DATA_COUNT] = {0};
  int32_t gyr_sum[SEN_DATA_COUNT] = {0};

  LOG_INF("Calibrating... keep device still");

  for (int i = 0; i < CAL_SAMPLES; i++) {
    imu_read_raw(&d);

    for (int j = 0; j < 3; j++) {
      acc_sum[j] += d.accel[j];
      gyr_sum[j] += d.gyro[j];
    }
    k_msleep(SAMPLE_DELAY_MS);
  }

  acc_offset[0] = acc_sum[0] / CAL_SAMPLES;
  acc_offset[1] = acc_sum[1] / CAL_SAMPLES;
  acc_offset[2] = (acc_sum[2] / CAL_SAMPLES) - (int32_t)acc_sens;

  gyro_offset[0] = gyr_sum[0] / CAL_SAMPLES;
  gyro_offset[1] = gyr_sum[1] / CAL_SAMPLES;
  gyro_offset[2] = gyr_sum[2] / CAL_SAMPLES;

  LOG_INF("Calibration complete");
}

static bool imu_validate_config(void) {
  /* ODR range check */
  if (imu_acc_odr < IMU_ODR_MIN || imu_acc_odr > IMU_ODR_MAX)
    return false;

  if (imu_gyro_odr < IMU_ODR_MIN || imu_gyro_odr > IMU_ODR_MAX)
    return false;

  /* FS range check */
  if (imu_acc_fs < IMU_ACC_FS_MIN || imu_acc_fs > IMU_ACC_FS_MAX)
    return false;

  if (imu_gyro_fs < IMU_GYRO_FS_MIN || imu_gyro_fs > IMU_GYRO_FS_MAX)

    return false;
#if CONFIG_IMU_USE_INTERRUPT

  if (imu_fifo_acc_odr < IMU_ODR_MIN || imu_fifo_acc_odr > IMU_ODR_MAX)
    return false;

  if (imu_fifo_gyro_odr < IMU_ODR_MIN || imu_fifo_gyro_odr > IMU_ODR_MAX)
    return false;
  /* Logical constraint: FIFO ODR ≤ Sensor ODR */
  if (imu_fifo_acc_odr > imu_acc_odr)
    return false;

  if (imu_fifo_gyro_odr > imu_gyro_odr)
    return false;

  if (imu_fifo_watermark != 8 && imu_fifo_watermark != 16 &&
      imu_fifo_watermark != 32) {
    return false;
  }
#endif

  return true;
}

/**
 * @brief Shell command to start IMU operation
 *
 * Configures sensors, updates sensitivity,
 * enables FIFO (if interrupt mode is enabled), and starts data acquisition.
 *
 * @param shell Shell instance
 * @param argc  Argument count
 * @param argv  Argument list
 *
 * @return 0 on success
 */

static int32_t cmd_imu_start(const struct shell *shell, size_t argc,
                             char **argv) {
  if (argc != 8) {
    shell_print(shell, "Usage:\n"
                       "imu_start <acc_odr> <acc_fs> "
                       "<gyro_odr> <gyro_fs> "
                       "<fifo_acc_odr> <fifo_gyro_odr> "
                       "<watermark>");
    return -EINVAL;
  }

  /* Parse arguments */
  imu_acc_odr = (uint8_t)strtol(argv[1], NULL, 10);
  imu_acc_fs = (uint8_t)strtol(argv[2], NULL, 10);
  imu_gyro_odr = (uint8_t)strtol(argv[3], NULL, 10);
  imu_gyro_fs = (uint8_t)strtol(argv[4], NULL, 10);
#if CONFIG_IMU_USE_INTERRUPT
  imu_fifo_acc_odr = (uint8_t)strtol(argv[5], NULL, 10);
  imu_fifo_gyro_odr = (uint8_t)strtol(argv[6], NULL, 10);
  imu_fifo_watermark = (uint8_t)strtol(argv[7], NULL, 10);
#endif
  if (!imu_validate_config()) {
    shell_print(shell, "ERROR: Invalid IMU configuration");
    return -EINVAL;
  }

  shell_print(shell, "Updated IMU configuration:");
  shell_print(shell, " ACC_ODR        = %d", imu_acc_odr);
  shell_print(shell, " ACC_FS         = %d", imu_acc_fs);
  shell_print(shell, " GYRO_ODR       = %d", imu_gyro_odr);
  shell_print(shell, " GYRO_FS        = %d", imu_gyro_fs);
  shell_print(shell, " FIFO_ACC_ODR   = %d", imu_fifo_acc_odr);
  shell_print(shell, " FIFO_GYRO_ODR  = %d", imu_fifo_gyro_odr);
  shell_print(shell, " IMU_FIFO_WATERMARK  = %d", imu_fifo_watermark);

  int ret;

  ret = ism330_configure();
  if (ret) {
    LOG_ERR("IMU config failed: %d", ret);
    return -EIO;
  }
  imu_update_sensitivity();

  if (acc_sens <= 0.0f || gyr_sens <= 0.0f) {
    LOG_ERR("Invalid IMU sensitivity (acc=%f gyr=%f)", acc_sens, gyr_sens);
    return -EIO;
  }

  k_msleep(100);

#if CONFIG_IMU_USE_INTERRUPT
  ret = ism330_fifo_enable();
  if (ret) {
    LOG_ERR("FIFO enable failed: %d", ret);
    return -EIO;
  }
  LOG_INF("  Watermark: %d samples", FIFO_WATERMARK);
  LOG_INF("  Time out: %dms", INTERRUPT_TIMEOUT_MS);
#endif
  atomic_set(&is_imu_start, IMU_ACQ_START);
  shell_print(shell, "IMU started");

  return 0;
}

/**
 * @brief Shell command to stop IMU operation
 *
 * Stops data acquisition, resets FIFO and sensor configuration,
 * and prints runtime statistics when interrupt mode is enabled.
 *
 * @param shell Shell instance
 * @param argc  Argument count
 * @param argv  Argument list
 *
 * @return 0 on success
 */

static int32_t cmd_imu_stop(const struct shell *shell, size_t argc,
                            char **argv) {
  uint8_t buf[REG_BUF_SIZE];

  atomic_set(&is_imu_start, IMU_ACQ_STOP);

  buf[0] = ISM_FIFO_CTRL4;
  buf[1] = REG_RESET;
  i2c_write_dt(&dev_i2c, buf, sizeof(buf));

  buf[0] = ISM_CTRL1_XL;
  buf[1] = REG_RESET;
  i2c_write_dt(&dev_i2c, buf, sizeof(buf));

  buf[0] = ISM_CTRL2_G;
  buf[1] = REG_RESET;
  i2c_write_dt(&dev_i2c, buf, sizeof(buf));

#if CONFIG_IMU_USE_INTERRUPT
  LOG_INF("=== STATISTICS ===");
  LOG_INF("Interrupts:    %u", interrupt_count);
  LOG_INF("Overflows:     %u", overflow_count);
  LOG_INF("Errors:        %u", error_count);
#endif
  LOG_INF("IMU_STOPPED");
  return 0;
}

/**
 * @brief IMU data processing thread
 *
 * Main IMU processing loop.
 * - In interrupt mode: waits for FIFO semaphore and reads FIFO data
 * - In polling mode: periodically reads raw sensor data
 *
 * Handles FIFO timeout recovery and continuously processes sensor data.
 *
 * @param a Unused
 * @param b Unused
 * @param c Unused
 */

void imu_data_thread(void *a, void *b, void *c) {
#if IS_ENABLED(CONFIG_WDT_ENABLE)
  wdt_enable_thread(IMU);
#endif
  int ret;
  ret = imu_init();
  if (ret < 0) {
    LOG_ERR("IMU init failed: %d", ret);
    return;
  }
  LOG_INF("IMU thread started ");

  while (1) {
#if CONFIG_IMU_USE_INTERRUPT
    if (atomic_get(&is_imu_start)) {
      ret = k_sem_take(&fifo_sem, K_MSEC(INTERRUPT_TIMEOUT_MS));

      if (ret == 0) {
        imu_fifo_read();
      } else {
        LOG_WRN(" Timeout");
        ism330_fifo_reset();
      }
    } else {
      k_msleep(SAMPLE_DELAY_MS);
    }
#else
    struct ism330_data data = {0};
    if (atomic_get(&is_imu_start)) {

      imu_read_raw(&data);

      float ax = (data.accel[0] * acc_sens) / MG_PER_G;
      float ay = (data.accel[1] * acc_sens) / MG_PER_G;
      float az = (data.accel[2] * acc_sens) / MG_PER_G;

      float gx = (data.gyro[0] * gyr_sens) / MDPS_PER_DPS;
      float gy = (data.gyro[1] * gyr_sens) / MDPS_PER_DPS;
      float gz = (data.gyro[2] * gyr_sens) / MDPS_PER_DPS;

      // LOG_INF("%f,%f,%f,%f,%f,%f", ax, ay, az, gx, gy, gz);
    }

    k_msleep(SAMPLE_DELAY_MS);
#endif
#if IS_ENABLED(CONFIG_WDT_ENABLE)
    /* Mark thread as healthy */
    atomic_set(&thread_health[IMU], 1);
#endif
  }
}

SHELL_CMD_REGISTER(
    imu_start, NULL,
    "Set IMU configuration\n"
    "Usage:\n"
    "imu_start <acc_odr> <acc_fs> <gyro_odr> <gyro_fs> <fifo_acc_odr> "
    "<fifo_gyro_odr>\n"
    "Order:\n"
    "1.ACC ODR 2.ACC FS 3.GYRO ODR 4.GYRO FS 5.FIFO ACC ODR 6.FIFO GYRO ODR",
    cmd_imu_start);
SHELL_CMD_REGISTER(imu_stop, NULL, "imu_stop", cmd_imu_stop);