#include "gpio/gpio.h"
#include <errno.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio, LOG_LEVEL_DBG);

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
#define AKD_LP_NODE DT_NODELABEL(akd_lp)

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

/* AKD1500 SLEEP pin (active-high): 1 = low-power (clocks gated, model retained) */
static const struct gpio_dt_spec akd_lp = GPIO_DT_SPEC_GET(AKD_LP_NODE, gpios);

static struct gpio_callback akd_async_cb;

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
    LOG_ERR("AKD enable GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_acc)) {
    LOG_ERR("ACC enable GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_pdm)) {
    LOG_ERR("PDM enable GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_akd_0V)) {
    LOG_ERR("AKD 0V enable GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_camera)) {
    LOG_ERR("Camera enable GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&enable_akd_async)) {
    LOG_ERR("AKD ASYNC enable GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&akd_lp)) {
    LOG_ERR("AKD sleep GPIO not ready");
    return -ENODEV;
  }

  /* --- Configure control pins as output inactive --- */
  err = gpio_pin_configure_dt(&enable_akd, GPIO_OUTPUT_INACTIVE);
  if (err) {
    LOG_ERR("Failed to configure AKD enable pin (err %d)", err);
    return err;
  }

  err = gpio_pin_configure_dt(&enable_acc, GPIO_OUTPUT_INACTIVE);
  if (err) {
    LOG_ERR("Failed to configure ACC enable pin (err %d)", err);
    return err;
  }

  err = gpio_pin_configure_dt(&enable_pdm, GPIO_OUTPUT_INACTIVE);
  if (err) {
    LOG_ERR("Failed to configure PDM enable pin (err %d)", err);
    return err;
  }

  err = gpio_pin_configure_dt(&enable_akd_0V, GPIO_OUTPUT_INACTIVE);
  if (err) {
    LOG_ERR("Failed to configure AKD 0V enable pin (err %d)", err);
    return err;
  }
  err = gpio_pin_configure_dt(&enable_camera, GPIO_OUTPUT_INACTIVE);
  if (err) {
    LOG_ERR("Failed to configure camera enable pin (err %d)", err);
    return err;
  }
  err = gpio_pin_configure_dt(&enable_akd_async, GPIO_INPUT);
  if (err) {
    LOG_ERR("Failed to configure AKD ASYNC enable pin (err %d)", err);
    return err;
  }
  err = gpio_pin_configure_dt(&akd_lp, GPIO_OUTPUT_INACTIVE); /* start awake */
  if (err) {
    LOG_ERR("Failed to configure AKD sleep pin (err %d)", err);
    return err;
  }
  gpio_init_callback(&akd_async_cb, akd_async_isr_handler,
                     BIT(enable_akd_async.pin));
  err = gpio_add_callback(enable_akd_async.port, &akd_async_cb);
  if (err) {
    LOG_ERR("Failed to add AKD ASYNC callback (err %d)", err);
    return err;
  }

  LOG_INF("GPIO initialized");
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
/* Mirrors the last state requested through akd_sleep(). gpio_init() configures
 * akd_lp as GPIO_OUTPUT_INACTIVE, so the chip starts awake and this starts
 * false. Tracked here rather than read back off the pin: this is the requested
 * state, which is what a save/restore around a flash access needs. */
static bool akd_asleep = false;

/**
 * @brief Put the AKD1500 into low-power sleep or wake it.
 *
 * Drives the active-high SLEEP pin: true gates the internal clocks (state and
 * programmed model retained), false resumes normal operation.
 */
void akd_sleep(bool sleep) {
  akd_asleep = sleep;
  gpio_pin_set_dt(&akd_lp, sleep);
}

/**
 * @brief Report the last state requested through akd_sleep().
 *
 * @return true if the AKD1500 was last asked to sleep, false if awake.
 */
bool akd_sleep_get(void) { return akd_asleep; }
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