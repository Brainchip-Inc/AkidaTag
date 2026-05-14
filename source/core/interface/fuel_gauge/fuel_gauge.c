#include "fuel_gauge/fuel_gauge.h"
#include "battery/battery.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* ========== I2C ========== */
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
/* ========== Interrupt GPIO ========== */
#define FG_INT_NODE DT_ALIAS(fg_int)
static const struct gpio_dt_spec fg_int = GPIO_DT_SPEC_GET(FG_INT_NODE, gpios);
static struct gpio_callback fg_int_cb;

/**
 * @brief Interrupt handler for fuel gauge SOC change notification
 *
 * Called from ISR context when the BQ27427 pulses its GPOUT / SOC_INT pin
 * on a SOC change. Notifies the battery module via a semaphore so the
 * battery thread can read the new SOC outside ISR context.
 */
/* ========== Interrupt Thread ========== */
static void fg_int_handler(const struct device *port, struct gpio_callback *cb,
                           gpio_port_pins_t pins) {
  battery_isr_notify();
}

/**
 * @brief Read a 16-bit word from a specified fuel gauge register
 *
 * Performs an I2C read of two consecutive bytes from the given register
 * address. The read data is returned in little-endian format (LSB first) as per
 * BQ27427 communication protocol.
 *
 * @param reg Register address to read
 * @param value Pointer to store the read 16-bit value
 *
 * @return 0 on success, negative error code on failure
 */
static int fg_read_word(uint8_t reg, uint16_t *value) {
  uint8_t buf[2] = {0};
  int ret = i2c_write_read(i2c_dev, BQ27427_I2C_ADDR, &reg, 1, buf, 2);
  if (ret == 0) {
    *value = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  }
  return ret;
}
/**
 * @brief Write a single byte to a fuel gauge register
 *
 * Writes one byte of data to the specified register address.
 * Used for configuring extended data memory commands like
 * BlockDataControl(), DataClass(), and DataBlock().
 *
 * @param reg Register address to write to
 * @param val Byte value to write
 *
 * @return 0 on success, negative error code on failure
 */
static int fg_write_byte(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  return i2c_write(i2c_dev, buf, sizeof(buf), BQ27427_I2C_ADDR);
}
/**
 * @brief Read a single byte from a fuel gauge register
 *
 * Reads one byte of data from the specified register address.
 * Used for reading checksum values and verifying configuration writes.
 *
 * @param reg Register address to read from
 * @param val Pointer to store the read byte value
 *
 * @return 0 on success, negative error code on failure
 */
static int fg_read_byte(uint8_t reg, uint8_t *val) {
  return i2c_write_read(i2c_dev, BQ27427_I2C_ADDR, &reg, 1, val, 1);
}
/**
 * @brief Send a control subcommand to the fuel gauge
 *
 * Writes a 16-bit subcommand to the Control() register (0x00).
 * Used for device control operations like BAT_INSERT, SOFT_RESET,
 * reading device type, firmware version, and status.
 *
 * @param subcmd 16-bit subcommand
 * @param result Optional pointer to store result value (if subcommand returns
 * data)
 *
 * @return 0 on success, negative error code on failure
 */
static int fg_control(uint16_t subcmd, uint16_t *result) {
  uint8_t buf[3] = {
      BQ27427_CMD_CONTROL,
      (uint8_t)(subcmd & 0xFF),
      (uint8_t)(subcmd >> 8),
  };
  int ret = i2c_write(i2c_dev, buf, sizeof(buf), BQ27427_I2C_ADDR);
  if (ret) {
    printk("[BQ27427] Control command 0x%04X write failed (%d)\n", subcmd, ret);
    return ret;
  };
  if (result) {
    ret = fg_read_word(BQ27427_CMD_CONTROL, result);
  }
  return ret;
}
/**
 * @brief Compute block data checksum for Data Memory writes
 *
 * Calculates the 8-bit checksum required when writing to Data Memory blocks.
 * The checksum is computed as: 0xFF - (sum of all 32 bytes in the block).
 *
 * @param data Pointer to 32-byte block data
 * @param len Length of data (must be 32 for full block)
 *
 * @return Computed checksum byte (0x00 to 0xFF)
 */
