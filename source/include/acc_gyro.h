#ifndef ACC_GYRO_H_
#define ACC_GYRO_H_

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ISM330 data structure */
struct ism330_data {
	int16_t accel[3];
	int16_t gyro[3];
};

/* Public APIs */
int32_t acc_gyro_init(const struct i2c_dt_spec *dev_i2c);
void acc_gyro_read_all(const struct i2c_dt_spec *dev_i2c,
		       struct ism330_data *data);

#ifdef __cplusplus
}
#endif

#endif /* ACC_GYRO_H_ */