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

/* ================= FALL / NO-FALL WINDOWING ================= */
static float fall_window_buf[FALL_WINDOW_LEN][FALL_AXIS_COUNT];
static float fall_stage_buf[FALL_WINDOW_STRIDE][FALL_AXIS_COUNT];
static int fall_window_fill;    /* 0..FALL_WINDOW_LEN, during initial fill only */
static int fall_stage_fill;     /* 0..FALL_WINDOW_STRIDE, samples staged since last inference */
static bool fall_window_primed; /* true once the first full window has fired */

/**
 * Callback function invoked when a complete fall-detection window is
 * available for inference.
 */
static fall_window_cb_t fall_cb;
/**
 * Atomic flag indicating whether fall-data capture is currently active.
 */
static atomic_t fall_capture_active;

static int64_t fall_next_sample_ms;
/**
 * Number of raw samples used as knots for cubic spline interpolation.
 */
#define FALL_SPLINE_KNOTS 4
/**
 * Number of raw IMU samples retained in the interpolation buffer.
 *
 * The buffer stores the most recent timestamped samples required to
 * interpolate values at the target sampling timestamps.
 */
#define FALL_RAW_BUF_LEN 6

/**
 * Timestamp of each raw IMU sample in milliseconds.
 */
static int64_t fall_raw_ts_ms[FALL_RAW_BUF_LEN];
/**
 * Raw IMU sensor values corresponding to fall_raw_ts_ms.
 *
 * Each sample contains FALL_AXIS_COUNT values in the order:
 * [ax, ay, az, gx, gy, gz].
 */
static float fall_raw_val[FALL_RAW_BUF_LEN][FALL_AXIS_COUNT];
static int fall_raw_count; /* 0..FALL_RAW_BUF_LEN valid entries, oldest first */

/**
 * @brief Adds a timestamped raw IMU sample to the fall-detection buffer.
 *
 * Stores the timestamp, 3-axis accelerometer data (ax, ay, az), and
 * 3-axis gyroscope data (gx, gy, gz) in the raw IMU buffer.
 *
 * If the buffer is not full, the new sample is added at the next
 * available index. If the buffer is full, the oldest sample is
 * removed by shifting all existing samples one position forward,
 * and the new sample is added at the end.
 *
 * This function only stores the incoming timestamp and sensor values.
 * It does not modify, resample, or interpolate the IMU data.
 *
 * @param ts_ms Timestamp of the IMU sample in milliseconds.
 * @param ax    Accelerometer X-axis value.
 * @param ay    Accelerometer Y-axis value.
 * @param az    Accelerometer Z-axis value.
 * @param gx    Gyroscope X-axis value.
 * @param gy    Gyroscope Y-axis value.
 * @param gz    Gyroscope Z-axis value.
 *
 * @note The buffer maintains the most recent FALL_RAW_BUF_LEN samples.
 */
static void fall_raw_buf_push(int64_t ts_ms, float ax, float ay, float az, float gx, float gy,
                              float gz) {
    int idx;
    if (fall_raw_count < FALL_RAW_BUF_LEN) {
        idx = fall_raw_count++;
    } else {
        memmove(&fall_raw_ts_ms[0], &fall_raw_ts_ms[1],
                (FALL_RAW_BUF_LEN - 1) * sizeof(fall_raw_ts_ms[0]));
        memmove(&fall_raw_val[0], &fall_raw_val[1],
                (FALL_RAW_BUF_LEN - 1) * sizeof(fall_raw_val[0]));
        idx = FALL_RAW_BUF_LEN - 1;
    }
    fall_raw_ts_ms[idx] = ts_ms;
    fall_raw_val[idx][0] = ax;
    fall_raw_val[idx][1] = ay;
    fall_raw_val[idx][2] = az;
    fall_raw_val[idx][3] = gx;
    fall_raw_val[idx][4] = gy;
    fall_raw_val[idx][5] = gz;
}

