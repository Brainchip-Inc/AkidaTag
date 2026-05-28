#include "akd_spi_flash.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(AKD_SPI_FLASH, LOG_LEVEL_DBG);

#define FLASH_CMD_WR_ENABLE (0x6)
#define FLASH_CMD_WR (0x2)
#define FLASH_CMD_RD (0x3)
#define FLASH_CMD_4KB_ERASE (0x20)
#define FLASH_CMD_32KB_ERASE (0x52)
#define FLASH_CMD_64KB_ERASE (0xD8)
#define FLASH_CMD_RD_STAT_REG (0x5)
#define FLASH_CMD_WHOLE_FLASH_ERASE (0xC7) // if needed

/* Micron MT25Q-specific flag-status register commands */
#define CMD_CLEAR_FLAG_STATUS_REG (0x50)
#define CMD_READ_FLAG_STATUS_REG (0x70)

/* Winbond / GigaDevice Status Register-2 (carries QE bit) */
#define CMD_READ_STATUS_REG_2 (0x35)
#define CMD_WRITE_STATUS_REG_2 (0x31)
#define SR2_QE_BIT (1 << 1)

#define CMD_READ_ID (0x9F)

#define PAGE_SIZE 256 // Flash page size

#define SPI_FLASH_SECTOR_TYPE_4KB 0x0c

static spi_flash_vendor_t g_vendor = SPI_FLASH_VENDOR_UNKNOWN;

spi_flash_vendor_t spi_flash_get_vendor(void) { return g_vendor; }

static spi_flash_vendor_t vendor_from_id(uint32_t device_id) {
  /* JEDEC ID layout in the uint32 (little endian read of 3 bytes from
   * spiflashread): byte 0 = manufacturer ID. */
  switch (device_id & 0xFF) {
  case 0x20:
    return SPI_FLASH_VENDOR_MICRON;
  case 0xEF:
    return SPI_FLASH_VENDOR_WINBOND;
  case 0xC8:
    return SPI_FLASH_VENDOR_GIGADEVICE;
  default:
    return SPI_FLASH_VENDOR_UNKNOWN;
  }
}

static int spi_flash_clear_flag(akida::ZephyrSpiDriver spi_flash_driver_) {
  /* Flag Status Register is Micron-specific. On Winbond/GigaDevice opcode 0x50
   * means "Volatile SR Write Enable" with very different semantics, so skip. */
  if (g_vendor != SPI_FLASH_VENDOR_MICRON) {
    return 0;
  }
  uint8_t cmd = CMD_CLEAR_FLAG_STATUS_REG;
  spi_flash_driver_.spiflashwrite(0, &cmd, 1);
  return 0;
}

static int spi_flash_write_enable(akida::ZephyrSpiDriver spi_flash_driver_) {
  uint8_t cmd = FLASH_CMD_WR_ENABLE;
  spi_flash_driver_.spiflashwrite(0, &cmd, 1);
  return 0;
}

static int spi_flash_wait_until_ready(akida::ZephyrSpiDriver spi_flash_driver_,
                                      uint32_t timeout_ms = 1000) {
  uint8_t cmd = FLASH_CMD_RD_STAT_REG;
  uint8_t status = 0;
  uint32_t elapsed_ms = 0;
  const uint32_t poll_interval_ms = 10;
  uint32_t tx_rx_len = ((1 << 16) | 1);

  while (elapsed_ms < timeout_ms) {

    spi_flash_driver_.spiflashread(0, &cmd, &status, tx_rx_len);

    if ((status & 0x01) == 0) {
      return 0; // Ready
    }
    k_sleep(K_MSEC(poll_interval_ms));
    elapsed_ms += poll_interval_ms;
  }

  return -ETIMEDOUT;
}

static uint8_t
spi_flash_read_flag_status(akida::ZephyrSpiDriver spi_flash_driver_) {
  /* On non-Micron parts opcode 0x70 returns SR3 (different bit layout) and the
   * erase/program error bits this code checks for don't exist there. Return 0
   * so the caller's bit-tests treat the operation as successful. */
  if (g_vendor != SPI_FLASH_VENDOR_MICRON) {
    return 0;
  }
  uint8_t cmd = CMD_READ_FLAG_STATUS_REG;
  uint32_t status = 0;
  uint32_t tx_rx_len = ((1 << 16) | 1);

  spi_flash_driver_.spiflashread(0, &cmd, (uint8_t *)&status, tx_rx_len);

  return status;
}

