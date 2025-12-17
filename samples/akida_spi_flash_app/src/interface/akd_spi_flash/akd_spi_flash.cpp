#include "akd_spi_flash.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#define FLASH_CMD_WR_ENABLE (0x6)
#define FLASH_CMD_WR (0x2)
#define FLASH_CMD_RD (0x3)
#define FLASH_CMD_4KB_ERASE (0x20)
#define FLASH_CMD_32KB_ERASE (0x52)
#define FLASH_CMD_64KB_ERASE (0xD8)
#define FLASH_CMD_RD_STAT_REG (0x5)
#define FLASH_CMD_WHOLE_FLASH_ERASE (0xC7) // if needed

#define CMD_CLEAR_FLAG_STATUS_REG (0x50)
#define CMD_READ_FLAG_STATUS_REG (0x70)

#define CMD_READ_ID (0x9F)

#define PAGE_SIZE 256 // Flash page size

#define SPI_FLASH_SECTOR_TYPE_4KB 0x0c


static int spi_flash_clear_flag(akida::ZephyrSpiDriver spi_flash_driver_) {
  uint8_t cmd = CMD_CLEAR_FLAG_STATUS_REG;
  spi_flash_driver_.spiflashwrite(0, &cmd, 1);
  return 0;
}

static int spi_flash_write_enable(akida::ZephyrSpiDriver spi_flash_driver_) {
  uint8_t cmd = FLASH_CMD_WR_ENABLE;
  spi_flash_driver_.spiflashwrite(0, &cmd, 1);
  return 0;
}

static int spi_flash_wait_until_ready(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t timeout_ms = 1000) {
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

static uint8_t spi_flash_read_flag_status(akida::ZephyrSpiDriver spi_flash_driver_) {
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
  printf("Serial Flash Device ID 0x%x\n", device_id);
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

int spi_flash_erase_api(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t sector_id, uint32_t no_of_sectors,
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
      printf("Error in erase operation\n");
      return ret;
    }
    ret = spi_flash_read_flag_status(spi_flash_driver_);
    if ((ret & 0x20) == 0x20) {
      printf("read_flag_status error in erase operation, error value %x\n",
             ret);
      return ret;
    }
  }

  return 0;
}

int spi_flash_erase(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, uint32_t size) {
  uint32_t sector_size = 1 << SPI_FLASH_SECTOR_TYPE_4KB;
  int s_sector = address / sector_size;
  int e_sector = (address + size - 1) / sector_size;
  int count = e_sector - s_sector + 1;
  return spi_flash_erase_api(spi_flash_driver_, s_sector, count, SECTOR_TYPE_4KB);
}

int spi_flash_write(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, const uint8_t *data, size_t length) {
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
      printf("Error in write operation");
      return ret;
    }
    ret = spi_flash_read_flag_status(spi_flash_driver_);
    if ((ret & 0x10) == 0x10) {
      printf("read_flag_status error in flash write operation");
      return ret;
    }
    address += chunk;
    offset += chunk;
  }

  return 0;
}

int spi_flash_read(akida::ZephyrSpiDriver spi_flash_driver_, uint32_t address, uint8_t *data, size_t length) {
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

/*
int flashread(akida::ZephyrSpiDriver spi_flash_driver_, unsigned char *data, uint32_t addr, uint32_t len) {
  spi_flash_read(spi_flash_driver_, addr, data, len);
  return 0;
}*/
