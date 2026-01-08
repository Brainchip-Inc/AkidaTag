#include "acc_gyro.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* ISM330 Registers */
#define ISM_WHOAMI           0x0F
#define ISM_CTRL1_XL         0x10
#define ISM_CTRL2_G          0x11
#define ISM_OUTX_L_G         0x22
#define ISM_OUTX_L_XL        0x28
#define ISM_WHOAMI_EXPECTED  0x6B

/* Accelerometer control registers*/
/* ODR_XL [7:4] */
#define ISM_A_ODR_104HZ        (0x4 << 4)   /* 0100 = 104 Hz */
/* FS_XL [3:2] */
#define ISM_A_FS_4G            (0x2 << 2)   /* 10 = ±4 g */
/* LPF1_BW_SEL [1] */
#define ISM_A_LPF1_DEFAULT     (0x0 << 1)   /* controlled by CTRL8_XL */
/* BW0_XL [0] */
#define ISM_A_BW_400HZ         (0x1 << 0)   /* 1 = 400 Hz */

/*gyroscope control registers*/
/* ODR_G [7:4] */
#define ISM_G_ODR_104HZ     (0x4 << 4)

/* FS_G [3:2] */
#define ISM_G_FS_250DPS    (0x0 << 2)
#define ISM_G_FS_500DPS    (0x1 << 2)
#define ISM_G_FS_1000DPS   (0x2 << 2)
#define ISM_G_FS_2000DPS   (0x3 << 2)

/* FS_125 [1] */
#define ISM_G_FS_125DPS_OFF (0x0 << 1)
#define ISM_G_FS_125DPS_ON  (0x1 << 1)

/* BIT 0 must be 0 */
#define ISM_G_BIT0_ZERO     (0x0)

/* ---------- Internal helpers ---------- */

static int ism330_verify_id(const struct i2c_dt_spec *spec)
{
	uint8_t id;
	uint8_t reg = ISM_WHOAMI;

	int ret = i2c_write_read_dt(spec, &reg, 1, &id, 1);
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

static int ism330_configure(const struct i2c_dt_spec *spec)
{
	uint8_t accel_val =
        ISM_A_ODR_104HZ |
        ISM_A_FS_4G     |
        ISM_A_LPF1_DEFAULT |
        ISM_A_BW_400HZ;
	
		uint8_t gyro_val =
        ISM_G_ODR_104HZ |
        ISM_G_FS_1000DPS |
        ISM_G_FS_125DPS_OFF;


	uint8_t accel_cfg[] = { ISM_CTRL1_XL, accel_val };
	uint8_t gyro_cfg[]  = { ISM_CTRL2_G,  gyro_val };

	if (i2c_write_dt(spec, accel_cfg, sizeof(accel_cfg))) {
		printk("Accel config failed\n");
		return -EIO;
	}

	if (i2c_write_dt(spec, gyro_cfg, sizeof(gyro_cfg))) {
		printk("Gyro config failed\n");
		return -EIO;
	}

	return 0;
}

/* ---------- Public APIs ---------- */

int32_t acc_gyro_init(const struct i2c_dt_spec *dev_i2c)
{
	if (!device_is_ready(dev_i2c->bus)) {
		printk("I2C bus not ready\n");
		return -ENODEV;
	}

	if (ism330_verify_id(dev_i2c)) {
		return -1;
	}

	if (ism330_configure(dev_i2c)) {
		return -1;
	}

	printk("ISM330 initialized\n");
	return 0;
}

void acc_gyro_read_all(const struct i2c_dt_spec *spec,
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