uint32_t spi_flash_read_id(akida::ZephyrSpiDriver spi_flash_driver_) {

  uint8_t cmd = CMD_READ_ID;
  uint32_t device_id = 0;
  uint32_t tx_rx_len = ((1 << 16) | 4);

  spi_flash_driver_.spiflashread(0, &cmd, (uint8_t *)&device_id, tx_rx_len);
  g_vendor = vendor_from_id(device_id);
  const char *vendor_name = "Unknown";
  switch (g_vendor) {
  case SPI_FLASH_VENDOR_MICRON:
    vendor_name = "Micron";
    break;
  case SPI_FLASH_VENDOR_WINBOND:
    vendor_name = "Winbond";
    break;
  case SPI_FLASH_VENDOR_GIGADEVICE:
    vendor_name = "GigaDevice";
    break;
  default:
    break;
  }
  LOG_PRINTK("AKD_SPI_FLASH: Serial Flash Device ID 0x%x (%s)\n", device_id,
             vendor_name);
  return device_id;
}

typedef enum {
  SECTOR_TYPE_4KB,
  SECTOR_TYPE_32KB,
  SECTOR_TYPE_64KB
} spi_flash_sector_type_t;

static uint32_t get_sector_size(spi_flash_sector_type_t type) {
  switch (type) {
  case SECTOR_TYPE_4KB:
    return 4 * 1024;
  case SECTOR_TYPE_32KB:
    return 32 * 1024;
  case SECTOR_TYPE_64KB:
    return 64 * 1024;
  default:
    return 0;
  }
}

int spi_flash_erase_api(akida::ZephyrSpiDriver spi_flash_driver_,
                        uint32_t sector_id, uint32_t no_of_sectors,
                        spi_flash_sector_type_t sector_type) {
  uint32_t sector_size = get_sector_size(sector_type);
  if (sector_size == 0) {
    return -EINVAL;
  }
  spi_flash_clear_flag(spi_flash_driver_);

  int ret;
  for (uint32_t i = 0; i < no_of_sectors; i++) {
    uint32_t address = sector_id * sector_size + i * sector_size;

    ret = spi_flash_write_enable(spi_flash_driver_);
    if (ret != 0)
      return ret;

    uint8_t cmd_buf[4];
    switch (sector_type) {
    case SECTOR_TYPE_4KB:
      cmd_buf[0] = FLASH_CMD_4KB_ERASE;
      break;
    case SECTOR_TYPE_32KB:
      cmd_buf[0] = FLASH_CMD_32KB_ERASE;
      break;
    case SECTOR_TYPE_64KB:
      cmd_buf[0] = FLASH_CMD_64KB_ERASE;
      break;
    default:
      return -EINVAL;
    }

    cmd_buf[1] = (address >> 16) & 0xFF;
    cmd_buf[2] = (address >> 8) & 0xFF;
    cmd_buf[3] = address & 0xFF;

    spi_flash_driver_.spiflashwrite(0, cmd_buf, 4);

    ret = spi_flash_wait_until_ready(spi_flash_driver_);
    if (ret != 0) {
      LOG_ERR("AKD_SPI_FLASH: Error in erase operation\n");
      return ret;
    }
    ret = spi_flash_read_flag_status(spi_flash_driver_);
    if ((ret & 0x20) == 0x20) {
      LOG_ERR("AKD_SPI_FLASH: read_flag_status error in erase operation, error "
              "value %x\n",
              ret);
      return ret;
    }
  }

  return 0;
}

int spi_flash_erase(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address,
                    uint32_t size) {
  uint32_t sector_size = 1 << SPI_FLASH_SECTOR_TYPE_4KB;
  int s_sector = address / sector_size;
  int e_sector = (address + size - 1) / sector_size;
  int count = e_sector - s_sector + 1;
  return spi_flash_erase_api(spi_flash_driver_, s_sector, count,
                             SECTOR_TYPE_4KB);
}

