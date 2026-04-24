#include "gpio/gpio.h"
#include <errno.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

static bool irq_enabled = false;

/* Semaphore signaled by ISR when Akida asserts its done interrupt */
K_SEM_DEFINE(akd_async_sem, 0, 1);

/* ---------- Control GPIOs ---------- */
#define AKD_ENB_NODE DT_NODELABEL(akd_enb)
#define ACC_ENB_NODE DT_NODELABEL(acc_enb)
#define PDM_ENB_NODE DT_NODELABEL(pdm_enb)
#define AKD_0V_ENB_NODE DT_NODELABEL(akd_0v_enb)
#define CAM_ENB_NODE DT_NODELABEL(cam_enb)
#define AKD_ASYNC_ENB_NODE DT_NODELABEL(akd_async)
#define CHRG_STS1_NODE DT_NODELABEL(chgr_sts1)
#define CHRG_STS2_NODE DT_NODELABEL(chgr_sts2)

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

static const struct gpio_dt_spec enable_akd_async =
    GPIO_DT_SPEC_GET(AKD_ASYNC_ENB_NODE, gpios);

static struct gpio_callback akd_async_cb;

static const struct gpio_dt_spec chgr_sts1 =
    GPIO_DT_SPEC_GET(CHRG_STS1_NODE, gpios);

static const struct gpio_dt_spec chgr_sts2 =
    GPIO_DT_SPEC_GET(CHRG_STS2_NODE, gpios);
/**
 * @brief Wrapper function to take the Akida async semaphore
 *
 * This function encapsulates access to the internal semaphore used for
 * async processing.
 *
 * @param timeout Timeout for semaphore wait
 *
 * @return 0 on success, negative error code on failure/timeout
 */
int akd_async_sem_take(k_timeout_t timeout) {
  return k_sem_take(&akd_async_sem, timeout);
}

/**
 * @brief Interrupt handler for AKD asynchronous GPIO pin.
 *
 * This ISR is triggered on an edge-to-active transition of the AKD async GPIO.
 * It indicates that the Akida processor has generated an asynchronous event.
 *
 * Behavior:
 * - If the system is currently in learning mode, the AKD learning workqueue
 *   is scheduled to handle the event in a deferred context.
 * - Otherwise, the akd_async_sem semaphore is released to notify the main
 *   processing thread of the event.
 *
 * @param dev    GPIO device structure (unused)
 * @param cb     GPIO callback structure (unused)
 * @param pins   Bitmask of pins that triggered the interrupt (unused)
 */

static void akd_async_isr_handler(const struct device *dev,
                                  struct gpio_callback *cb, uint32_t pins) {
  if (akd_in_learning()) {
    schedule_akd_learning_wq();
  } else {
    k_sem_give(&akd_async_sem);
  }
}
/**
 * @brief Initialize all GPIO pins and configure AKD async interrupt.
 *
 * This function performs the full GPIO bring-up sequence:
 *  - Verifies readiness of all GPIO devices (AKD, ACC, PDM, Camera, AKD_0V,
 * AKD_ASYNC)
 *  - Configures power enable pins as output inactive
 *  - Sets up AKD async GPIO as input with interrupt on edge-to-active
 *  - Registers callback handler for AKD async interrupts
 *
 * The AKD async pin is configured to trigger an interrupt when the signal
 * transitions to active state, allowing the system to respond to asynchronous
 * events from the Akida processor.
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
  if (!gpio_is_ready_dt(&enable_akd_async)) {
    printk("AKD ASYNC enable GPIO not ready\n");
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
  err = gpio_pin_configure_dt(&enable_akd_async, GPIO_INPUT);
  if (err) {
    printk("Failed to configure AKD ASYNC enable pin (err %d)\n", err);
    return err;
  }
  gpio_init_callback(&akd_async_cb, akd_async_isr_handler,
                     BIT(enable_akd_async.pin));
  err = gpio_add_callback(enable_akd_async.port, &akd_async_cb);
  if (err) {
    printk("Failed to add AKD ASYNC callback (err %d)\n", err);
    return err;
  }

  printk("GPIO initialized\n");
  return 0;
}
/**
 * @brief Enable GPIO interrupt for Akida async processing
 *
 * Configures the GPIO interrupt to trigger on active edge if not already
 * enabled. This interrupt is used to signal async events.
 *
 * Notes:
 * - Safe to call multiple times (idempotent).
 * - Internal state is tracked using `irq_enabled`.
 */
