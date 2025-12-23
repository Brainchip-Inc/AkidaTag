/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>


#include "akida/hardware_device.h"
#include "sample_input/mnist/mnist_inputs.h"
#include "sample_input/kws/kws_inputs.h"
#include "nrf_spi.h"
#include "mnist/mnist_program_info.h"
#include "kws/kws_program_info.h"
#include "akd_spi_flash.h"
#include <akd1500/akd1500_spi_driver.h>
#include <cmath>
#include <hardware_device_impl.h>
#include <infra/system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "io_objects.h"
#include "akd_spi_flash_handler.h"


#ifdef __cplusplus
extern "C" {
#endif
#include "littlefs_storage.h"
#include "boot_manager.h"
#include "ble_services/ble_initialization.h"
#include "ble_services/file_transfer.h"

#ifdef __cplusplus
}
#endif




/*
FLash offset indices
KWS - 1
MNIST - 0
*/

#define VALID_PROGRAM_DATA_MNIST  0xD8130700
#define VALID_PROGRAM_DATA_KWS 0x64d70000


const unsigned char* inputs[] = {mnist_inputs, kws_inputs};
uint32_t valid_program_data[] = {VALID_PROGRAM_DATA_MNIST, VALID_PROGRAM_DATA_KWS};
const unsigned char* program_info[] = {mnist_program_info, kws_program_info};
const int64_t program_info_len[] = {mnist_program_info_len, kws_program_info_len};



const struct device *wdt_dev; // Global watchdog device
void kick_watchdog(void) {
  if (wdt_dev) {
    // wdt_feed(wdt_dev, 0); // Feed the watchdog
    printk("Watchdog fed\n");
  }
}

/* helper function to swap the endianness */
uint32_t swap_endian(uint32_t value) {
  return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) |
         ((value << 8) & 0x00FF0000) | ((value << 24) & 0xFF000000);
}

/* function to read 1st 4 bytes of model data from the flash_offsets[app_index]
 * and validate with the VALID_PROGRAM_DATA, once model is uploaded to the
 * spi-flash via external applications (BLE or Jlink)  */
bool check_program_data(int offset, int len, int app_index_l) {
  uint32_t data;
  spi_flash_read(spi_driver, offset, (uint8_t *)&data, 4);
  if (swap_endian(data) == valid_program_data[app_index_l])
  {
	printk("program data @%x: %x is same as the expected one\n", offset,  valid_program_data[app_index_l]);
    return true;
  }
  printk("program data @%x: %x is not the expected one\n", offset, data);
  return false;
}



//int spi_flash_erase_helper_func(uint32_t offset, uint32_t size);


int64_t time_ms() { return k_uptime_get(); }

void msleep(uint32_t duration) { k_sleep(K_MSEC(duration)); }

void panic(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  exit(EXIT_FAILURE);
}


int post_processing(auto out, const int32_t *bytes_out, int app_index_l){
  if(app_index_l <= 1){
    int32_t max_val = bytes_out[0];
    int max_index = -1;
    for (int i = 0; i < (int)out->size(); i++) {
        if (bytes_out[i] > max_val) {
            max_val = bytes_out[i];
            max_index = i;
        }
    }
    return max_index;
  }
  else{
    return -1;
  }
}




int main(void)
{
	confirm_image_if_needed();
	file_transfer_init();
	ble_init();
	akida_spiflash_init();
	
    const struct device *qspi = DEVICE_DT_GET(DT_NODELABEL(mx25r64));

	if (!device_is_ready(qspi)) {
		printk("QSPI not ready\n");
	}
	else 
	{
		printk("QSPI device ready: %s \n", qspi->name);
	}
    int err = storage_init ();
	if (err != 0)
	{
		printk("LittleFS mount failed %d", err);
	}
	else
	{
		printk("LittleFS mount succeeded %d", err);
	}
	//test_create_file();    


	for (;;) {
		prcess_led();
	}
}