/**
 * @brief Performs cubic spline interpolation using four raw IMU samples.
 *
 * Calculates an interpolated IMU value at the specified target timestamp
 * using four consecutive timestamped samples. A natural cubic spline is
 * used with the boundary conditions M0 = 0 and M3 = 0.
 *
 * The timestamps are converted to relative time with respect to the first
 * sample to keep the floating-point values small and improve numerical
 * precision.
 *
 * The interpolation is performed independently for each IMU axis.
 *
 * This function does not modify the input timestamps or sensor data.
 * It only calculates and returns the interpolated values for the
 * requested target timestamp.
 *
 * @param t_ms      Array of four sample timestamps in milliseconds.
 * @param y         Array containing the IMU values for the four samples.
 *                  Each sample contains FALL_AXIS_COUNT axis values.
 * @param target_ms Target timestamp in milliseconds at which the
 *                  interpolated value is required.
 * @param out       Output array containing the interpolated IMU values.
 *
 * @note The function uses four consecutive samples to perform the
 *       interpolation and is intended for the fall-detection
 *       preprocessing/resampling pipeline.
 */
static void fall_spline_eval4(const int64_t t_ms[FALL_SPLINE_KNOTS],
                              const float y[FALL_SPLINE_KNOTS][FALL_AXIS_COUNT], int64_t target_ms,
                              float out[FALL_AXIS_COUNT]) {
    /* Relative times in ms (float) - keeps magnitudes small (tens of ms) for
     * good float precision instead of raw k_uptime_get() values. */
    float t0 = 0.0f;
    float t1 = (float)(t_ms[1] - t_ms[0]);
    float t2 = (float)(t_ms[2] - t_ms[0]);
    float t3 = (float)(t_ms[3] - t_ms[0]);
    float x = (float)(target_ms - t_ms[0]);

    float h0 = t1 - t0;
    float h1 = t2 - t1;
    float h2 = t3 - t2;

    /* 2x2 system for M1, M2 (M0 = M3 = 0, natural boundary):
     *   2*(h0+h1)*M1 + h1*M2        = rhs1
     *   h1*M1        + 2*(h1+h2)*M2 = rhs2
     */
    float a11 = 2.0f * (h0 + h1), a12 = h1;
    float a21 = h1, a22 = 2.0f * (h1 + h2);
    float det = a11 * a22 - a12 * a21;

    for (int c = 0; c < FALL_AXIS_COUNT; c++) {
        float y0 = y[0][c], y1 = y[1][c], y2 = y[2][c], y3 = y[3][c];
        float rhs1 = 6.0f * ((y2 - y1) / h1 - (y1 - y0) / h0);
        float rhs2 = 6.0f * ((y3 - y2) / h2 - (y2 - y1) / h1);

        float M1 = 0.0f, M2 = 0.0f;
        if (det != 0.0f) {
            M1 = (rhs1 * a22 - a12 * rhs2) / det;
            M2 = (a11 * rhs2 - a21 * rhs1) / det;
        }

        float A = (t2 - x) / h1;
        float B = (x - t1) / h1;
        out[c] = A * y1 + B * y2 + ((A * A * A - A) * M1 + (B * B * B - B) * M2) * (h1 * h1) / 6.0f;
    }
}

static bool fall_sensor_preconfigured;
static uint8_t fall_sensor_preconfig_acc_odr, fall_sensor_preconfig_acc_fs;
static uint8_t fall_sensor_preconfig_gyro_odr, fall_sensor_preconfig_gyro_fs;

/**
 * @brief Adds a new IMU sample to the fall-detection window buffer.
 *
 * Stores incoming accelerometer and gyroscope samples in the fall-detection
 * window buffer and triggers the fall-detection callback when a complete
 * window is available.
 *
 * Initially, the function fills the buffer until FALL_WINDOW_LEN samples
 * are collected. Once the buffer is full, the fall-detection callback is
 * triggered with the complete window.
 *
 * After the initial window is filled, new samples are collected in a
 * staging buffer. When FALL_WINDOW_STRIDE new samples are available, the
 * oldest FALL_WINDOW_STRIDE samples are removed from the main window and
 * the new samples are appended. This creates a sliding window with
 * overlapping data.
 *
 * For example, with a window length of 200 samples and a stride of
 * 100 samples, the resulting windows have 50% overlap:
 *
 * The function only processes samples when fall capture is active and
 * a valid fall-detection callback is registered.
 *
 * @param ax Accelerometer X-axis value.
 * @param ay Accelerometer Y-axis value.
 * @param az Accelerometer Z-axis value.
 * @param gx Gyroscope X-axis value.
 * @param gy Gyroscope Y-axis value.
 * @param gz Gyroscope Z-axis value.
 *
 * @note The input samples are stored as six-axis IMU data:
 *       [ax, ay, az, gx, gy, gz].
 *
 * @note The function triggers fall_cb() whenever a complete window is
 *       available.
 */

