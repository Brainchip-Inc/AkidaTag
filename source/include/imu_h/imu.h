#ifndef ACC_GYRO_H_
#define ACC_GYRO_H_

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>

/* ISM330 Sensitivity */
#define ACC_SENS_8G       4096.0f
#define GYR_SENS_500DPS   65.536f

/* ISM330 Registers */
#define ISM_WHOAMI           0x0F
#define ISM_CTRL1_XL         0x10
#define ISM_CTRL2_G          0x11
#define ISM_CTRL3_C          0x12
#define ISM_OUTX_L_G         0x22
#define ISM_OUTX_L_XL        0x28
#define ISM_WHOAMI_EXPECTED  0x6B

/* Accelerometer control registers*/
/* ODR_XL [7:4] */
typedef enum {
    ISM_XL_ODR_OFF   = 0x0,
    ISM_XL_ODR_12HZ5 = 0x1,
    ISM_XL_ODR_26HZ  = 0x2,
    ISM_XL_ODR_52HZ  = 0x3,
    ISM_XL_ODR_104HZ = 0x4,
    ISM_XL_ODR_208HZ = 0x5,
    ISM_XL_ODR_416HZ = 0x6,
    ISM_XL_ODR_833HZ = 0x7,
    ISM_XL_ODR_1660HZ= 0x8,
    ISM_XL_ODR_3330HZ= 0x9,
    ISM_XL_ODR_6660HZ= 0xA,
} ism_xl_odr_t;

/* FS_XL [3:2] */
typedef enum {
    ISM_XL_FS_2G  = 0x0,
    ISM_XL_FS_16G = 0x1,
    ISM_XL_FS_4G  = 0x2,
    ISM_XL_FS_8G  = 0x3,
} ism_xl_fs_t;


/* LPF1_BW_SEL [1] */
#define ISM_XL_LPF1_DISABLE 0
#define ISM_XL_LPF1_ENABLE  1
/* BW0_XL [0] */
#define ISM_XL_BW_1P5KHZ 0
#define ISM_XL_BW_400HZ  1

/*gyroscope control registers*/
/* ODR_G [7:4] */
typedef enum {
    ISM_G_ODR_OFF    = 0x0,
    ISM_G_ODR_12HZ5  = 0x1,
    ISM_G_ODR_26HZ   = 0x2,
    ISM_G_ODR_52HZ   = 0x3,
    ISM_G_ODR_104HZ  = 0x4,
    ISM_G_ODR_208HZ  = 0x5,
    ISM_G_ODR_416HZ  = 0x6,
    ISM_G_ODR_833HZ  = 0x7,
    ISM_G_ODR_1660HZ = 0x8,
    ISM_G_ODR_3330HZ = 0x9,
    ISM_G_ODR_6660HZ = 0xA,
} ism_g_odr_t;


/* FS_G [3:2] */
typedef enum {
    ISM_G_FS_250DPS  = 0x0,
    ISM_G_FS_500DPS  = 0x1,
    ISM_G_FS_1000DPS = 0x2,
    ISM_G_FS_2000DPS = 0x3,
} ism_g_fs_t;


/* FS_125 [1] */
#define ISM_G_FS_125_DISABLE 0
#define ISM_G_FS_125_ENABLE  1

/*  BDU/IF_INC */
#define ISM_BDU_ENABLE      (1 << 6)
#define ISM_IF_INC_ENABLE   (1 << 2)
/* BIT 0 must be 0 */
#define ISM_G_BIT0_ZERO     (0x0)

#define IMU_STACK_SIZE 4096
#define IMU_PRIORITY   3   /* highest */

/* ISM330 data structure */
struct ism330_data {
	int16_t accel[3];
	int16_t gyro[3];
};

/* Public APIs */
int32_t imu_init();
void imu_read_all(const struct i2c_dt_spec *dev_i2c,
		       struct ism330_data *data);
void imu_data_thread(void *a, void *b, void *c);

#endif /* ACC_GYRO_H_ */