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


#ifdef __cplusplus
extern "C" {
#endif

// Include BLE C headers
#include "ble_services/file_transfer.h"
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#ifdef __cplusplus
}
#endif

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)


#define RUN_STATUS_LED          DK_LED1
#define CON_STATUS_LED          DK_LED2
#define RUN_LED_BLINK_INTERVAL  1000

#define USER_LED                DK_LED3

#define USER_BUTTON             DK_BTN1_MSK

int app_index = -1;


#define SRAM_128_BYTES_LEN 128

/*
FLash offset indices
KWS - 1
MNIST - 0
*/

#define VALID_PROGRAM_DATA_MNIST                                                     \
  0xD8130700 //0xD8130700        //0x78710600 // first 4 bytes in the generated program_data.bin

#define VALID_PROGRAM_DATA_KWS 0x64d70000

volatile static size_t akd_flash_offset = 0x1000;
uint32_t buff_size = 0x100000;
uint32_t flash_offsets[] = {akd_flash_offset, akd_flash_offset + buff_size};
const unsigned char* inputs[] = {mnist_inputs, kws_inputs};
uint32_t valid_program_data[] = {VALID_PROGRAM_DATA_MNIST, VALID_PROGRAM_DATA_KWS};
const unsigned char* program_info[] = {mnist_program_info, kws_program_info};
const int64_t program_info_len[] = {mnist_program_info_len, kws_program_info_len};



static bool app_button_state;

/* Flag to write BLE transferred data into SRAM buffer first or write directly
 * into SPI-Flash */
#define WRITE_SRAM_CHUNKS 1

#define SRAM_BUFFER_SIZE                                                       \
  249856 // 244 KB i.e multiples of 244 bytes to ease the BLE operations  31232  // 30.5 //
#if FLASH_READ_BACK_CHECK
#define HALF_OF_SRAM_BUFFER_SIZE (SRAM_BUFFER_SIZE / 2)
#define BUFFER_SIZE HALF_OF_SRAM_BUFFER_SIZE
uint8_t read_back_flash[HALF_OF_SRAM_BUFFER_SIZE]; // buffer to validate read
                                                   // back from flash
#else
#define BUFFER_SIZE SRAM_BUFFER_SIZE
#endif

/* Define SRAM buffer in a named section */
__attribute__((section(".sram_upload_buf"),
               used)) uint8_t sram_upload_buffer[BUFFER_SIZE];


#define WORD_SIZE 4
#define NUM_WORDS 1
#define READ_LEN (NUM_WORDS * WORD_SIZE)

#define ONE_MB_SRAM_ADDR 0xFC800000 //  AKD1500 1MB SRAM address

#define CONFIG_AKD1500_CTRL 0XFCE00018
#define EN_SPI_S2M_Pos (16U)
#define EN_SPI_S2M_Msk (0x1UL << EN_SPI_S2M_Pos)
#define EN_SPI_S2M EN_SPI_S2M_Msk


#define FLASH_BASE_ADDRESS 0x80000000

#define FLASH_MAX_16_MB_SIZE (16777216) // total max size of SPI-Flash => 16 MB

#define ACK_FLASH_ERASE_DONE 0xEE
#define ACK_FLASH_WRITE_DONE 0xCC

#define NUM_STATES 3

volatile static size_t total_pgm_size = 0;
volatile static size_t ble_pgm_offset = 0;

volatile static size_t total_received = 0;

static uint8_t read_data[READ_LEN];

enum app_states { IDLE_STATE, BLE_IN_USE, JLINK_IN_USE };

enum app_states curr_state = IDLE_STATE;
constexpr uint32_t scratch_base_address = 0x00000000; // Use a valid address
constexpr uint32_t scratch_size = akida::soc::akd1500::kMainMemorySize;
constexpr uint32_t akida_visible_memory_base = scratch_base_address;
constexpr uint32_t akida_visible_memory_size = scratch_size;

const struct device *wdt_dev; // Global watchdog device
void kick_watchdog(void) {
  if (wdt_dev) {
    // wdt_feed(wdt_dev, 0); // Feed the watchdog
    printk("Watchdog fed\n");
  }
}


// Create an instance of ZephyrSpiDriver
akida::ZephyrSpiDriver spi_driver;

// Create an instance of Akd1500SpiDriver with predefined memory regions
akida::Akd1500SpiDriver akd1500(&spi_driver, akida_visible_memory_base,
                                akida_visible_memory_size);
akida::HardwareDeviceImpl device(&akd1500);


int spi_flash_erase_helper_func(uint32_t offset, uint32_t size);