int spi_flash_write(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address,
                    const uint8_t *data, size_t length) {
  size_t offset = 0;
  spi_flash_clear_flag(spi_flash_driver_);

  while (offset < length) {
    size_t page_offset = address % PAGE_SIZE;
    size_t chunk = PAGE_SIZE - page_offset;
    if (chunk > (length - offset)) {
      chunk = length - offset;
    }

    int ret = spi_flash_write_enable(spi_flash_driver_);
    if (ret != 0)
      return ret;

    uint8_t cmd_buf[4 + PAGE_SIZE] = {0};
    cmd_buf[0] = FLASH_CMD_WR;
    cmd_buf[1] = (address >> 16) & 0xFF;
    cmd_buf[2] = (address >> 8) & 0xFF;
    cmd_buf[3] = address & 0xFF;

    memcpy(&cmd_buf[4], &data[offset], chunk);

    spi_flash_driver_.spiflashwrite(0, cmd_buf, 4 + chunk);

    ret = spi_flash_wait_until_ready(spi_flash_driver_);
    if (ret != 0) {
      LOG_ERR("AKD_SPI_FLASH: Error in write operation");
      return ret;
    }
    ret = spi_flash_read_flag_status(spi_flash_driver_);
    if ((ret & 0x10) == 0x10) {
      LOG_ERR("AKD_SPI_FLASH: read_flag_status error in flash write operation");
      return ret;
    }
    address += chunk;
    offset += chunk;
  }

  return 0;
}

int spi_flash_read(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address,
                   uint8_t *data, size_t length) {
  size_t offset = 0;

  while (offset < length) {
    size_t page_offset = address % PAGE_SIZE;
    size_t chunk = PAGE_SIZE - page_offset;

    if (chunk > (length - offset)) {
      chunk = length - offset;
    }

    uint8_t cmd_buf[5];
    cmd_buf[0] = FLASH_CMD_RD;
    cmd_buf[1] = (address >> 16) & 0xFF;
    cmd_buf[2] = (address >> 8) & 0xFF;
    cmd_buf[3] = address & 0xFF;
    cmd_buf[4] = 0xFF;

    uint32_t tx_rx_len = (4 << 16) | chunk;

    spi_flash_driver_.spiflashread(0, cmd_buf, &data[offset], tx_rx_len);
    address += chunk;
    offset += chunk;
  }

  return 0;
}

int spi_flash_init_quad_mode(akida::ZephyrSpiDriver spi_flash_driver_) {
  /* Required for Winbond / GigaDevice parts: AKD1500 reads model data via
   * Quad I/O Fast Read (0xEB) and the chip will ignore quad commands unless
   * the QE bit (Status Register-2 bit 1) is set. Factory default is 0 on these
   * vendors. Micron parts use a different mechanism that's already configured
   * by default, so this is a no-op there. */
  if (g_vendor != SPI_FLASH_VENDOR_WINBOND &&
      g_vendor != SPI_FLASH_VENDOR_GIGADEVICE) {
    return 0;
  }

  uint8_t cmd = CMD_READ_STATUS_REG_2;
  uint32_t sr2 = 0;
  uint32_t tx_rx_len = ((1 << 16) | 1);
  spi_flash_driver_.spiflashread(0, &cmd, (uint8_t *)&sr2, tx_rx_len);

  if (sr2 & SR2_QE_BIT) {
    LOG_PRINTK("AKD_SPI_FLASH: QE bit already set (SR2=0x%02x)\n",
               (unsigned)(sr2 & 0xFF));
    return 0;
  }

  int ret = spi_flash_write_enable(spi_flash_driver_);
  if (ret != 0) {
    LOG_ERR("AKD_SPI_FLASH: write enable failed before QE write (%d)\n", ret);
    return ret;
  }

  uint8_t buf[2] = {CMD_WRITE_STATUS_REG_2,
                    (uint8_t)((sr2 & 0xFF) | SR2_QE_BIT)};
  spi_flash_driver_.spiflashwrite(0, buf, sizeof(buf));

  /* SR write takes up to ~15 ms; reuse the standard WIP poll. */
  ret = spi_flash_wait_until_ready(spi_flash_driver_);
  if (ret != 0) {
    LOG_ERR("AKD_SPI_FLASH: QE write timed out (%d)\n", ret);
    return ret;
  }

  /* Verify */
  sr2 = 0;
  spi_flash_driver_.spiflashread(0, &cmd, (uint8_t *)&sr2, tx_rx_len);
  if (!(sr2 & SR2_QE_BIT)) {
    LOG_ERR("AKD_SPI_FLASH: QE bit not latched (SR2=0x%02x)\n",
            (unsigned)(sr2 & 0xFF));
    return -EIO;
  }
  LOG_PRINTK("AKD_SPI_FLASH: QE bit enabled (SR2=0x%02x)\n",
             (unsigned)(sr2 & 0xFF));
  return 0;
}