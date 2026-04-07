#include "akd_spi_flash_handler.h"
#include "akd_spi_flash.h"
#include "akida.h"
#include "akida/hardware_device.h"
extern "C" {
#include "ble_services/file_transfer.h"
}

#include "io_objects.h"
#include <akd1500/akd1500_spi_driver.h>
#include <hardware_device_impl.h>
#include <zephyr/types.h>

#if FLASH_READ_BACK_CHECK
uint8_t read_back_flash[HALF_OF_SRAM_BUFFER_SIZE];
#endif

union _data {
  uint8_t ucdata[4];
  uint32_t uint_data;
};

uint32_t flash_offsets[] = {AKD_FLASH_OFFSET,
                            AKD_FLASH_OFFSET + AKD_MODEL_OFFSET};
int app_index = -1;

void akida_spiflash_init(void) {
  static uint8_t read_data[READ_LEN];
  /* Set MCU as SPI-Master */
  akida_config_spi(1);
  /* read Akida Device ID */
  akd1500.read(0xFCC00000, read_data, READ_LEN);
  printk("Akida Device ID: \n");
  for (int i = 0; i < READ_LEN; i += WORD_SIZE) {
    printk("Word %d: 0x%02X%02X%02X%02X\n", i / WORD_SIZE, read_data[i],
           read_data[i + 1], read_data[i + 2], read_data[i + 3]);
  }

  uint8_t msg[SRAM_128_BYTES_LEN] =
      "Hello world!!! This is a test for writing and reading 128 bytes of data "
      "to and from 1MB of RAM within Brainchip's AKD1500 chip";
  uint8_t sram_read_data[SRAM_128_BYTES_LEN] = "kkkkkkkkk";
  uint32_t fail_cnt = 0;

  // Write and read 1 MB RAM in Akida in 128-byte chunks (Currently it is tested
  // for 128 Bytes). To check complete 1MB replace offset < 1 with offset <
  // ONE_MB in the below for loop

  for (uint32_t offset = 0; offset < 1; offset += SRAM_128_BYTES_LEN) {
    uint32_t addr = ONE_MB_SRAM_ADDR + offset;

    // Write
    akd1500.write(addr, msg, SRAM_128_BYTES_LEN);

    akd1500.read(addr, sram_read_data, SRAM_128_BYTES_LEN);

    // Compare
    if (memcmp(msg, sram_read_data, SRAM_128_BYTES_LEN) != 0) {
      fail_cnt++;
      printk("Data mismatch at address 0x%08X (offset: %u bytes)\n", addr,
             offset);
      for (int i = 0; i < SRAM_128_BYTES_LEN; ++i) {
        printk("%c", sram_read_data[i]);
      }
    }
    // Clear the read data
    memset(sram_read_data, 0, SRAM_128_BYTES_LEN);
  }
  if (fail_cnt == 0) {
    printk("Sanity test of 1 MB SRAM is passed\n");
  } else {
    printk("Sanity test of 1 MB SRAM is failed\n");
  }

  /* Initialize the AKD1500 SPI-Flash functionality */
  init_akd_1500_spi_flash();

  /* get akida device version */
  auto hw_version = akida::read_hw_version(akd1500);
  printk("Device Version: v%u.%u\n", hw_version.major_rev,
         hw_version.minor_rev);

  spi_flash_read_id(spi_driver); // read SPI-Flash id
}
/* function to enable external host MCU/AKD1500 as SPI master for 16 MB flash */
void akida_config_spi(bool is_mcu_master) {
  union _data rw_data;
  akd1500.read(CONFIG_AKD1500_CTRL, rw_data.ucdata, 4);
  if (is_mcu_master) {
    // configure external host (MCU) as SPI master for 16 MB flash
    rw_data.uint_data = rw_data.uint_data | EN_SPI_S2M;
    akd1500.write(CONFIG_AKD1500_CTRL, rw_data.ucdata, 4);
    printk("Enabling external host as SPI master\r\n");
  } else {
    // configure AKD1500 as SPI master for 16 MB flash
    rw_data.uint_data = rw_data.uint_data & (~EN_SPI_S2M);
    akd1500.write(CONFIG_AKD1500_CTRL, rw_data.ucdata, 4);
    printk("Enabling AKD1500 as SPI master\r\n");
  }
}

