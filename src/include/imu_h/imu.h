#ifndef _IMU_H
#define _IMU_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

/* IMU configuration limits
 * These macros define the valid encoding ranges for IMU configuration
 * parameters. The values correspond to the Kconfig encoding.
 */
#define IMU_ODR_MIN 0
#define IMU_ODR_MAX 8

#define IMU_ACC_FS_MIN 0
#define IMU_ACC_FS_MAX 3

#define IMU_GYRO_FS_MIN 0
#define IMU_GYRO_FS_MAX 3

/* ISM330 Registers */
#define ISM_WHOAMI 0x0F
#define ISM_CTRL1_XL 0x10
#define ISM_CTRL2_G 0x11
#define ISM_CTRL3_C 0x12
#define ISM_OUTX_L_G 0x22
#define ISM_OUTX_L_XL 0x28
#define ISM_WHOAMI_EXPECTED 0x6B

#define SAMPLE_DELAY_MS 50
#define FALL_SAMPLE_DELAY_MS 5
#define FALL_DOWNSAMPLE_PERIOD_MS 5
#define CAL_SAMPLES 500

/* FIFO Configuration - OPTIMIZED */
#define FIFO_WATERMARK 8 // Very low for fastest interrupts!
#define MAX_SAMPLES_PER_READ 128
#define SAFETY_LIMIT 128 // Lower limit

/* FIFO Register Definitions */
#define ISM_FIFO_CTRL1 0x07
#define ISM_FIFO_CTRL2 0x08
#define ISM_FIFO_CTRL3 0x09
#define ISM_FIFO_CTRL4 0x0A
#define ISM_FIFO_STATUS1 0x3A
#define ISM_FIFO_STATUS2 0x3B
#define ISM_FIFO_DATA_OUT_TAG 0x78
#define ISM_FIFO_DATA_OUT_L 0x79
#define ISM_INT1_CTRL 0x0D

/* time out timeout */
#define INTERRUPT_TIMEOUT_MS 150

/* Accelerometer control registers*/
/* ODR_XL [7:4] */
typedef enum {
  ISM_XL_ODR_OFF = 0x0,
  ISM_XL_ODR_12HZ5 = 0x1,
  ISM_XL_ODR_26HZ = 0x2,
  ISM_XL_ODR_52HZ = 0x3,
  ISM_XL_ODR_104HZ = 0x4,
  ISM_XL_ODR_208HZ = 0x5,
  ISM_XL_ODR_416HZ = 0x6,
  ISM_XL_ODR_833HZ = 0x7,
  ISM_XL_ODR_1660HZ = 0x8,
  ISM_XL_ODR_3330HZ = 0x9,
  ISM_XL_ODR_6660HZ = 0xA,
} ism_xl_odr_t;

/* FS_XL [3:2] */
typedef enum {
  ISM_XL_FS_2G = 0x0,
  ISM_XL_FS_16G = 0x1,
  ISM_XL_FS_4G = 0x2,
  ISM_XL_FS_8G = 0x3,
} ism_xl_fs_t;

/*disable / enable LPF2*/
#define ISM_XL_LPF2_DISABLE 0x0
#define ISM_XL_LPF2_ENABLE 0x1

/*gyroscope control registers*/
/* ODR_G [7:4] */
typedef enum {
  ISM_G_ODR_OFF = 0x0,
  ISM_G_ODR_12HZ5 = 0x1,
  ISM_G_ODR_26HZ = 0x2,
  ISM_G_ODR_52HZ = 0x3,
  ISM_G_ODR_104HZ = 0x4,
  ISM_G_ODR_208HZ = 0x5,
  ISM_G_ODR_416HZ = 0x6,
  ISM_G_ODR_833HZ = 0x7,
  ISM_G_ODR_1660HZ = 0x8,
  ISM_G_ODR_3330HZ = 0x9,
  ISM_G_ODR_6660HZ = 0xA,
} ism_g_odr_t;

typedef enum {
  IMU_ACQ_STOP = 0,
  IMU_ACQ_START = 1,
} ism_start_stop;

/* FS_G [3:2] */
typedef enum {
  ISM_G_FS_250DPS = 0x0,
  ISM_G_FS_500DPS = 0x1,
  ISM_G_FS_1000DPS = 0x2,
  ISM_G_FS_2000DPS = 0x3,
} ism_g_fs_t;

/* FS_125 [1] */
#define ISM_G_FS_125_DISABLE 0
#define ISM_G_FS_125_ENABLE 1

/* FS_125 [1] */
#define ISM_G_FS_4000_DISABLE 0
#define ISM_G_FS_4000_ENABLE 1

/* To reset the register */
#define REG_RESET (0x00)

#define IMU_STACK_SIZE 4096
#define IMU_PRIORITY 1 /* highest */

#define ISM_CTRL3_BOOT (1U << 7)      /* BOOT */
#define ISM_CTRL3_BDU (1U << 6)       /* Block Data Update */
#define ISM_CTRL3_H_LACTIVE (1U << 5) /* Interrupt active level */
#define ISM_CTRL3_PP_OD (1U << 4)     /* Push-pull / Open-drain */
#define ISM_CTRL3_SIM_3WIRE (1U << 3) /* SPI 3-wire mode */
#define ISM_CTRL3_IF_INC (1U << 2)    /* Register auto-increment */
#define ISM_CTRL3_SW_RESET (1U << 0)  /* Software reset */

