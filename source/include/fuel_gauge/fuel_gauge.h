#ifndef FUEL_GAUGE_H
#define FUEL_GAUGE_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define BQ27427_I2C_ADDR 0x55

/* Standard commands */
#define BQ27427_CMD_NULL NULL
#define BQ27427_CMD_CONTROL 0x00
#define BQ27427_CMD_FLAGS 0x06
#define BQ27427_CMD_SOC 0x1C

#define BQ27427_EXT_BLOCK_DATA_CTRL 0x61
#define BQ27427_EXT_DATA_CLASS 0x3E
#define BQ27427_EXT_DATA_BLOCK 0x3F
#define BQ27427_EXT_BLOCK_DATA 0x40
#define BQ27427_EXT_CHECKSUM 0x60

#define BQ27427_CTRL_STATUS 0x0000
#define BQ27427_CTRL_DEVICE_TYPE 0x0001
#define BQ27427_CTRL_FW_VERSION 0x0002
#define BQ27427_CTRL_CHEM_ID 0x0008
#define BQ27427_CTRL_BAT_INSERT 0x000C
#define BQ27427_CTRL_BAT_REMOVE 0x000D
#define BQ27427_CTRL_SET_CFGUPDATE 0x0013
#define BQ27427_CTRL_SMOOTH_SYNC 0x0019
#define BQ27427_CTRL_CHEM_B 0x0031
#define BQ27427_CTRL_SOFT_RESET 0x0042
#define BQ27427_CTRL_UNSEAL 0x8000

#define BQ27427_FLAG_BAT_DET BIT(3)   /* battery detected        */
#define BQ27427_FLAG_CFGUPMODE BIT(4) /* config update mode      */
#define BQ27427_FLAG_ITPOR BIT(5)     /* POR occurred — in FLAGS */
#define BQ27427_CSTS_SS BIT(13)       /* sealed — in CSTS        */
/*As per 1100 mah battery*/
#define BATTERY_DESIGN_CAPACITY 1100
#define BATTERY_DESIGN_ENERGY 4070
#define BATTERY_TERMINATE_VOLTAGE 3000
/*The Taper Rate is a parameter used by the fuel gauge to detect
 * when the battery has finished charging*/
#define BATTERY_TAPER_RATE                                                     \
  100 /*Standard (0.1C)	110 mA	(1100 / 110) × 10 = 10 × 10	100*/
/*
 * Default = 4100 mV. Must be BELOW your charger's full-charge voltage.
 * Your battery charges to 4200 mV, so set to 4150 mV (50 mV margin).
 * FC condition requires: Voltage() > Taper Voltage
 */
#define BATTERY_TAPER_VOLTAGE 4150

#define BQ27427_SUBCLASS_CCGAIN 0x69
#define BQ27427_SUBCLASS_STATE 0x52
#define BQ27427_STATE_BLOCK 0x00
#define BQ27427_CCGAIN_BLOCK 0x00
#define BQ27427_CCGAIN_OFFSET 5
#define BQ27427_CCGAIN_SIGN_BIT 0x80

#define BQ27427_DEVICE_TYPE 0x0427
#define BQ27427_CHEM_ID_1202 0x1202
#define SAMPLES_COUNT 10
#define FG_DATA_MEMORY_BLOCK_SIZE 32
#define FG_DATA_MEMORY_WRITE_BUFFER_SIZE (FG_DATA_MEMORY_BLOCK_SIZE + 1)

int fuel_gauge_init(void);
int fuel_gauge_get_soc(void);
int fuel_gauge_isr_init(void);

#endif