static void fall_window_push(float ax, float ay, float az, float gx, float gy, float gz) {
    if (!atomic_get(&fall_capture_active) || fall_cb == NULL) {
        return;
    }

    if (!fall_window_primed) {
        float* row = fall_window_buf[fall_window_fill];
        row[0] = ax;
        row[1] = ay;
        row[2] = az;
        row[3] = gx;
        row[4] = gy;
        row[5] = gz;
        fall_window_fill++;

        if (fall_window_fill >= FALL_WINDOW_LEN) {
            fall_window_primed = true;
            fall_stage_fill = 0;
            fall_cb((const float (*)[FALL_AXIS_COUNT])fall_window_buf);
        }
        return;
    }

    float* srow = fall_stage_buf[fall_stage_fill];
    srow[0] = ax;
    srow[1] = ay;
    srow[2] = az;
    srow[3] = gx;
    srow[4] = gy;
    srow[5] = gz;
    fall_stage_fill++;

    if (fall_stage_fill >= FALL_WINDOW_STRIDE) {
        /* Drop the oldest FALL_WINDOW_STRIDE rows, keep the newest
         * (FALL_WINDOW_LEN - FALL_WINDOW_STRIDE) rows, then append the staged
         * batch - i.e. the classic 50%-overlap slide. */
        memmove(fall_window_buf[0], fall_window_buf[FALL_WINDOW_STRIDE],
                (FALL_WINDOW_LEN - FALL_WINDOW_STRIDE) * sizeof(fall_window_buf[0]));
        memcpy(fall_window_buf[FALL_WINDOW_LEN - FALL_WINDOW_STRIDE], fall_stage_buf,
               FALL_WINDOW_STRIDE * sizeof(fall_stage_buf[0]));
        fall_stage_fill = 0;
        fall_cb((const float (*)[FALL_AXIS_COUNT])fall_window_buf);
    }
}

/**
 * @brief Registers the callback function for fall-detection windows.
 *
 * Stores the provided callback function so that it can be invoked when
 * a complete fall-detection window is available.
 *
 * @param cb Callback function to be registered for fall detection.
 *
 * @note Passing NULL disables the fall-detection callback.
 */
void imu_register_fall_callback(fall_window_cb_t cb) {
    fall_cb = cb;
}

/**
 * @brief Checks whether fall-data capture is currently active.
 *
 * Returns the current state of the fall-capture flag. This can be used
 * to determine whether IMU samples should be processed for fall detection.
 *
 * @return true if fall-data capture is active, otherwise false.
 */
bool imu_fall_capture_is_active(void) {
    return atomic_get(&fall_capture_active) != 0;
}

static float acc_sens;
static float gyr_sens;

/* ================= OFFSETS ================= */

int32_t acc_offset[SEN_DATA_COUNT];
int32_t gyro_offset[SEN_DATA_COUNT];
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

/**
 * @brief Delay between consecutive IMU sampling operations in milliseconds.
 *
 * Controls the time interval between IMU sample reads and is initialized
 * to the default value defined by SAMPLE_DELAY_MS.
 */
static uint32_t imu_sample_delay_ms = SAMPLE_DELAY_MS;

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
 * based on the currently configured full-scale range.
 */
void imu_update_sensitivity(void) {
    acc_sens = acc_sens_mg_per_lsb[imu_acc_fs];
    gyr_sens = gyr_sens_mdps_per_lsb[imu_gyro_fs];
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

    fall_sensor_preconfigured = false;

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
        k_msleep(FALL_SAMPLE_DELAY_MS);
    }

    acc_offset[0] = acc_sum[0] / CAL_SAMPLES;
    acc_offset[1] = acc_sum[1] / CAL_SAMPLES;
    acc_offset[2] = (acc_sum[2] / CAL_SAMPLES) - (int32_t)ACC_1G_RAW;

  gyro_offset[0] = gyr_sum[0] / CAL_SAMPLES;
  gyro_offset[1] = gyr_sum[1] / CAL_SAMPLES;
  gyro_offset[2] = gyr_sum[2] / CAL_SAMPLES;

  LOG_INF("Calibration complete");
}