static uint8_t fg_checksum(uint8_t *data, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len; i++)
    sum += data[i];
  return 0xFF - sum;
}

/**
 * @brief Enter CONFIG UPDATE mode to modify Data Memory parameters
 *
 * Sends SET_CFGUPDATE subcommand and waits up to 1 second for the
 * Flags()[CFGUPMODE] bit to be set. In this mode, fuel gauging is
 * suspended and host can modify configuration parameters.
 *
 * @return 0 on success, -ETIMEDOUT if CFGUPMODE not set within timeout
 */
static int fg_enter_config_update(void) {
  int ret;
  ret = fg_control(BQ27427_CTRL_SET_CFGUPDATE, BQ27427_CMD_NULL);
  if (ret) {
    printk("[BQ27427] SET_CFGUPDATE command failed (%d)\n", ret);
    return ret;
  }
  /*
   * To ensure that the gas gauge has entered CONFIG UPDATE mode correctly,
   * there needs to be at least an 1100-ms delay after sending the
   * SET_CFGUPDATE."
   */
  k_msleep(1100);
  /*
   * Poll CFGUPMODE bit for up to 1 second (10 × 100ms).
   * The gauge typically sets this bit within 500-1000ms total.
   */
  for (int i = 0; i < SAMPLES_COUNT; i++) {
    uint16_t flags = 0;
    ret = fg_read_word(BQ27427_CMD_FLAGS, &flags);
    if (ret) {
      printk("[BQ27427] FLAGS read error (%d)\n", ret);
      return ret;
    }
    if (flags & BQ27427_FLAG_CFGUPMODE) {
      printk("[BQ27427] CONFIG UPDATE entered\n");
      return 0;
    }
    k_msleep(50);
  }
  printk("[BQ27427] CFGUPMODE not set\n");
  return -ETIMEDOUT;
}

/**
 * @brief Unseal the fuel gauge for full access to Data Memory
 *
 * Sends the unseal key (0x8000) twice consecutively to transition from
 * SEALED to UNSEALED mode. Required before modifying configuration parameters.
 *
 * @return 0 on success (or already unsealed), -EIO if unseal failed
 */
