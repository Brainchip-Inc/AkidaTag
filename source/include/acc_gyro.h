#ifndef ACC_GYRO_H
#define ACC_GYRO_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
/* Include the header file of the I2C API */
#include <zephyr/drivers/i2c.h>
/* Include the header file of printk() */
#include <zephyr/sys/printk.h>
/* Data structure for ISM330DHCX */

struct ism330_data {
    int16_t accel[3];  /* X, Y, Z in raw LSB */
    int16_t gyro[3];   /* X, Y, Z in raw LSB */
};
int32_t acc_gyro_init(const struct i2c_dt_spec *dev_i2c);
int ism330_verify_id(const struct i2c_dt_spec *spec);
int ism330_configure(const struct i2c_dt_spec *spec);
void ism330_read_gyro(const struct i2c_dt_spec *spec, struct ism330_data *data);
void ism330_read_accel(const struct i2c_dt_spec *spec, struct ism330_data *data);

#endif /* ACC_GYRO_H */