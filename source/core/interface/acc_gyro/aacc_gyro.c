#include "acc_gyro.h"

int32_t acc_gyro_init(const struct i2c_dt_spec *dev_i2c)
{
    
	if (ism330_verify_id(dev_i2c) != 0) {
        return -1;
    }
    
    if (ism330_configure(dev_i2c) != 0) {
        return -1;
    }

    return 0;
}


#define ISM_WHOAMI   0x0F
#define ISM_CTRL1_XL 0x10  /* Accel: 104Hz, 4g */
#define ISM_CTRL2_G  0x11  /* Gyro: 104Hz, 1000dps */
#define ISM_OUTX_L_G 0x22  /* Gyro X low */
#define ISM_OUTZ_H_G 0x27  /* Gyro Z high */
#define ISM_OUTX_L_XL 0x28 /* Accel X low */
#define ISM_OUTZ_H_XL 0x2D /* Accel Z high */

#define ISM_WHOAMI_EXPECTED 0x6B


/* Verify ISM330DHCX WHO_AM_I */
int ism330_verify_id(const struct i2c_dt_spec *spec)
{
    uint8_t id;
    uint8_t reg = ISM_WHOAMI;
    
    int ret = i2c_write_read_dt(spec, &reg, 1, &id, 1);
    if (ret != 0) {
        printk("Failed to read ISM WHO_AM_I (0x%x)\n", ISM_WHOAMI);
        return -1;
    }
    
    if (id != ISM_WHOAMI_EXPECTED) {
        printk("Invalid ISM chip id! Expected 0x6B, got 0x%x\n", id);
        return -1;
    }
    printk("ISM330DHCX WHO_AM_I verified: 0x%x\n", id);
    return 0;
}

/* Configure ISM330DHCX (104Hz, accel 4g, gyro 1000dps) */
int ism330_configure(const struct i2c_dt_spec *spec)
{
    /* Enable accel: 104Hz ODR (1010b), ±4g (10b) */
    uint8_t accel_ctrl[] = {ISM_CTRL1_XL, 0x52};  /* 0101_0010 */
    int ret = i2c_write_dt(spec, accel_ctrl, 2);
    if (ret != 0) {
        printk("Failed to write CTRL1_XL (0x%x)\n", ISM_CTRL1_XL);
        return -1;
    }
    
    /* Enable gyro: 104Hz ODR (1010b), ±1000dps (11b) */
    uint8_t gyro_ctrl[] = {ISM_CTRL2_G, 0x5C};   /* 0101_1100 */
    ret = i2c_write_dt(spec, gyro_ctrl, 2);
    if (ret != 0) {
        printk("Failed to write CTRL2_G (0x%x)\n", ISM_CTRL2_G);
        return -1;
    }
    
    printk("ISM330DHCX configured: 104Hz, Accel±4g, Gyro±1000dps\n");
    return 0;
}

/* Read gyro data (6 bytes: X,Y,Z) */
void ism330_read_gyro(const struct i2c_dt_spec *spec, struct ism330_data *data)
{
    uint8_t gyro_raw[6];
    int ret = i2c_burst_read_dt(spec, ISM_OUTX_L_G, gyro_raw, 6);
    
    if (ret == 0) {
        data->gyro[0] = (int16_t)(gyro_raw[1] << 8 | gyro_raw[0]);
        data->gyro[1] = (int16_t)(gyro_raw[3] << 8 | gyro_raw[2]);
        data->gyro[2] = (int16_t)(gyro_raw[5] << 8 | gyro_raw[4]);
    } else {
        printk("Gyro read failed\n");
    }
}

/* Read accel data (6 bytes: X,Y,Z) */
void ism330_read_accel(const struct i2c_dt_spec *spec, struct ism330_data *data)
{
    uint8_t accel_raw[6];
    int ret = i2c_burst_read_dt(spec, ISM_OUTX_L_XL, accel_raw, 6);
    
    if (ret == 0) {
        data->accel[0] = (int16_t)(accel_raw[1] << 8 | accel_raw[0]);
        data->accel[1] = (int16_t)(accel_raw[3] << 8 | accel_raw[2]);
        data->accel[2] = (int16_t)(accel_raw[5] << 8 | accel_raw[4]);
    } else {
        printk("Accel read failed\n");
    }
}