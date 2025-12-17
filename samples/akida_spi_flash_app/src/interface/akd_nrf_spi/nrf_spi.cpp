#include "nrf_spi.h"
#include <akd1500/akd1500_spi_driver.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio.h>
#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(akd1500_driver, LOG_LEVEL_DBG);

#define SPI_WRITE_CMD 0x80
#define SPI_READ_CMD  0x60

#define AKD_SLEEP_NODE DT_ALIAS(akdsleep)
#define AKD_RESET_NODE DT_ALIAS(akdreset)
#define AKD_CS_NODE DT_ALIAS(akdcs)
#define FLASH_CS_NODE DT_ALIAS(flashcs)


const struct gpio_dt_spec akdSleep = GPIO_DT_SPEC_GET(AKD_SLEEP_NODE, gpios);
const struct gpio_dt_spec akdReset = GPIO_DT_SPEC_GET(AKD_RESET_NODE, gpios);
const struct gpio_dt_spec akd_cs = GPIO_DT_SPEC_GET(AKD_CS_NODE, gpios);
const struct gpio_dt_spec flash_cs = GPIO_DT_SPEC_GET(FLASH_CS_NODE, gpios);

/*static struct spi_cs_control spi_cs = {
    .gpio = GPIO_DT_SPEC_GET(DT_ALIAS(spi1), cs_gpios),
    .delay = 0,
};*/

static struct spi_config spi_cfg = {
    .frequency = 400000U, // match Python default for stability
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .cs = NULL,
};

namespace akida {

int ZephyrSpiDriver::init_qspi() {
    this->spi_dev = DEVICE_DT_GET(DT_ALIAS(spi1));
    if (!device_is_ready(this->spi_dev)) {
        printk("SPI device not ready\n");
        return -ENODEV;
    }

    if (!device_is_ready(akd_cs.port)) {
        printf("Akida CS GPIO not ready.\n");
        return 0;
    }
    if (gpio_pin_configure_dt(&akd_cs, GPIO_OUTPUT_ACTIVE) != 0) {
        printf("Failed to configure Akida CS pin.\n");
        return 0;
    }
    gpio_pin_set_dt(&akd_cs, 1); // CS high (not selected)


    if (!device_is_ready(flash_cs.port)) {
        printf("Flash CS GPIO not ready.\n");
        return 0;
    }
    if (gpio_pin_configure_dt(&flash_cs, GPIO_OUTPUT_ACTIVE) != 0) {
        printf("Failed to configure Flash CS pin.\n");
        return 0;
    }
    gpio_pin_set_dt(&flash_cs, 1); // CS high (not selected)


    return 0;
}

void ZephyrSpiDriver::read(uint8_t *data, size_t size) 
{
	printf("ZephyrSpiDriver::read 2 args");
	return;
}
void ZephyrSpiDriver::write(const uint8_t *data, size_t size)
{
	printf("ZephyrSpiDriver::write 2 args");
	return;
}


void ZephyrSpiDriver::read_api(uint32_t header_size, uint8_t *data, size_t size) {
	
	uint8_t tx_buf[size] = {0};

	struct spi_buf tx = {.buf = data, .len =  size};
	struct spi_buf rx = {.buf = tx_buf, .len = size};

	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
	gpio_pin_set_dt(&akd_cs, 0); // CS LOW
	int err = spi_transceive(this->spi_dev, &spi_cfg, &tx_set, &rx_set);
	gpio_pin_set_dt(&akd_cs, 1); // CS HIGH
	if (err != 0) {
		printk("SPI transfer failed: %d\n", err);
		return;
	}        
	memcpy(&data[0], &tx_buf[header_size], size);

    return;
}

void ZephyrSpiDriver::write_api(uint32_t header_size, const uint8_t *data, size_t size) {
	
	struct spi_buf tx = {.buf = (uint8_t *)data, .len = size};
	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	gpio_pin_set_dt(&akd_cs, 0); // CS LOW
	int err = spi_write(this->spi_dev, &spi_cfg, &tx_set);
	gpio_pin_set_dt(&akd_cs, 1); // CS HIGH
	if (err != 0) {
		LOG_ERR("SPI write failed: %d", err);
		return;
	}
    return;
}

void ZephyrSpiDriver::chip_select(uint32_t slave_ID, bool active) {
     return;
}

void ZephyrSpiDriver::spiflashwrite(uint32_t address, const uint8_t *data, size_t size) {

	struct spi_buf tx = {.buf = (uint8_t *)data, .len =  size};
	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
    gpio_pin_set_dt(&flash_cs, 0); // CS LOW
	int err = spi_write(this->spi_dev, &spi_cfg, &tx_set);
	gpio_pin_set_dt(&flash_cs, 1); // CS HIGH
	if (err != 0) {
		LOG_ERR("SPI write failed: %d", err);
	}
	return;
}

void ZephyrSpiDriver::spiflashread(uint32_t address, uint8_t *cmd, uint8_t *data, uint32_t size) {
    size_t cmd_bytes = (size >> 16) & 0xFF;
		
	uint8_t rx_buf[5 + 256] = {0};
	
	int len = (size & 0xFFFF) ;
	struct spi_buf tx = {.buf = cmd, .len = len + cmd_bytes};
	struct spi_buf rx = {.buf = rx_buf, .len = len + cmd_bytes};

	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
    gpio_pin_set_dt(&flash_cs, 0); // CS LOW
	int err = spi_transceive(this->spi_dev, &spi_cfg, &tx_set, &rx_set);
	gpio_pin_set_dt(&flash_cs, 1); // CS HIGH
	if (err != 0) {
		printk("SPI transfer failed: %d\n", err);
		return;
	}

	memcpy(&data[0], &rx_buf[cmd_bytes], len);
	return;
}


} // namespace akida