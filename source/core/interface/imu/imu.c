#include "imu_h/imu.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>

/*I2C - ACC/GYRO*/
#define I2C_NODE DT_NODELABEL(mysensor)
#define SLEEP_TIME_MS 1000
atomic_t is_imu_start;
static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);

/* ---------- Internal helpers ---------- */

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

	uint8_t accel_cfg[] = { ISM_CTRL1_XL, accel_val };
	uint8_t gyro_cfg[]  = { ISM_CTRL2_G,  gyro_val };
	uint8_t en_bdu[] = {ISM_CTRL3_C,(1 << 6)};

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

int32_t imu_init()
{
	if (!device_is_ready(dev_i2c.bus)) {
		printk("I2C bus not ready\n");
		return -ENODEV;
	}

	if (ism330_verify_id()) {
		return -1;
	}

	printk("ISM330 initialized\n");
	return 0;
}

int32_t cmd_imu_start(const struct shell *shell, size_t argc, char **argv)
{
	if (ism330_configure()) {
		return -1;
	}
	atomic_set(&is_imu_start, 1);
	printk("ISM330 started\n");
	return 0;
}
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
	
			printk("ACC X:%d Y:%d Z:%d | GYR X:%d Y:%d Z:%d\n",
				   data.accel[0], data.accel[1], data.accel[2],
				   data.gyro[0],  data.gyro[1],  data.gyro[2]);
		}

		k_msleep(SLEEP_TIME_MS);
	}
}

SHELL_CMD_REGISTER(imu_start, NULL, "imu_start", cmd_imu_start);
SHELL_CMD_REGISTER(imu_stop, NULL, "imu_stop", cmd_imu_stop);