int64_t time_ms() { return k_uptime_get(); }

void msleep(uint32_t duration) { k_sleep(K_MSEC(duration)); }

void panic(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  exit(EXIT_FAILURE);
}


static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};

static void connected_ble(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
		return;
	}

	printk("Connected\n");

	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected_ble(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);

	dk_set_led_off(CON_STATUS_LED);
}

#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d\n", addr, level,
			err);
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected_ble,
	.disconnected     = disconnected_ble,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_LBS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void app_led_cb(bool led_state)
{
	dk_set_led(USER_LED, led_state);
}

static bool app_button_cb(void)
{
	return app_button_state;
}

static struct bt_lbs_cb lbs_callbacs = {
	.led_cb    = app_led_cb,
	.button_cb = app_button_cb,
};

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON) {
		uint32_t user_button_state = button_state & USER_BUTTON;

		bt_lbs_send_button_state(user_button_state);
		app_button_state = user_button_state ? true : false;
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}


union _data {
  uint8_t ucdata[4];
  uint32_t uint_data;
};

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

/* function to validate model meta data with the acutal model present at the
 * flash_offsets[app_index] */
int akida_program_info(uint8_t *program_info, int len, uint32_t offset) {

  auto info = device.program_external_data(program_info, len,
                                           offset + FLASH_BASE_ADDRESS);
  if (info.is_valid()) {
    auto inputsz = info.input_dims();
    printk("input shape: (%d, %d, %d)\n\r", inputsz[0], inputsz[1], inputsz[2]);
    return 0;
  }
  return -1;
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
	int blink_status = 0;
	int err;

	printk("Starting Bluetooth Peripheral LBS example\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return 0;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return 0;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_lbs_init(&lbs_callbacs);
	if (err) {
		printk("Failed to init LBS (err:%d)\n", err);
		return 0;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising successfully started\n");

	/* Set MCU as SPI-Master */
	akida_config_spi(1); 
	/* read Akida Device ID */
	akd1500.read(0xFCC00000, read_data, READ_LEN);
	printk("Akida Device ID: \n");
	for (int i = 0; i < READ_LEN; i += WORD_SIZE) {
		printk("Word %d: 0x%02X%02X%02X%02X\n", i / WORD_SIZE, read_data[i],
		   read_data[i + 1], read_data[i + 2], read_data[i + 3]);
	}


	k_sleep(K_MSEC(1000));

    uint8_t msg[SRAM_128_BYTES_LEN] = "Hello world!!! This is a test for writing and reading 128 bytes of data to and from 1MB of RAM within Brainchip's AKD1500 chip";
	uint8_t sram_read_data[SRAM_128_BYTES_LEN] = "kkkkkkkkk";
	uint32_t fail_cnt = 0;

    // Write and read 1 MB RAM in Akida in 128-byte chunks (Currently it is tested for 128 Bytes).
	// To check complete 1MB replace offset < 1 with offset < ONE_MB in the below for loop
	
    for (uint32_t offset = 0; offset < 1; offset += SRAM_128_BYTES_LEN) {
        uint32_t addr = ONE_MB_SRAM_ADDR + offset;

        // Write
        akd1500.write(addr, msg, SRAM_128_BYTES_LEN);
        k_sleep(K_MSEC(1));
        akd1500.read(addr, sram_read_data, SRAM_128_BYTES_LEN);
        k_sleep(K_MSEC(1));
        // Compare
        if (memcmp(msg, sram_read_data, SRAM_128_BYTES_LEN) != 0)
			{
				fail_cnt++;
				printk("Data mismatch at address 0x%08X (offset: %u bytes)\n", addr, offset);
				for (int i = 0; i < SRAM_128_BYTES_LEN; ++i) {
					printk("%c", sram_read_data[i]);
				}
        }
		//Clear the read data
		memset(sram_read_data, 0, SRAM_128_BYTES_LEN);
    }
	if (fail_cnt == 0)
	{
		printk("Sanity test of 1 MB SRAM is passed\n");
	}
	else
	{
		printk("Sanity test of 1 MB SRAM is failed\n");
	}
     
	/* Initialize the AKD1500 SPI-Flash functionality */
	init_akd_1500_spi_flash();

	/* get akida device version */
	auto hw_version = akida::read_hw_version(akd1500);
	printk("Device Version: v%u.%u\n", hw_version.major_rev,
		 hw_version.minor_rev);

	k_sleep(K_MSEC(10));
	spi_flash_read_id(spi_driver); // read SPI-Flash id

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}



/* helper function to invoke spi-flash write driver API for the given data,
 * offset, and size */
void spi_flash_write_helper_func(const uint8_t *data, size_t offset,
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

/* function to run the inference */
static int infer(int app_index_l) {
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
  device.set_batch_size(1, true);
  app_index = app_index_l;

  }

  device.toggle_clock_counter(true);


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

  s_dma_cycls = device.read_clock_counter();
  s_tick = time_ms();
  auto inference_op = device.forward({in});
  e_tick = time_ms();
  e_dma_cycls = device.read_clock_counter();
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

/* helper function to invoke flash erase API calls */
int spi_flash_erase_helper_func(uint32_t offset, uint32_t size) {
  if (size == 0 ||
      size > (FLASH_MAX_16_MB_SIZE - offset)) {
    printk("Invalid size. Must be > 0 and <= %d\n",
           (FLASH_MAX_16_MB_SIZE - offset));
    return 1;
  }
  akida_config_spi(1);

  uint64_t s_tick = 0;
  ;
  uint64_t e_tick = 0;
  uint32_t erase_time = 0;

  printk("Flase erase offset %x and size = %d bytes\n",
           offset, size);

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

/* shell cli function to invoke erase function */
static int cmd_full_erase(const struct shell *shell, size_t argc, char **argv) {


  if (spi_flash_erase_helper_func(0x1000, FLASH_MAX_16_MB_SIZE - 0x1000)) {
    return 1;
  }

  return 0;
}

/* shell cli function to print the buffer size allocated, useful for Jlink host
 * application  */
static int get_buffer_size(const struct shell *shell, size_t argc,
                           char **argv) {
  printk("Allocated SRAM buffer size: %d\n", BUFFER_SIZE);
  return 0;
}

/* shell cli function to update the status command */
static int cmd_status(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    // Update state
    enum app_states new_state = (enum app_states)atoi(argv[1]);
    if (new_state == IDLE_STATE || new_state == BLE_IN_USE ||
        new_state == JLINK_IN_USE) {
      curr_state = new_state;
    } else
      printk("Invalid state value %d\n", new_state);
  }
  printk("App status is %d\n", curr_state);
  return 0;
}

/* Upload command: expects two arguments (<flash_offset> <size_in_bytes>)
 * function used to upload the model data into spi-flash, prior calling this
 * function make sure the respective model size is erased first */
static int cmd_upload(const struct shell *shell, size_t argc, char **argv) {
  if (argc != 3) {
    printk("Usage: upload <flash_offset> <size_in_bytes>\n");
    return -EINVAL;
  }

  size_t size = strtoul(argv[2], NULL, 0);
  size_t flash_offset = strtoul(argv[1], NULL, 0);

  if (size == 0 || size > BUFFER_SIZE) {
    printk("Invalid size. Must be > 0 and <= %d\n", BUFFER_SIZE);
    return -EINVAL;
  }
  spi_flash_write_helper_func(sram_upload_buffer, flash_offset, size);
  if ((flash_offset + size) == total_pgm_size) {
    printk("Start Program\n");
    // program the model info part to AKD1500
    akida_program_info((uint8_t *)program_info[app_index], program_info_len[app_index],flash_offsets[app_index]);
    device.set_batch_size(1, true);
  }

  return 0;
}

/* function to set the allocated buffer data to zero */
#if WRITE_SRAM_CHUNKS
extern "C" void reset_buffer(void) {
  ble_pgm_offset = 0;
  memset(sram_upload_buffer, 0, sizeof(sram_upload_buffer));
}
#endif

uint32_t app_flash_offset = akd_flash_offset;
extern "C" ssize_t get_app_index(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *app, uint16_t len,
                            uint16_t offset, uint8_t flags){
    if((uint32_t *)app == NULL)
    {
       printk(" app index is NULL \n\r");
       return -1;   
    }

    uint8_t app_index_local = *(uint8_t *)app;
    if(app_index_local > 1)
    {
       printk("illegal app request: %d\n", app_index_local);
       return -1;
    }
    app_index = app_index_local;
    app_flash_offset = flash_offsets[app_index];

    return len;
}
/* This function is a BLE service that receives data and its length from the BLE
 * client host application. If the flag "WRITE_SRAM_CHUNKS" is set, the data is
 * first stored in an SRAM buffer in chunks of size BUFFER_SIZE (or the
 * remaining bytes), and later written to the SPI flash. If the flag is not set,
 * the data is written directly to the SPI flash. */

extern "C" ssize_t file_transfer_write(struct bt_conn *conn,
                                       const struct bt_gatt_attr *attr,
                                       const void *buf, uint16_t len,
                                       uint16_t offset, uint8_t flags) {
  if (curr_state == IDLE_STATE || curr_state == BLE_IN_USE) {
    curr_state = BLE_IN_USE;
#if WRITE_SRAM_CHUNKS
    memcpy(&sram_upload_buffer[ble_pgm_offset], buf, len);

    ble_pgm_offset += len;

    total_received += len;

    printk("Received chunk (%d bytes), total: %d bytes\n", len, total_received);

    if (ble_pgm_offset == BUFFER_SIZE || total_received == total_pgm_size) {
      spi_flash_write_helper_func(
          static_cast<const uint8_t *>(sram_upload_buffer), app_flash_offset,
          ble_pgm_offset);
      app_flash_offset += ble_pgm_offset;
      reset_buffer();
      send_ack_to_host(ACK_FLASH_WRITE_DONE);
      if (total_received == total_pgm_size) {
        // program the model info part to AKD1500
        akida_program_info((uint8_t *)program_info[app_index], program_info_len[app_index],flash_offsets[app_index]);
        device.set_batch_size(1, true);
        printk("Start inference\n");
        if (infer(app_index)) {
          return 1;
        }
        total_received = 0;
        app_flash_offset = 0;
        printk("Going back to idle state\n");
        curr_state = IDLE_STATE;
      }
    } else if (ble_pgm_offset > BUFFER_SIZE) {
      ble_pgm_offset = 0;
      printk("Data exceeds buffer size %d\n", BUFFER_SIZE);
    }
#else
    if ((ble_pgm_offset + len) <= total_pgm_size) {
      spi_flash_write_helper_func(static_cast<const uint8_t *>(buf),
                                  ble_pgm_offset, len);
      ble_pgm_offset += len;
      printk("Received chunk (%d bytes), total: %d bytes\n", len,
             ble_pgm_offset);
      if ((ble_pgm_offset) == total_pgm_size) {
        // program the model info part to AKD1500
        akida_program_info((uint8_t *)program_info, program_info_len,
                           flash_offsets[app_index]);
        device.set_batch_size(1, true);
        if (infer(app_index)) {
          return 1;
        }
        ble_pgm_offset = 0;
        printk("Going back to idle state\n");
        curr_state = IDLE_STATE;
        send_ack_to_host(ACK_FLASH_WRITE_DONE);
      }
    } else {
      ble_pgm_offset = 0;
      printk("Data exceeds given model size %d\n", total_pgm_size);
    }
#endif
    return len;
  } else {
    printk("App not in expected state : state = %d\n", curr_state);
    return -1;
  }
}

/* This function is a BLE service that receives the size of the model passed
 * using BLE host application and erase the spi-flash content of the given size
 */
extern "C" ssize_t get_file_size(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags) {
  if (curr_state == IDLE_STATE || curr_state == BLE_IN_USE) {
    curr_state = BLE_IN_USE;
    printk("App state = %d\n", curr_state);
    memcpy((void *)&total_pgm_size, buf, len);

    printk("size = %d\n", total_pgm_size);

    if (total_pgm_size == 0 ||
        total_pgm_size > (FLASH_MAX_16_MB_SIZE - flash_offsets[app_index])) {
      printk("Invalid size. Must be > 0 and <= %d\n",
             (FLASH_MAX_16_MB_SIZE - flash_offsets[app_index]));
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    printk("Received file size: %u bytes\n", total_pgm_size);
    if (spi_flash_erase_helper_func(flash_offsets[app_index], total_pgm_size)) {
      return 1;
    }
    send_ack_to_host(ACK_FLASH_ERASE_DONE);
    return len;
  } else {
    printk("App not in expected state : state = %d\n", curr_state);
    return -1;
  }
}



SHELL_CMD_REGISTER(full_erase, NULL, "Erase flash: erase <size>", cmd_full_erase);
SHELL_CMD_REGISTER(
    upload, NULL,
    "Upload data from SRAM to flash: upload <flash_offset> <size_in_bytes>",
    cmd_upload);
SHELL_CMD_REGISTER(
    set, NULL, "Set MCU/AKD1500 as SPI-Master: set <bool> (0:AKD1500 1:MCU)",
    cmd_set);
SHELL_CMD_REGISTER(infer, NULL, "Start the Inference: infer", cmd_infer);
SHELL_CMD_REGISTER(get_size, NULL,
                   "To get the SRAM allocated buffer size: get_size",
                   get_buffer_size);
SHELL_CMD_REGISTER(status, NULL, "Check app status: status [flag]", cmd_status);