/* function to run the inference */
int infer(int app_index_l) {
  if(app_index_l > 1) {
    printk("Illegal model index %d\n", app_index_l);  
    return -1;
  }

  akida_config_spi(1);
  int ret = check_program_data(flash_offsets[app_index_l], 4, app_index_l);
  akida_config_spi(0);

  if (ret == false) {
	printk("model data, not present in SPI Flash, upload the model\n");
	return -1;
  } else {
  printk("model is already present \n");
// program the model info part to AKD1500

  printk("Programming the model\n");
  akida_program_info((uint8_t *)program_info[app_index_l], program_info_len[app_index_l],
			flash_offsets[app_index_l]);
  akd_device.set_batch_size(1, true);
  app_index = app_index_l;

  }

  akd_device.toggle_clock_counter(true);


  uint32_t inf_complete = 0;
  uint32_t s_dma_cycls = 0;
  uint64_t s_tick = 0;
  uint64_t e_tick = 0;
  uint32_t inf_time = 0;
  uint32_t e_dma_cycls = 0;
  uint32_t delta_cycle = 0;

auto shape = mnist_inputs_shape;

if(app_index_l == 1)
    shape = kws_inputs_shape;

  akida::TensorConstPtr in = akida::Dense::create_view(
      reinterpret_cast<const char *>(inputs[app_index_l]), akida::TensorType::uint8,
      {shape}, akida::Dense::Layout::RowMajor);

  s_dma_cycls = akd_device.read_clock_counter();
  s_tick = time_ms();
  auto inference_op = akd_device.forward({in});
  e_tick = time_ms();
  e_dma_cycls = akd_device.read_clock_counter();
  delta_cycle = e_dma_cycls - s_dma_cycls;
  inf_time = e_tick - s_tick;
  printk("\n\rinference time= %u dma cycles, time = %u ms\n\r", delta_cycle,
         inf_time);
  /** Get output buffer */
  auto out = akida::Tensor::ensure_dense(std::move(inference_op[0]));
  const int32_t *bytes_out = (int32_t *)out->buffer()->data();
  for (int i = 0; i < (int)out->size(); i++) {
    printk("Output-%d = %d\n", i, bytes_out[i]);
    inf_complete = 0xAA;
  }
  int class_id = post_processing(out, bytes_out, app_index_l);

  if(class_id == -1){
    return -1;
  }
  if(app_index_l == 0){ //mnist
    printk("Predicted Digit : %d\n", class_id);
  }
  else if (app_index_l == 1){  //kws
    printk("\nClass : %d\n", class_id);
    printk("Word : %s\n", kws_tags[class_id]);
  }

  if (inf_complete != 0xAA) {
    printk("Inference did not happen\n");
  } else {
    printk("APP Inference Completed\n");
  }
  return 0;
}

/* shell cli function to invoke infer function */
static int cmd_infer(const struct shell *shell, size_t argc, char **argv) {
  if (argc != 2) {
    printk("Usage: infer <string>");
    return -EINVAL;
  }

  char* string = argv[1];
  if(!strcmp(string, "kws"))
  {
      app_index = 1;
	  printk("inference kws requested, app index %d", app_index);
  }
  else if(!strcmp(string, "mnist"))
  {
      app_index = 0;
	  printk("inference mnist requested, app index %d", app_index);
  }
  else {
    printk("Illegal model inference request");
    return -EINVAL;
  }

  return infer(app_index);
}

/* shell cli function to set external host MCU/AKD1500 as SPI master */
static int cmd_set(const struct shell *shell, size_t argc, char **argv) {
  if (argc != 2) {
    printk("Usage: set <bool>");
    return -EINVAL;
  }
  size_t value = strtoul(argv[1], NULL, 0);

  printk("Value = %d", value);
  if (value != 0 && value != 1) {
    printk("Invalid <bool> value %d", value);
    return -EINVAL;
  }
  if (value == 1) {
    akida_config_spi(value);
    spi_flash_read_id(spi_driver);
  } else {
    akida_config_spi(value);
  }

  return 0;
}



/* shell cli function to invoke erase function */
static int cmd_full_erase(const struct shell *shell, size_t argc, char **argv) {

  if (spi_flash_erase_helper_func(0x1000, FLASH_MAX_16_MB_SIZE - 0x1000)) {
    return 1;
  }

  return 0;
}



SHELL_CMD_REGISTER(full_erase, NULL, "Erase flash: erase <size>", cmd_full_erase);
SHELL_CMD_REGISTER(
    set, NULL, "Set MCU/AKD1500 as SPI-Master: set <bool> (0:AKD1500 1:MCU)",
    cmd_set);
SHELL_CMD_REGISTER(infer, NULL, "Start the Inference: infer", cmd_infer);