/* INT1_CTRL bit definitions */
#define ISM_INT1_DEN_DRDY_FLAG (1 << 7)
#define ISM_INT1_CNT_BDR (1 << 6)
#define ISM_INT1_FIFO_FULL (1 << 5)
#define ISM_INT1_FIFO_OVR (1 << 4)
#define ISM_INT1_FIFO_TH (1 << 3)
#define ISM_INT1_BOOT (1 << 2)
#define ISM_INT1_DRDY_G (1 << 1)
#define ISM_INT1_DRDY_XL (1 << 0)

/* FIFO mode selection (FIFO_MODE[2:0]) */
#define ISM_FIFO_MODE_BYPASS 0x00       /* 000: FIFO disabled */
#define ISM_FIFO_MODE_FIFO 0x01         /* 001: FIFO mode (stop when full) */
#define ISM_FIFO_MODE_RESERVED_010 0x02 /* 010: Reserved */
#define ISM_FIFO_MODE_CONTINUOUS_TO_FIFO 0x03   /* 011: Continuous → FIFO */
#define ISM_FIFO_MODE_BYPASS_TO_CONTINUOUS 0x04 /* 100: Bypass → Continuous */
#define ISM_FIFO_MODE_RESERVED_101 0x05         /* 101: Reserved */
#define ISM_FIFO_MODE_CONTINUOUS 0x06     /* 110: Continuous (overwrite old) */
#define ISM_FIFO_MODE_BYPASS_TO_FIFO 0x07 /* 111: Bypass → FIFO */

/* FIFO Batch Data Rate codes */
#define ISM_BDR_NOT_BATCHED 0x00
#define ISM_BDR_12_5_HZ 0x01
#define ISM_BDR_26_HZ 0x02
#define ISM_BDR_52_HZ 0x03
#define ISM_BDR_104_HZ 0x04
#define ISM_BDR_208_HZ 0x05
#define ISM_BDR_417_HZ 0x06
#define ISM_BDR_833_HZ 0x07
#define ISM_BDR_1667_HZ 0x08
#define ISM_BDR_3333_HZ 0x09
#define ISM_BDR_6667_HZ 0x0A

/*tag ID*/
#define ACCELEROMETER 0x02
#define GYROSCOPE 0x01

#define MG_PER_G 1000.0f
#define MDPS_PER_DPS 1000.0f

/*data size*/
#define RAW_DATA_POLL_SIZE 6
#define RAW_DATA_INTR_SIZE 7
#define REG_BUF_SIZE 2
#define SEN_DATA_COUNT 3

/* ISM330 data structure */
struct ism330_data {
  int16_t accel[3];
  int16_t gyro[3];
};

#define ACC_1G_RAW 4098

/* ================= Fall / no-fall windowing ================= */

#define FALL_AXIS_COUNT 6 /* ax, ay, az, gx, gy, gz */

/* 200 samples @ 200 Hz = 1.0 s window. */
#ifndef CONFIG_FALL_WINDOW_LEN
#define CONFIG_FALL_WINDOW_LEN 200
#endif
#define FALL_WINDOW_LEN CONFIG_FALL_WINDOW_LEN

/* 50% overlapping sliding window */
#define FALL_WINDOW_STRIDE (FALL_WINDOW_LEN / 2)

/* Sensor ODR/FS used specifically for fall-detection capture */
#ifndef CONFIG_FALL_ACC_ODR
#define CONFIG_FALL_ACC_ODR ISM_XL_ODR_208HZ
#endif
#ifndef CONFIG_FALL_GYRO_ODR
#define CONFIG_FALL_GYRO_ODR ISM_G_ODR_208HZ
#endif
#ifndef CONFIG_FALL_ACC_FS
#define CONFIG_FALL_ACC_FS ISM_XL_FS_8G
#endif
#ifndef CONFIG_FALL_GYRO_FS
#define CONFIG_FALL_GYRO_FS ISM_G_FS_1000DPS
#endif

/** Callback type invoked when a complete fall-detection IMU window is ready for inference. */
typedef void (*fall_window_cb_t)(const float window[FALL_WINDOW_LEN][FALL_AXIS_COUNT]);

/* Register the callback invoked on every completed window. Call once before
 * imu_fall_capture_start(). Passing NULL disarms it. */
void imu_register_fall_callback(fall_window_cb_t cb);

/* Start pushing IMU samples into the fall-detection window.
 * Configures the ISM330 with the given ODR/FS if the IMU is not already
 * running, resets the window index, and begins sampling. Returns 0 on
 * success, -EIO on sensor configuration failure. */
int32_t imu_fall_capture_start(uint8_t acc_odr, uint8_t acc_fs, uint8_t gyro_odr, uint8_t gyro_fs);

/* Stop feeding samples into the fall-detection window and stop the IMU
 * sensor (mirrors cmd_imu_stop()). Safe to call even if capture was never
 * started. */
void imu_fall_capture_stop(void);

/* True while imu_fall_capture_start() is active and no imu_fall_capture_stop()
 * has been issued yet. */
bool imu_fall_capture_is_active(void);

/* Configure the IMU for fall-detection calibration (208 Hz ODR, +-8g accel /
 * +-1000 dps gyro FS - CONFIG_FALL_ACC_ODR/FS, CONFIG_FALL_GYRO_ODR/FS) and
 * run the blocking accel+gyro offset calibration. Shared by the `imu_calibrate` CLI command and the
 * CMD_CALIBRATE BLE handler - see fall_calibrate_and_start() in main.cpp.
 * Returns 0 on success, -EIO on sensor configuration failure. */
int32_t imu_calibrate_for_fall(void);

/* Public APIs */
int32_t imu_init();
void imu_data_thread(void *a, void *b, void *c);

#endif /* ACC_GYRO_H_ */