void akd_irq_enable(void) {
  if (!irq_enabled) {
    gpio_pin_interrupt_configure_dt(&enable_akd_async, GPIO_INT_EDGE_TO_ACTIVE);
    irq_enabled = true;
  }
}
/**
 * @brief Disable GPIO interrupt for Akida async processing
 *
 * Disables the GPIO interrupt to prevent async event triggering.
 * Typically used when switching to sync mode.
 *
 * Notes:
 * - Safe to call multiple times (idempotent).
 * - Internal state is tracked using `irq_enabled`.
 */
void akd_irq_disable(void) {
  if (irq_enabled) {
    gpio_pin_interrupt_configure_dt(&enable_akd_async, GPIO_INT_DISABLE);
    irq_enabled = false;
  }
}

/**
 * @brief Initialize GPIO pins for battery charger status monitoring
 *
 * Checks readiness of the two charger status GPIO pins and configures
 * them as inputs for reading charger state information.
 *
 * @return int 0 on success, negative error code on failure:
 *         -ENODEV if GPIO device not ready or configuration fails
 */
int bat_sts_gpio_init(void) {
  int err;

  /* --- Check GPIO readiness --- */
  if (!gpio_is_ready_dt(&chgr_sts1)) {
    printk("Charging status1 GPIO not ready\n");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&chgr_sts2)) {
    printk("Charging status2 GPIO not ready\n");
    return -ENODEV;
  }
  /* --- Configure control pins as input --- */
  err = gpio_pin_configure_dt(&chgr_sts1, GPIO_INPUT);
  if (err) {
    printk("Failed to configure Charging status1 pin (err %d)\n", err);
    return err;
  }
  err = gpio_pin_configure_dt(&chgr_sts2, GPIO_INPUT);
  if (err) {
    printk("Failed to configure Charging status2 pin (err %d)\n", err);
    return err;
  }
  return 0;
}
/**
 * @brief Get the current battery charger status
 *
 * Reads two GPIO status pins from the battery charger IC and decodes
 * the combined state to determine if the battery is charging, not charging,
 * or in a fault condition (recoverable or non-recoverable fault).
 *
 * @return bat_status_t Battery status code:
 *         - BAT_NOT_CHARGING: Battery not charging
 *         - BAT_CHARGING: Battery actively charging
 *         - BAT_FAULT_RECOVERABLE: Recoverable fault condition detected
 *         - BAT_FAULT_NON_RECOVERABLE: Non-recoverable fault condition detected
 */
bat_status check_bat_status(void) {
  int sts1 = gpio_pin_get_dt(&chgr_sts1);
  if (sts1 < 0) {
    printk("Failed to read CHGR_STS1: %d", sts1);
    return BAT_READ_FAILED;
  }

  int sts2 = gpio_pin_get_dt(&chgr_sts2);
  if (sts2 < 0) {
    printk("Failed to read CHGR_STS2: %d", sts2);
    return BAT_READ_FAILED;
  }

  if (sts1 == 1 && sts2 == 1) {
    return BAT_NOT_CHARGING;
  } else if (sts1 == 1 && sts2 == 0) {
    return BAT_CHARGING;
  } else if (sts1 == 0 && sts2 == 1) {
    printk("Charger Status: Recoverable fault\n");
    return BAT_FAULT_RECOVERABLE;
  } else {
    printk("Charger Status: Non-recoverable fault\n");
    return BAT_FAULT_NON_RECOVERABLE;
  }
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