static int fg_unseal(void) {
  int ret;
  uint16_t csts = 0;
  ret = fg_control(BQ27427_CTRL_STATUS, &csts);
  if (ret) {
    printk("[BQ27427] STATUS read failed (%d)\n", ret);
    return ret;
  }
  if (!(csts & BQ27427_CSTS_SS)) {
    printk("[BQ27427] Already UNSEALED\n");
    return 0;
  }
  ret = fg_control(BQ27427_CTRL_UNSEAL, BQ27427_CMD_NULL);
  if (ret) {
    printk("[BQ27427] CTRL_UNSEAL read failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to unseal */
  ret = fg_control(BQ27427_CTRL_UNSEAL, BQ27427_CMD_NULL);
  if (ret) {
    printk("[BQ27427] CTRL_UNSEAL read failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to unseal */
  ret = fg_control(BQ27427_CTRL_STATUS, &csts);
  if (ret) {
    printk("[BQ27427] CTRL_STATUS read failed (%d)\n", ret);
    return ret;
  }
  if (csts & BQ27427_CSTS_SS) {
    printk("[BQ27427] UNSEAL failed\n");
    return -EIO;
  }
  printk("[BQ27427] UNSEALED\n");
  return 0;
}
/**
 * @brief Exit CONFIG UPDATE mode via SOFT_RESET and resume normal operation
 *
 * Sends SOFT_RESET command and polls CFGUPMODE bit for up to 1 second.
 * Must be called after writing configuration parameters.
 *
 * @return 0 on success, -ETIMEDOUT if CFGUPMODE not cleared, negative on I2C
 * error
 */
static int fg_exit_config_update(void) {
  int ret;
  ret = fg_control(BQ27427_CTRL_SOFT_RESET, BQ27427_CMD_NULL);
  if (ret) {
    printk("[BQ27427] SOFT_RESET command failed (%d)\n", ret);
    return ret;
  }
  /*
   * Total timeout: 10 × 100ms = 1000ms (1 second)
   *
   * The CFGUPMODE bit is typically cleared within 500ms after SOFT_RESET.
   * 1 second timeout provides sufficient margin for reliable operation.
   */
  for (int i = 0; i < SAMPLES_COUNT; i++) {

    uint16_t flags = 0;
    ret = fg_read_word(BQ27427_CMD_FLAGS, &flags);
    if (ret) {
      printk("[BQ27427] FLAGS read error (%d)\n", ret);
      return ret;
    }
    if (!(flags & BQ27427_FLAG_CFGUPMODE)) {
      printk("[BQ27427] CONFIG UPDATE exited\n");
      return 0;
    }
    k_msleep(100);
  }
  printk("[BQ27427] CONFIG UPDATE exit timeout\n");
  return -ETIMEDOUT;
}

/**
 * @brief Set battery chemistry profile to 4.2V Li-ion (Chem ID 0x1202)
 *
 * Changes the active chemistry profile to match standard 4.2V Li-ion batteries.
 * Uses CHEM_B subcommand . Skips if already correct to avoid
 * unnecessary SOFT_RESET.
 *
 * @return 0 on success (or already correct), negative error code on failure
 */
static int fg_set_chem_id_1202(void) {
  uint16_t chem_id = 0;
  int ret;

  ret = fg_control(BQ27427_CTRL_CHEM_ID, &chem_id);
  if (ret) {
    printk("[BQ27427] CHEM_ID read failed (%d)\n", ret);
    return ret;
  }
  printk("[BQ27427] Chem ID: 0x%04X\n", chem_id);

  if (chem_id == BQ27427_CHEM_ID_1202) {
    printk("[BQ27427] Chem ID already 1202 — skipping\n");
    return 0;
  }

  ret = fg_enter_config_update();
  if (ret) {
    printk("[BQ27427] config_update failed (%d)\n", ret);
    return ret;
  }

  ret = fg_control(BQ27427_CTRL_CHEM_B, BQ27427_CMD_NULL);
  if (ret) {
    printk("[BQ27427] CHEM_B command failed (%d)\n", ret);
    return ret;
  }
  /* 1000ms delay after CHEM_B command:
   * Allows the gauge sufficient time to process the chemistry change
   * and update internal tables before exiting CONFIG UPDATE mode.
   * Without this delay, the SOFT_RESET might occur before the chemistry
   * change is fully committed to RAM.
   */
  k_msleep(1000);

  ret = fg_exit_config_update();
  if (ret) {
    printk("[BQ27427] config_update failed (%d)\n", ret);
    return ret;
  }
  /* Wait for gauge to settle after SOFT_RESET before verification */
  k_msleep(10);

  ret = fg_control(BQ27427_CTRL_CHEM_ID, &chem_id);
  if (ret) {
    printk("[BQ27427] CHEM_ID re-read failed (%d)\n", ret);
    return ret;
  }

  printk("[BQ27427] Chem ID set: 0x%04X %s\n", chem_id,
         chem_id == BQ27427_CHEM_ID_1202 ? "(OK)" : "(unexpected)");

  return (chem_id == BQ27427_CHEM_ID_1202) ? 0 : -EINVAL;
}

/**
 * @brief Write battery design parameters to Data Memory
 *
 * Writes Design Capacity, Design Energy, Terminate Voltage, and Taper Rate
 * to the State subclass (0x52). Must be called inside CONFIG UPDATE mode.
 *
 * @return 0 on success, negative error code on failure
 */
static int fg_write_battery_params(void) {
  int ret;

  /* Select subclass and block */
  ret = fg_write_byte(BQ27427_EXT_BLOCK_DATA_CTRL, BQ27427_STATE_BLOCK);
  if (ret) {
    printk("[BQ27427] Block data control write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to enable block access mode */

  ret = fg_write_byte(BQ27427_EXT_DATA_CLASS, BQ27427_SUBCLASS_STATE);
  if (ret) {
    printk("[BQ27427] Data class write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to load subclass into internal buffer */

  ret = fg_write_byte(BQ27427_EXT_DATA_BLOCK, BQ27427_STATE_BLOCK);
  if (ret) {
    printk("[BQ27427] Data block write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to populate block data buffer */

  /* Read current block */
  uint8_t block[FG_DATA_MEMORY_BLOCK_SIZE] = {0};
  uint8_t reg = BQ27427_EXT_BLOCK_DATA;
  ret = i2c_write_read(i2c_dev, BQ27427_I2C_ADDR, &reg, 1, block,
                       FG_DATA_MEMORY_BLOCK_SIZE);
  if (ret) {
    printk("[BQ27427] Battery params block read failed (%d)\n", ret);
    return ret;
  }

  /* Modify all params in local buffer (big-endian, MSB first) */
  block[6] = (BATTERY_DESIGN_CAPACITY >> 8) & 0xFF;
  block[7] = BATTERY_DESIGN_CAPACITY & 0xFF;
  block[8] = (BATTERY_DESIGN_ENERGY >> 8) & 0xFF;
  block[9] = BATTERY_DESIGN_ENERGY & 0xFF;
  block[10] = (BATTERY_TERMINATE_VOLTAGE >> 8) & 0xFF;
  block[11] = BATTERY_TERMINATE_VOLTAGE & 0xFF;
  block[21] = (BATTERY_TAPER_RATE >> 8) & 0xFF;
  block[22] = BATTERY_TAPER_RATE & 0xFF;
  block[27] = (BATTERY_TAPER_VOLTAGE >> 8) & 0xFF;
  block[28] = BATTERY_TAPER_VOLTAGE & 0xFF;
  /* Write full block back atomically */
  uint8_t wbuf[FG_DATA_MEMORY_WRITE_BUFFER_SIZE];
  wbuf[0] = BQ27427_EXT_BLOCK_DATA;
  memcpy(wbuf + 1, block, FG_DATA_MEMORY_BLOCK_SIZE);
  ret = i2c_write(i2c_dev, wbuf, FG_DATA_MEMORY_WRITE_BUFFER_SIZE,
                  BQ27427_I2C_ADDR);
  if (ret) {
    printk("[BQ27427] Battery params block write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(5); /* Allow gauge to commit block data to internal memory */

  /* Recompute full checksum */
  uint8_t csum = fg_checksum(block, FG_DATA_MEMORY_BLOCK_SIZE);
  ret = fg_write_byte(BQ27427_EXT_CHECKSUM, csum);
  if (ret) {
    printk("[BQ27427] Checksum write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(5); /* Allow gauge to verify and store checksum before readback */

  printk("[BQ27427] Battery params written OK "
         "(Cap=%dmAh, Energy=%dmWh, TermV=%dmV, Taper=%d)\n",
         BATTERY_DESIGN_CAPACITY, BATTERY_DESIGN_ENERGY,
         BATTERY_TERMINATE_VOLTAGE, BATTERY_TAPER_RATE);
  return 0;
}

/**
 * @brief Signal battery insertion to start Impedance Track gauging
 *
 * Sends BAT_REMOVE then BAT_INSERT to force fresh OCV measurement.
 * Polls BAT_DET flag for up to 1 second to confirm normal mode.
 *
 * @return 0 on success (BAT_DET=1), -ETIMEDOUT if timeout
 */
static int fg_signal_bat_insert(void) {
  int ret;

  /*
   * BAT_REMOVE first: resets gauge detection state.
   * Forces fresh OCV measurement on BAT_INSERT.
   * Prevents stale OCV from previous session causing
   * wrong SOC on reboot with charger connected.
   */
  ret = fg_control(BQ27427_CTRL_BAT_REMOVE, BQ27427_CMD_NULL);
  if (ret) {
    printk("[BQ27427] BAT_REMOVE command failed (%d)\n", ret);
    return ret;
  }
  k_msleep(5); /* Allow gauge to process BAT_REMOVE before BAT_INSERT */

  ret = fg_control(BQ27427_CTRL_BAT_INSERT, BQ27427_CMD_NULL);
  if (ret) {
    printk("[BQ27427] BAT_INSERT command failed (%d)\n", ret);
    return ret;
  }
  printk("[BQ27427] BAT_INSERT sent — polling BAT_DET...\n");
  /*
   * Poll BAT_DET flag for up to 1 second (SAMPLES_COUNT = 10 iterations)
   * Each iteration: read FLAGS register + 100ms delay
   * The gauge typically sets BAT_DET within 100-300ms after BAT_INSERT.
   */
  for (int i = 0; i < SAMPLES_COUNT; i++) {
    uint16_t flags = 0;
    ret = fg_read_word(BQ27427_CMD_FLAGS, &flags);
    if (ret) {
      printk("[BQ27427] FLAGS read error (%d)\n", ret);
      return ret;
    }
    if (flags & BQ27427_FLAG_BAT_DET) {
      printk("[BQ27427] BAT_DET=1 — NORMAL mode\n");
      return 0;
    }
    k_msleep(50);
  }
  printk("[BQ27427] WARNING: BAT_DET still 0 after 1 s\n");
  return -ETIMEDOUT;
}

/**
 * @brief Fix the CCGain sign bit to correct reversed current direction
 *
 * This function addresses a hardware design issue where the current sense
 * resistor is physically reversed (IN+ and IN- swapped), causing the
 * Coulomb counter gain to have the wrong polarity.
 *
 */
static int fg_fix_ccgain_tracked(void) {
  int ret;

  ret = fg_write_byte(BQ27427_EXT_BLOCK_DATA_CTRL, BQ27427_CCGAIN_BLOCK);
  if (ret) {
    printk("[BQ27427] Block data control write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to enable block access mode */

  ret = fg_write_byte(BQ27427_EXT_DATA_CLASS, BQ27427_SUBCLASS_CCGAIN);
  if (ret) {
    printk("[BQ27427] Data class write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to load subclass into internal buffer */

  ret = fg_write_byte(BQ27427_EXT_DATA_BLOCK, BQ27427_CCGAIN_BLOCK);
  if (ret) {
    printk("[BQ27427] Data block write failed (%d)\n", ret);
    return ret;
  }
  k_msleep(1); /* Allow gauge to populate block data buffer after selecting
                  block 0 */

  uint8_t block[FG_DATA_MEMORY_BLOCK_SIZE] = {0};
  uint8_t reg = BQ27427_EXT_BLOCK_DATA;
  ret = i2c_write_read(i2c_dev, BQ27427_I2C_ADDR, &reg, 1, block,
                       FG_DATA_MEMORY_BLOCK_SIZE);
  if (ret) {
    printk("[BQ27427] CCGain block read failed (%d)\n", ret);
    return ret;
  }

  printk("[BQ27427] CCGain byte[5] = 0x%02X\n", block[BQ27427_CCGAIN_OFFSET]);

  /* Already correct — nothing to do */
  if (!(block[BQ27427_CCGAIN_OFFSET] & BQ27427_CCGAIN_SIGN_BIT)) {
    printk("[BQ27427] CCGain correct — no fix needed\n");
    return 0;
  }

  /* Read old checksum */
  uint8_t old_csum = 0;
  ret = fg_read_byte(BQ27427_EXT_CHECKSUM, &old_csum);
  if (ret) {
    printk("[BQ27427] Old checksum read failed (%d)\n", ret);
    return ret;
  }

  uint8_t old_byte = block[BQ27427_CCGAIN_OFFSET];

  /* Clear sign bit */
  block[BQ27427_CCGAIN_OFFSET] &= ~BQ27427_CCGAIN_SIGN_BIT;
  printk("[BQ27427] CCGain fix: 0x%02X → 0x%02X\n", old_byte,
         block[BQ27427_CCGAIN_OFFSET]);

  /* Write modified block */
  uint8_t wbuf[FG_DATA_MEMORY_WRITE_BUFFER_SIZE];
  wbuf[0] = BQ27427_EXT_BLOCK_DATA;
  memcpy(wbuf + 1, block, FG_DATA_MEMORY_BLOCK_SIZE);
  ret = i2c_write(i2c_dev, wbuf, FG_DATA_MEMORY_WRITE_BUFFER_SIZE,
                  BQ27427_I2C_ADDR);
  if (ret) {
    printk("[BQ27427] CCGain block write failed (%d)\n", ret);
    return ret;
  }

  /* Incremental checksum update */
  uint8_t new_csum =
      0xFF -
      ((uint8_t)((0xFF - old_csum) - old_byte + block[BQ27427_CCGAIN_OFFSET]));

  ret = fg_write_byte(BQ27427_EXT_CHECKSUM, new_csum);
  if (ret) {
    printk("[BQ27427] Checksum write failed (%d)\n", ret);
    return ret;
  }

  printk("[BQ27427] CCGain fixed OK\n");
  return 0;
}

/**
 * @brief Initialize BQ27427 fuel gauge with full configuration
 *
 * Complete initialization sequence for BQ27427 fuel gauge:
 * 1. Verify device identity (DEVICE_TYPE = 0x0427)
 * 2. Unseal the device for configuration access
 * 3. Read ITPOR flag to determine if config was lost
 * 4. Set Chem ID to 0x1202 (4.2V Li-ion) if needed
 *
 * If ITPOR=1 (config lost):
 * 5. Enter CONFIG UPDATE mode
 * 6. Write battery params
 * 7. Fix CCGain sign bit
 * 8. Exit CONFIG UPDATE via SOFT_RESET
 * 9. Signal battery insertion to start gauging
 * 10. Send SMOOTH_SYNC to synchronize filtered capacity
 *
 * If ITPOR=0 (config retained):
 * Skip 5-10 and print gauge already initialized
 *
 * @return 0 on success, negative error code on failure
 */
int fuel_gauge_init(void) {
  uint16_t device_type = 0;
  uint16_t fw_version = 0;
  uint16_t flags = 0;
  int ret = 0;

  if (!device_is_ready(i2c_dev)) {
    printk("I2C device not ready\n");
    return -ENODEV;
  }
  /* STEP 1: Verify device */
  ret = fg_control(BQ27427_CTRL_DEVICE_TYPE, &device_type);
  if (ret) {
    printk("[BQ27427] I2C failed\n");
    return ret;
  }
  if (device_type != BQ27427_DEVICE_TYPE) {
    printk("[BQ27427] Wrong Device type\n");
    return -ENODEV;
  }
  ret = fg_control(BQ27427_CTRL_FW_VERSION, &fw_version);
  if (ret) {
    printk("[BQ27427] CTRL_FW_VERSION failed\n");
    return ret;
  }
  printk("[BQ27427] Device=0x%04X  FW=0x%04X\n", device_type, fw_version);

  /* STEP 2: Unseal */
  ret = fg_unseal();
  if (ret) {
    printk("[BQ27427] Unseal failed\n");
    return ret;
  }

  /* STEP 3: Read ITPOR */
  ret = fg_read_word(BQ27427_CMD_FLAGS, &flags);
  if (ret) {
    printk("[BQ27427] FLAGS read failed\n");
    return ret;
  }
  bool itpor = (flags & BQ27427_FLAG_ITPOR) != 0;
  printk("[BQ27427] FLAGS=0x%04X  ITPOR=%d\n", flags, itpor ? 1 : 0);

  /* STEP 4: Fix chem ID if wrong */
  ret = fg_set_chem_id_1202();
  if (ret)
    printk("[BQ27427] Chem ID fix failed (continuing)\n");

  if (itpor) {
    printk("[BQ27427] writing full config\n");

    ret = fg_enter_config_update();
    if (ret) {
      printk("[BQ27427] config_update read failed\n");
      return ret;
    }

    /* Write battery params — log failure but continue */
    ret = fg_write_battery_params();
    if (ret) {
      printk("[BQ27427] Battery params failed — continuing to CCGain\n");
    }

    /* Fix CCGain — attempt even if params failed (ITPOR=1)*/
    ret = fg_fix_ccgain_tracked();
    if (ret)
      printk("[BQ27427] CCGain fix failed\n");

    /* exit CONFIG UPDATE */
    ret = fg_exit_config_update();
    if (ret) {
      printk("[BQ27427] config_update failed\n");
      return ret;
    }

    ret = fg_signal_bat_insert();
    if (ret)
      printk("[BQ27427] BAT_INSERT timeout\n");
    /*Allow gauge to stabilize after BAT_INSERT
     *(BAT_DET typically sets within 100-200ms)
     */
    k_msleep(100);
    ret = fg_control(BQ27427_CTRL_SMOOTH_SYNC, BQ27427_CMD_NULL);
    if (ret) {
      printk("[BQ27427] SMOOTH_SYNC failed %d\n", ret);
    }
    printk("[BQ27427] SMOOTH_SYNC sent\n");
  } else {
    printk("[BQ27427] Already Initialized\n");
  }
  return 0;
}
/**
 * @brief Get current battery State of Charge (SOC) percentage and print status
 *
 * Reads the StateOfCharge() register and FLAGS register from the fuel gauge.
 * Returns the battery percentage (0-100%) and prints whether the battery
 * is fully charged based on the FC (Full Charge) flag.
 *
 * @return Current SOC percentage (0-100) on success, negative error code on
 * failure
 */
int fuel_gauge_get_soc(void) {
  uint16_t soc_pct = 0;
  int ret = 0;
  ret |= fg_read_word(BQ27427_CMD_SOC, &soc_pct);

  if (ret) {
    printk("[BQ27427] Read error (%d)\n", ret);
    return -1;
  }
  return soc_pct;
}

/**
 * @brief Initialize fuel gauge interrupt (GPOUT/SOC_INT pin)
 *
 * Configures the fuel gauge GPOUT pin as an input with edge-to-active
 * interrupt. The BQ27427 generates  pulse on this pin when SOC changes
 *
 * @return 0 on success, negative error code on failure
 */
int fuel_gauge_isr_init(void) {
  int ret;

  if (!device_is_ready(fg_int.port)) {
    printk("Interrupt GPIO not ready\n");
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(&fg_int, GPIO_INPUT);
  if (ret) {
    printk("Failed to configure FG interrupt GPIO (%d)\n", ret);
    return ret;
  }

  ret = gpio_pin_interrupt_configure_dt(&fg_int, GPIO_INT_EDGE_TO_ACTIVE);
  if (ret) {
    printk("Failed to configure FG interrupt (%d)\n", ret);
    return ret;
  }

  gpio_init_callback(&fg_int_cb, fg_int_handler, BIT(fg_int.pin));

  ret = gpio_add_callback(fg_int.port, &fg_int_cb);
  if (ret) {
    printk("Failed to add FG interrupt callback (%d)\n", ret);
    return ret;
  }

  printk("Fuel gauge interrupt initialized\n");
  return 0;
}