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
#include "sample_input/kws/kws_inputs.h"
#include "nrf_spi.h"
#include "kws/kws_model.h"
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

static bool app_button_state;

#define SRAM_128_BYTES_LEN 128
#define WORD_SIZE 4
#define NUM_WORDS 1
#define READ_LEN (NUM_WORDS * WORD_SIZE)

#define ONE_MB_SRAM_ADDR 0xFC800000 //  AKD1500 1MB SRAM address

#define CONFIG_AKD1500_CTRL 0XFCE00018
#define EN_SPI_S2M_Pos (16U)
#define EN_SPI_S2M_Msk (0x1UL << EN_SPI_S2M_Pos)
#define EN_SPI_S2M EN_SPI_S2M_Msk


#define FLASH_BASE_ADDRESS 0x80000000
#define FLASH_MODEL_PROG_OFFSET                                                \
  0X1000 // model resides in this SPI-Flash offset location
#define FLASH_MAX_16_MB_SIZE (16777216) // total max size of SPI-Flash => 16 MB

#define ACK_FLASH_ERASE_DONE 0xEE
#define ACK_FLASH_WRITE_DONE 0xCC

#define NUM_STATES 3

volatile static size_t total_pgm_size = 0;
volatile static size_t ble_pgm_offset = 0;
volatile static size_t akd_flash_offset = 0;
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


int spi_flash_erase_helper_func();


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




int post_processing(auto out, const int32_t *bytes_out){

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
	/* read Akida device ID */
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

    // Write and read 1 MB in 128-byte chunks (Currently it is tested for 128 Bytes).
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
     
    akida::HardwareDeviceImpl device(&akd1500);
    akida::ProgramInfo program_info(&kws_model[0], kws_model_len);
    auto hw_version = akida::read_hw_version(akd1500);
    printk("Device Version: v%u.%u\n", hw_version.major_rev, hw_version.minor_rev);
    auto program_version = program_info.device_version();
    printk("Program Version: v%u.%u\n", program_version.major_rev, program_version.minor_rev);
    device.toggle_clock_counter(true);
    volatile uint32_t s_dma_cycls = device.read_clock_counter();
	volatile uint64_t s_tick = time_ms();
	/*  model programming to akida*/
    device.program(&kws_model[0], kws_model_len);
	volatile uint64_t e_tick = time_ms();
	volatile uint32_t model_prog_time = e_tick - s_tick;
    volatile uint32_t e_dma_cycls = device.read_clock_counter();
	volatile uint32_t delta_cycle = e_dma_cycls - s_dma_cycls;
    if (program_info.is_valid()) {
        printk("model program time= %u dma cycles, model_prog_time = %u ms\n", delta_cycle, model_prog_time );
    }
    else
        printk("program info is not valid\n");
    k_sleep(K_MSEC(500));
	device.toggle_clock_counter(true);	

	printk("input Shape = %dx%dx%d\n", kws_inputs_shape[0], kws_inputs_shape[1], kws_inputs_shape[2]);
	uint32_t inf_complete = 0;
	device.set_batch_size(1, true);
		
	akida::TensorConstPtr in = akida::Dense::create_view(
	reinterpret_cast<const char *>(kws_inputs), akida::TensorType::uint8,
	{kws_inputs_shape}, akida::Dense::Layout::RowMajor);

	s_dma_cycls = device.read_clock_counter();
	s_tick = time_ms();
	/* infer the input */
	auto inference_op = device.forward({in});
	e_tick = time_ms();
	e_dma_cycls = device.read_clock_counter();
	delta_cycle = e_dma_cycls - s_dma_cycls;
	uint32_t inf_time = e_tick - s_tick;
	printk("\n\rinference time= %u dma cycles, time = %u ms\n\r", delta_cycle,
	 inf_time);
	/** Get output buffer */
	auto out = akida::Tensor::ensure_dense(std::move(inference_op[0]));
	const int32_t *bytes_out = (int32_t *)out->buffer()->data();
	for (int i = 0; i < (int)out->size(); i++) {
		printk("Output-%d = %d\n", i, bytes_out[i]);
		inf_complete = 0xAA;
	}
	/* perform post processing on outputs */
	int class_id = post_processing(out, bytes_out);

	printk("\nClass : %d\n", class_id);
	printk("Word : %s\n", kws_tags[class_id]);

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}

