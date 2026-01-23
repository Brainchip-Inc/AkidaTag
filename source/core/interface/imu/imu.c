#include "imu_h/imu.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>

/*I2C - ACC/GYRO*/
#define I2C_NODE DT_NODELABEL(mysensor)
#define SLEEP_TIME_MS 5
atomic_t is_imu_start;
static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);

/* ---------- Internal helpers ---------- */

/**
 * @brief Verify ISM330 sensor identity using WHO_AM_I register
 *
 * Reads the WHO_AM_I register over I2C and checks whether the
 * returned value matches the expected ISM330 device ID.
 *
 * @return 0 on success, negative error code on failure
 */

static int ism330_verify_id()
{
	uint8_t id;
	uint8_t reg = ISM_WHOAMI;

	int ret = i2c_write_read_dt(&dev_i2c, &reg, 1, &id, 1);
	if (ret) {
		printk("WHO_AM_I read failed\n");
		return -EIO;
	}

	if (id != ISM_WHOAMI_EXPECTED) {
		printk("Invalid ISM ID: 0x%x\n", id);
		return -ENODEV;
	}

	printk("ISM330 ID verified: 0x%x\n", id);
	return 0;
}

/**
 * @brief Configure ISM330 accelerometer and gyroscope
 *
 * Configures:
 *   Accelerometer: 208 Hz ODR, ±8g full scale, LPF enabled
 *   Gyroscope: 208 Hz ODR, ±500 dps full scale
 *   Enables BDU (Block Data Update) and IF_INC (auto address increment)
 *
 * @return 0 on success, negative error code on failure
 */

static int ism330_configure()
{
	uint8_t accel_val = (ISM_XL_ODR_208HZ << 4) |
    (ISM_XL_FS_8G     << 2) |
    (ISM_XL_LPF1_ENABLE << 1) |
    ISM_XL_BW_1P5KHZ;
	
	uint8_t gyro_val =
    (ISM_G_ODR_208HZ << 4) |
    (ISM_G_FS_500DPS << 2) |
    (ISM_G_FS_125_DISABLE << 1);

	uint8_t bdu_val = ISM_IF_INC_ENABLE | ISM_BDU_ENABLE;

	uint8_t accel_cfg[] = { ISM_CTRL1_XL, accel_val };
	uint8_t gyro_cfg[]  = { ISM_CTRL2_G,  gyro_val };
	uint8_t en_bdu[] = {ISM_CTRL3_C, bdu_val };

	if (i2c_write_dt(&dev_i2c, accel_cfg, sizeof(accel_cfg))) {
		printk("Accel config failed\n");
		return -EIO;
	}

	if (i2c_write_dt(&dev_i2c, gyro_cfg, sizeof(gyro_cfg))) {
		printk("Gyro config failed\n");
		return -EIO;
	}
	if (i2c_write_dt(&dev_i2c, en_bdu, sizeof(en_bdu))) {
		printk("BDU config failed\n");
		return -EIO;
	}

	return 0;
}

/* ---------- Public APIs ---------- */
/**
 * @brief Initialize IMU device
 *
 * Checks I2C bus readiness and verifies the ISM330 device ID.
 * Does not start data streaming; configuration is done separately.
 *
 * @return 0 on success, negative error code on failure
 */
int32_t imu_init()
{
	if (!device_is_ready(dev_i2c.bus)) {
		printk("I2C bus not ready\n");
		return -ENODEV;
	}

	if (ism330_verify_id()) {
		return -1;
	}

	printk("ISM330_initialized\n");
	return 0;
}

/**
 * @brief Shell command to start IMU data streaming
 *
 * Configures the ISM330 sensor and sets the atomic flag
 * to enable periodic IMU data reading in the data thread.
 * @return 0 on success, negative error code on failure
 */

int32_t cmd_imu_start(const struct shell *shell, size_t argc, char **argv)
{
	if (ism330_configure()) {
		return -1;
	}
	atomic_set(&is_imu_start, 1);
	printk("ISM330_started\n");
	return 0;
}

/**
 * @brief Read raw accelerometer and gyroscope data
 *
 * Reads 3-axis gyroscope and accelerometer raw data
 * using I2C burst read and stores it in the provided structure.
 *
 * @param spec I2C device specification
 * @param data Pointer to structure holding IMU raw data
 */
void imu_read_all(const struct i2c_dt_spec *spec,
		       struct ism330_data *data)
{
	uint8_t raw[6];

	if (!i2c_burst_read_dt(spec, ISM_OUTX_L_G, raw, 6)) {
		data->gyro[0] = (int16_t)(raw[1] << 8 | raw[0]);
		data->gyro[1] = (int16_t)(raw[3] << 8 | raw[2]);
		data->gyro[2] = (int16_t)(raw[5] << 8 | raw[4]);
	}

	if (!i2c_burst_read_dt(spec, ISM_OUTX_L_XL, raw, 6)) {
		data->accel[0] = (int16_t)(raw[1] << 8 | raw[0]);
		data->accel[1] = (int16_t)(raw[3] << 8 | raw[2]);
		data->accel[2] = (int16_t)(raw[5] << 8 | raw[4]);
	}
}

/**
 * @brief Shell command to stop IMU data streaming
 *
 * Clears the IMU start flag and powers down both
 * accelerometer and gyroscope to save power.
 * @return 0 on success, negative error code on failure
 */

int32_t cmd_imu_stop(const struct shell *shell, size_t argc, char **argv)
{
	atomic_set(&is_imu_start, 0);
    uint8_t ctrl1_xl[] = { ISM_CTRL1_XL, 0x00 }; // Accel power-down
    uint8_t ctrl2_g[]  = { ISM_CTRL2_G,  0x00 }; // Gyro power-down

    if (i2c_write_dt(&dev_i2c, ctrl1_xl, sizeof(ctrl1_xl))) {
        printk("Failed to stop accelerometer\n");
        return -EIO;
    }

    if (i2c_write_dt(&dev_i2c, ctrl2_g, sizeof(ctrl2_g))) {
        printk("Failed to stop gyroscope\n");
        return -EIO;
    }
    printk("ISM330 stopped (power-down)\n");
    return 0;
}

/**
 * @brief IMU data acquisition thread
 *
 * Initializes the IMU and continuously reads accelerometer
 * and gyroscope data when imu_start flag is set.
 *
 * Converts raw data into physical units:
 *  - Accelerometer: g
 *  - Gyroscope: dps
 *
 * Outputs data over UART in CSV format for Python plotting.
 */

void imu_data_thread(void *a, void *b, void *c)
{

	  struct ism330_data data = {0};

	if (imu_init() < 0) {
		printk("ISM330 init failed\n");
		return 0;
	}
	
	while(1)
	{
		if (atomic_get(&is_imu_start)) {

			imu_read_all(&dev_i2c, &data);

			float ax = data.accel[0] / ACC_SENS_8G;
			float ay = data.accel[1] / ACC_SENS_8G;
			float az = data.accel[2] / ACC_SENS_8G;

			float gx = data.gyro[0] / GYR_SENS_500DPS;
			float gy = data.gyro[1] / GYR_SENS_500DPS;
			float gz = data.gyro[2] / GYR_SENS_500DPS;

			printk("%f,%f,%f,%f,%f,%f\n",
           	ax, ay, az,
           	gx, gy, gz);
		
		}

		k_msleep(SLEEP_TIME_MS);
	}
}

SHELL_CMD_REGISTER(imu_start, NULL, "imu_start", cmd_imu_start);
SHELL_CMD_REGISTER(imu_stop, NULL, "imu_stop", cmd_imu_stop);