/**
 * @brief Configures and calibrates the IMU for fall detection.
 *
 * Applies the fall-detection specific accelerometer and gyroscope
 * output data rates (ODR) and full-scale (FS) settings, then
 * configures the ISM330 sensor with those settings.
 *
 * After configuration, the sensor sensitivity is updated and a short
 * delay is introduced to allow the new ODR and FS settings to settle.
 * The IMU calibration routine is then executed using the configured
 * fall-detection settings.
 *
 * After successful calibration, the fall-sensor preconfiguration cache
 * is updated so that the following fall-capture start operation can
 * reuse the already configured sensor without unnecessary reconfiguration.
 *
 * @return 0 on successful IMU configuration and calibration.
 * @return -EIO if the IMU sensor configuration fails.
 *
 * @note The preconfiguration cache is updated only after the sensor has
 *       been successfully configured and calibrated.
 */
int32_t imu_calibrate_for_fall(void) {
    imu_acc_odr = CONFIG_FALL_ACC_ODR;
    imu_acc_fs = CONFIG_FALL_ACC_FS;
    imu_gyro_odr = CONFIG_FALL_GYRO_ODR;
    imu_gyro_fs = CONFIG_FALL_GYRO_FS;
#if CONFIG_IMU_USE_INTERRUPT
    imu_fifo_acc_odr = CONFIG_FALL_ACC_ODR;
    imu_fifo_gyro_odr = CONFIG_FALL_GYRO_ODR;
    imu_fifo_watermark = CONFIG_IMU_FIFO_WATERMARK;
#endif

    int ret = ism330_configure();
    if (ret) {
        LOG_ERR("imu_calibrate_for_fall: sensor config failed: %d", ret);
        return -EIO;
    }
    imu_update_sensitivity();
    k_msleep(100); /* let the new ODR/FS settle before sampling, as cmd_imu_start() does */

    imu_calibrate();

    /* Arm the skip-reconfigure cache for the imu_fall_capture_start() call
     * that follows shortly (fall_calibrate_and_start() -> fall_app_start() ->
     * infer() -> initiate_fall_inference()) - see fall_sensor_preconfigured's
     * doc comment above fall_window_push(). Set at the very end (not before
     * imu_calibrate()) so it only reflects a configuration that is both
     * applied AND has already been used successfully for calibration. */
    fall_sensor_preconfigured = true;
    fall_sensor_preconfig_acc_odr = CONFIG_FALL_ACC_ODR;
    fall_sensor_preconfig_acc_fs = CONFIG_FALL_ACC_FS;
    fall_sensor_preconfig_gyro_odr = CONFIG_FALL_GYRO_ODR;
    fall_sensor_preconfig_gyro_fs = CONFIG_FALL_GYRO_FS;
    return 0;
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

    fall_sensor_preconfigured = false;

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
 * @brief Starts IMU data capture for fall detection.
 *
 * Configures the ISM330 IMU with the specified accelerometer and gyroscope
 * ODR and full-scale settings. If the sensor was already configured with
 * the same settings during a just-completed fall calibration, the redundant
 * sensor reconfiguration is skipped.
 *
 * When required, the function validates the IMU configuration, applies the
 * sensor settings, updates the sensor sensitivity, and allows the sensor
 * time to settle. The FIFO is enabled when interrupt-based acquisition is
 * configured.
 *
 * Before starting a new capture session, all fall-detection window,
 * staging-buffer, and raw-sample state is reset. The next resampling
 * timestamp is initialized and the raw IMU polling delay is set to the
 * fall-detection sampling rate.
 *
 * Finally, fall-data capture and IMU acquisition are enabled.
 *
 * @param acc_odr  Accelerometer output data rate configuration.
 * @param acc_fs   Accelerometer full-scale configuration.
 * @param gyro_odr Gyroscope output data rate configuration.
 * @param gyro_fs  Gyroscope full-scale configuration.
 *
 * @return 0 on successful fall-capture start.
 * @return -EINVAL if the requested IMU configuration is invalid.
 * @return -EIO if sensor configuration, sensitivity validation, or FIFO
 *         initialization fails.
 */
int32_t imu_fall_capture_start(uint8_t acc_odr, uint8_t acc_fs, uint8_t gyro_odr, uint8_t gyro_fs) {
    int ret;

    bool already_configured =
        fall_sensor_preconfigured && fall_sensor_preconfig_acc_odr == acc_odr &&
        fall_sensor_preconfig_acc_fs == acc_fs && fall_sensor_preconfig_gyro_odr == gyro_odr &&
        fall_sensor_preconfig_gyro_fs == gyro_fs;
    fall_sensor_preconfigured = false;

    imu_acc_odr = acc_odr;
    imu_acc_fs = acc_fs;
    imu_gyro_odr = gyro_odr;
    imu_gyro_fs = gyro_fs;
#if CONFIG_IMU_USE_INTERRUPT
    imu_fifo_acc_odr = acc_odr;
    imu_fifo_gyro_odr = gyro_odr;
    imu_fifo_watermark = CONFIG_IMU_FIFO_WATERMARK;
#endif

    if (already_configured) {
        LOG_INF(
            "imu_fall_capture_start: sensor already configured for fall by a "
            "just-completed calibration - skipping redundant reconfigure");
    } else {
        if (!imu_validate_config()) {
            LOG_ERR("imu_fall_capture_start: invalid IMU configuration");
            return -EINVAL;
        }

        ret = ism330_configure();
        if (ret) {
            LOG_ERR("imu_fall_capture_start: sensor config failed: %d", ret);
            return -EIO;
        }
        imu_update_sensitivity();
        if (acc_sens <= 0.0f || gyr_sens <= 0.0f) {
            LOG_ERR("imu_fall_capture_start: invalid sensitivity (acc=%f gyr=%f)", (double)acc_sens,
                    (double)gyr_sens);
            return -EIO;
        }
        k_msleep(100);
    }

#if CONFIG_IMU_USE_INTERRUPT
    ret = ism330_fifo_enable();
    if (ret) {
        LOG_ERR("imu_fall_capture_start: FIFO enable failed: %d", ret);
        return -EIO;
    }
#endif

    fall_window_fill = 0;
    fall_stage_fill = 0;
    fall_window_primed = false;
    fall_raw_count = 0; /* drop any raw samples buffered by a previous capture session */
    fall_next_sample_ms = k_uptime_get();       /* accept immediately, then grid-lock */
    imu_sample_delay_ms = FALL_SAMPLE_DELAY_MS; /* ~200 Hz raw poll; */
    atomic_set(&fall_capture_active, 1);
    atomic_set(&is_imu_start, IMU_ACQ_START);
    LOG_INF("Fall capture started (window=%d samples, %d-sample 50%% overlap stride)",
            FALL_WINDOW_LEN, FALL_WINDOW_STRIDE);
    return 0;
}

/**
 * @brief Stop feeding the fall-detection window and stop the IMU sensor.
 *
 * Closes the fall_window_push() gate first so a sample in flight cannot land
 * in a window after the sensor has already been told to power down, then
 * performs the same register resets as cmd_imu_stop().
 */
void imu_fall_capture_stop(void) {
    atomic_set(&fall_capture_active, 0);
    imu_sample_delay_ms = SAMPLE_DELAY_MS; /* restore general polling rate */

    /* Resets CTRL1_XL/CTRL2_G below without going through ism330_configure(),
     * so the "already configured for fall" cache must be invalidated here
     * explicitly too - see fall_sensor_preconfigured's doc comment above
     * fall_window_push(). */
    fall_sensor_preconfigured = false;

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

    LOG_INF("Fall capture stopped");
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

        int32_t acc_x = data.accel[0] - acc_offset[0];
        int32_t acc_y = data.accel[1] - acc_offset[1];
        int32_t acc_z = data.accel[2] - acc_offset[2];

        int32_t gyr_x = data.gyro[0] - gyro_offset[0];
        int32_t gyr_y = data.gyro[1] - gyro_offset[1];
        int32_t gyr_z = data.gyro[2] - gyro_offset[2];

        float ax = (acc_x * acc_sens) / MG_PER_G;
        float ay = (acc_y * acc_sens) / MG_PER_G;
        float az = (acc_z * acc_sens) / MG_PER_G;

        float gx = (gyr_x * gyr_sens) / MDPS_PER_DPS;
        float gy = (gyr_y * gyr_sens) / MDPS_PER_DPS;
        float gz = (gyr_z * gyr_sens) / MDPS_PER_DPS;

        /**
         * @brief Resamples raw IMU data to a fixed 5 ms (200 Hz) time grid.
         *
         * The IMU sampling loop is intended to run at approximately 5 ms intervals
         * (imu_sample_delay_ms = 5 ms). However, the actual execution time of the
         * loop can vary due to thread scheduling, processing delays, callbacks, or
         * other system activity. Therefore, the samples are not guaranteed to arrive
         * at exactly 5 ms intervals and may contain timing jitter.
         *
         * To avoid passing jittered samples directly to the fall-detection model,
         * each raw IMU sample is stored together with its actual timestamp. The
         * preprocessing then generates samples at fixed 5 ms target timestamps
         * using timestamp-based cubic spline interpolation.
         *
         * FALL_DOWNSAMPLE_PERIOD_MS is set to 5 ms, so the target samples are
         * generated at:
         *
         *   T, T + 5 ms, T + 10 ms, T + 15 ms, ...
         *
         * The actual raw sample timestamps may look like:
         *
         *   0 ms, 6 ms, 11 ms, 17 ms, 21 ms, ...
         *
         * due to scheduling jitter. Cubic interpolation estimates the sensor value
         * at the required fixed target timestamp instead of simply using the
         * nearest raw sample.
         *
         * If the target timestamp falls behind the oldest sample currently stored
         * in the buffer, the target timestamp is moved forward to avoid attempting
         * interpolation without sufficient historical data.
         *
         * The while loop continues generating all target samples that can be
         * bracketed by the currently buffered raw data. This also allows the
         * resampling pipeline to catch up if the processing loop was temporarily
         * delayed.
         *
         * @note The timestamp is not modified. The actual timestamp of every raw
         *       IMU sample is captured using k_uptime_get().
         *
         * @note Cubic interpolation changes only the sensor value at the requested
         *       target timestamp; it does not modify the original raw samples.
         *
         * @note The 5 ms delay controls the approximate raw sampling loop timing,
         *       while FALL_DOWNSAMPLE_PERIOD_MS defines the fixed target time grid.
         */
        int64_t now_ms = k_uptime_get();
        fall_raw_buf_push(now_ms, ax, ay, az, gx, gy, gz);

        /* If the next target timestamp is older than the oldest buffered sample,
        reset it forward to the next 5 ms tick to avoid interpolating without valid data. */
        if (fall_raw_count > 0 && fall_next_sample_ms < fall_raw_ts_ms[0]) {
            fall_next_sample_ms = now_ms + FALL_DOWNSAMPLE_PERIOD_MS;
        }

        /* Find four consecutive raw samples that bracket the target timestamp for
          * interpolation. If no valid bracket is available yet, exit the loop and wait for more
          * raw samples. */
        while (true) {
            int k = -1;
            for (int i = 1; i + 2 < fall_raw_count; i++) {
                if (fall_raw_ts_ms[i] <= fall_next_sample_ms &&
                    fall_next_sample_ms < fall_raw_ts_ms[i + 1]) {
                    k = i;
                    break;
                }
            }
            if (k < 0) {
                break; /* not enough buffered context yet for this tick */
            }

            float interp[FALL_AXIS_COUNT];
            fall_spline_eval4(&fall_raw_ts_ms[k - 1],
                              (const float (*)[FALL_AXIS_COUNT]) & fall_raw_val[k - 1],
                              fall_next_sample_ms, interp);
            fall_window_push(interp[0], interp[1], interp[2], interp[3], interp[4], interp[5]);

            fall_next_sample_ms += FALL_DOWNSAMPLE_PERIOD_MS;
        }
    }
    k_msleep(imu_sample_delay_ms);
#endif
#if IS_ENABLED(CONFIG_WDT_ENABLE)
    /* Mark thread as healthy */
    atomic_set(&thread_health[IMU], 1);
#endif
  }
}
/**
 * @brief Triggers IMU calibration from the shell command.
 *
 * Executes the IMU calibration routine and prints a confirmation message
 * to the shell after the calibration process is completed.
 *
 * @param shell Shell instance used to print the calibration status.
 * @param argc  Number of command-line arguments.
 * @param argv  Array of command-line arguments.
 *
 * @return 0 on successful command execution.
 *
 * @note The command arguments are currently not used.
 */
static int cmd_imu_calibrate(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    imu_calibrate();
    shell_print(shell, "IMU calibration completed.");

    return 0;
}

SHELL_CMD_REGISTER(imu_start, NULL,
                   "Set IMU configuration\n"
                   "Usage:\n"
                   "imu_start <acc_odr> <acc_fs> <gyro_odr> <gyro_fs> <fifo_acc_odr> "
                   "<fifo_gyro_odr>\n"
                   "Order:\n"
                   "1.ACC ODR 2.ACC FS 3.GYRO ODR 4.GYRO FS 5.FIFO ACC ODR 6.FIFO GYRO ODR",
                   cmd_imu_start);
SHELL_CMD_REGISTER(imu_stop, NULL, "imu_stop", cmd_imu_stop);
SHELL_CMD_REGISTER(imu_calibrate, NULL, "Calibrate IMU", cmd_imu_calibrate);