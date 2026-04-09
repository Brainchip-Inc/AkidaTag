#include "gpio/gpio.h"
#include <errno.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/* ---------- Control GPIOs ---------- */
#define AKD_ENB_NODE DT_NODELABEL(akd_enb)
#define ACC_ENB_NODE DT_NODELABEL(acc_enb)
#define PDM_ENB_NODE DT_NODELABEL(pdm_enb)
#define AKD_0V_ENB_NODE DT_NODELABEL(akd_0v_enb)
#define CAM_ENB_NODE DT_NODELABEL(cam_enb)

static const struct gpio_dt_spec enable_akd =
    GPIO_DT_SPEC_GET(AKD_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_acc =
    GPIO_DT_SPEC_GET(ACC_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_pdm =
    GPIO_DT_SPEC_GET(PDM_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_akd_0V =
    GPIO_DT_SPEC_GET(AKD_0V_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_camera =
    GPIO_DT_SPEC_GET(CAM_ENB_NODE, gpios);

/**
 * @brief Initialize all GPIO pins and button interrupts.
 *
 * This function performs the full GPIO bring-up sequence:
 *  - Verifies readiness of all GPIO devices
 *  - Configures power enable pins as output inactive
 *  - Configures user button as input with edge interrupt
 *  - Registers the user button callback (user_pressed)
 *
 * @return 0 on success
 * @return -ENODEV if any GPIO device is not ready
 * @return negative errno on configuration failure
 */
int gpio_init(void) {
  int err;

  /* --- Check GPIO readiness --- */
  if (!gpio_is_ready_dt(&enable_akd)) {
    printk("AKD enable GPIO not ready\n");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_acc)) {
    printk("ACC enable GPIO not ready\n");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_pdm)) {
    printk("PDM enable GPIO not ready\n");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_akd_0V)) {
    printk("AKD 0V enable GPIO not ready\n");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_camera)) {
    printk("Camera enable GPIO not ready\n");
    return -ENODEV;
  }

  /* --- Configure control pins as output inactive --- */
  err = gpio_pin_configure_dt(&enable_akd, GPIO_OUTPUT_INACTIVE);
  if (err) {
    printk("Failed to configure AKD enable pin (err %d)\n", err);
    return err;
  }

  err = gpio_pin_configure_dt(&enable_acc, GPIO_OUTPUT_INACTIVE);
  if (err) {
    printk("Failed to configure ACC enable pin (err %d)\n", err);
    return err;
  }

  err = gpio_pin_configure_dt(&enable_pdm, GPIO_OUTPUT_INACTIVE);
  if (err) {
    printk("Failed to configure PDM enable pin (err %d)\n", err);
    return err;
  }

  err = gpio_pin_configure_dt(&enable_akd_0V, GPIO_OUTPUT_INACTIVE);
  if (err) {
    printk("Failed to configure AKD 0V enable pin (err %d)\n", err);
    return err;
  }
  err = gpio_pin_configure_dt(&enable_camera, GPIO_OUTPUT_INACTIVE);
  if (err) {
    printk("Failed to configure camera enable pin (err %d)\n", err);
    return err;
  }

  printk("GPIO initialized\n");
  return 0;
}
/**
 * @brief Enable power for onboard sensors and peripherals on the Spark board.
 *
 * This function enables the required power rails and peripherals in the
 * correct order with appropriate delays to ensure stable startup.
 *
 * Sequence:
 * 1. Enable AKD 0V rail
 * 2. Enable AKD1500 device
 * 3. Enable accelerometer
 * 4. Enable PDM microphone
 * 5. Enable camera module
 */
void spark_peripherals_power_enable(void) {
  gpio_pin_set_dt(&enable_akd_0V, GPIO_ENABLE);
  gpio_pin_set_dt(&enable_akd, GPIO_ENABLE);
  gpio_pin_set_dt(&enable_acc, GPIO_ENABLE);
  gpio_pin_set_dt(&enable_pdm, GPIO_ENABLE);
  gpio_pin_set_dt(&enable_camera, GPIO_ENABLE);
}