/* configure the spi-flash driver settings */
void init_akd_1500_spi_flash() {
  union _data rw_data;
  akd1500.read(0xfce00010, rw_data.ucdata, 4);
  uint32_t reg = rw_data.uint_data;
  akd1500.read(0xfce00018, rw_data.ucdata, 4);
  rw_data.uint_data |=
      1 << 21 | 1 << 16 | 1 << 17; /* switch endianness on spi master */
  akd1500.write(0xfce00018, rw_data.ucdata, 4);

  int64_t delay = time_ms() + 100;
  while (time_ms() < delay)
    ;

  /* Configure spi flash */
  rw_data.uint_data = 0;
  akd1500.write(0xfcf20008, rw_data.ucdata, 4);
  rw_data.uint_data = 0x8080001f;
  akd1500.write(0xfcf20000, rw_data.ucdata, 4);
  rw_data.uint_data = 1;
  akd1500.write(0xfcf20010, rw_data.ucdata, 4);
  rw_data.uint_data = 8;
  akd1500.write(0xfcf20014, rw_data.ucdata, 4); /* BAUD */
  rw_data.uint_data = 0x08100200 | 1 << 20 | 10 << 11 | 6 << 2 | 1;
  akd1500.write(0xfcf200f4, rw_data.ucdata, 4);
  rw_data.uint_data = 0;
  akd1500.write(0xfcf200f8, rw_data.ucdata, 4);
  rw_data.uint_data = 0xcc;
  akd1500.write(0xfcf200fc, rw_data.ucdata, 4);
  rw_data.uint_data = 0xeb;
  akd1500.write(0xfcf20100, rw_data.ucdata, 4);
  rw_data.uint_data = 0x1;
  akd1500.write(0xfcf20008, rw_data.ucdata, 4);
  akd1500.read(0xfce00018, rw_data.ucdata, 4);
  printk("Akida1500 SPI Flash initialized on %x %x\n", reg, rw_data.uint_data);
}

/* helper function to read from SPI flash – used by file_transfer.c for CRC */
extern "C" void spi_flash_read_helper_func(uint8_t *buf, uint32_t offset,
                                           uint32_t size) {
  if (!buf || size == 0) {
    return;
  }
  akida_config_spi(1);
  int ret = spi_flash_read(spi_driver, offset, buf, size);
  akida_config_spi(0);
  if (ret != 0) {
    printk("spi_flash_read_helper: read failed at 0x%x size=%u (err %d)\n",
           offset, size, ret);
  }
}

/* helper function to invoke flash erase API calls */
extern "C" int spi_flash_erase_helper_func(uint32_t offset, uint32_t size) {
  if (size == 0 || size > (FLASH_MAX_16_MB_SIZE - offset)) {
    printk("Invalid size. Must be > 0 and <= %d\n",
           (FLASH_MAX_16_MB_SIZE - offset));
    return 1;
  }
  akida_config_spi(1);

  uint64_t s_tick = 0;
  ;
  uint64_t e_tick = 0;
  uint32_t erase_time = 0;

  printk("Flase erase offset %x and size = %d bytes\n", offset, size);

  s_tick = time_ms();
  int ret = spi_flash_erase(spi_driver, offset, size);
  e_tick = time_ms();
  erase_time = e_tick - s_tick;
  printk("\n\rerase time= %u ms\n\r", erase_time);
  if (ret) {
    printk("Erase failed\n");
  } else {
    printk("Erase Successful\n");
  }

  akida_config_spi(0);

  return ret;
}

/* helper function to invoke spi-flash write driver API for the given data,
 * offset, and size */
extern "C" void spi_flash_write_helper_func(const uint8_t *data, size_t offset,
                                            size_t size) {
  akida_config_spi(1);

  uint32_t flash_addr = offset;

  uint64_t s_write = 0;
  ;
  uint64_t e_write = 0;
  uint32_t write_time = 0;

  s_write = time_ms();
  int ret = spi_flash_write(spi_driver, flash_addr, data, size);
  if (ret != 0) {
    printk("Flash write failed with error %d\n", ret);
    printk("Flash Address = 0x%x, size = %d", flash_addr, size);
    return;
  }
  e_write = time_ms();
  write_time = e_write - s_write;
  printk("\n\rwrite time = %u ms\n\r", write_time);
#if FLASH_READ_BACK_CHECK
  uint64_t s_read = 0;
  uint64_t e_read = 0;
  uint32_t read_time = 0;
  s_read = time_ms();
  // Read back from flash into second half of data[]
  ret = spi_flash_read(spi_driver, flash_addr, read_back_flash, size);
  if (ret != 0) {
    printk("Flash read failed with error %d\n", ret);
    return;
  }

  // Compare the written and read data
  if (memcmp(data, read_back_flash, size) == 0) {
    printk("Flash write-read verification successful.\n");
  } else {
    printk("Flash data mismatch!\n");
  }
  e_read = time_ms();
  read_time = e_read - s_read;
  printk("\n\rread_time= %u ms\n\r", read_time);
#endif
  if (!ret)
    printk("Flash Write Successful\n");
  akida_config_spi(0);
}
