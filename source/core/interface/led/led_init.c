#include "led/led_init.h"
#include <zephyr/sys/atomic.h>

/* GPIO specs */
static const struct gpio_dt_spec red_led =
    GPIO_DT_SPEC_GET(RED_LED_NODE, gpios);

static const struct gpio_dt_spec green_led =
    GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios);

static atomic_t current_state = ATOMIC_INIT(LED_STATE_NORMAL_APP);

static atomic_t ble_connected = ATOMIC_INIT(0);

/**
 * @brief Turn ON the RED LED.
 *
 * Sets the red LED GPIO to active state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int red_led_on(void) { return gpio_pin_set_dt(&red_led, LED_ON); }
/**
 * @brief Turn OFF the RED LED.
 *
 * Sets the red LED GPIO to inactive state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int red_led_off(void) { return gpio_pin_set_dt(&red_led, LED_OFF); }
/**
 * @brief Turn ON the GREEN LED.
 *
 * Sets the green LED GPIO to active state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int green_led_on(void) { return gpio_pin_set_dt(&green_led, LED_ON); }
/**
 * @brief Turn OFF the GREEN LED.
 *
 * Sets the green LED GPIO to inactive state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int green_led_off(void) { return gpio_pin_set_dt(&green_led, LED_OFF); }
/**
 * @brief Update BLE connection status.
 *
 * This function is expected to be called from the BLE
 * connection callback to inform the LED module about
 * current BLE connection state.
 *
 * @param connected true if BLE is connected, false otherwise.
 */
void ble_connection_callback(bool connected) {
  atomic_set(&ble_connected, connected ? 1 : 0);
}
/**
 * @brief Get current BLE connection status.
 *
 * @return true if BLE is connected.
 * @return false if BLE is disconnected.
 */
bool is_ble_connected(void) { return atomic_get(&ble_connected); }
/**
 * @brief Get current BLE connection status.
 *
 * @return true if BLE is connected.
 * @return false if BLE is disconnected.
 */
int32_t led_init(void) {

  int ret;

  if (!gpio_is_ready_dt(&red_led)) {
    printk(" RED LED GPIO not ready (port=%s, pin=%d)\n", red_led.port->name,
           red_led.pin);
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&green_led)) {
    printk(" GREEN LED GPIO not ready (port=%s, pin=%d)\n",
           green_led.port->name, green_led.pin);
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    printk("   Configuration failed (err=%d)\n", ret);
    return ret;
  }
  ret = gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    printk("   Configuration failed (err=%d)\n", ret);
    return ret;
  }

  printk("   INITIALIZATION SUCCESS\n");

  return 0;
}

/**
 * @brief LED indication thread.
 *
 * This thread continuously monitors the current LED state
 * and updates LED patterns accordingly.
 *
 * State behavior:
 * - NORMAL_APP        : Green slow blink (2s), Red OFF
 * - BLE_CONNECTED     : Green ON, Red OFF
 * - FOTA_RECEIVING    : Green ON, Red fast blink (500ms)
 * - FLASH_WRITE       : Green ON, Red ON
 * - VERIFICATION      : Green OFF, Red slow blink (2s)
 * - UPDATE_SUCCESS    : Both LEDs blink 3 times, then
 *                       restore runtime state based on BLE status
 * - UPDATE_FAILED     : Green OFF, Red ON
 *
 * The thread runs indefinitely with LED_TICK_MS timing base.
 *
 * @param a Unused
 * @param b Unused
 * @param c Unused
 */

void led_ind_thread(void *a, void *b, void *c) {
  if (led_init() < 0) {
    return;
  }

  uint32_t tick = 0;

  while (1) {

    led_state_t state = atomic_get(&current_state);

    switch (state) {

    case LED_STATE_NORMAL_APP:
      /* LED1: Slow blink (2s), LED2: OFF */
      if ((tick / 10) % 2 == 0)
        green_led_on();
      else
        green_led_off();

      red_led_off();
      break;

    case LED_STATE_BLE_CONNECTED:
      /* LED1: ON, LED2: OFF */
      green_led_on();
      red_led_off();
      break;

    case LED_STATE_FOTA_RECEIVING:
      /* LED1: ON, LED2: Fast blink (500ms) */
      green_led_on();
      if ((tick / 2) % 2 == 0)
        red_led_on();
      else
        red_led_off();
      break;

    case LED_STATE_FLASH_WRITE:
      /* LED1: ON, LED2: ON */
      green_led_on();
      red_led_on();
      break;

    case LED_STATE_VERIFICATION:
      /* LED1: OFF, LED2: Slow blink */
      green_led_off();
      if ((tick / 10) % 2 == 0)
        red_led_on();
      else
        red_led_off();
      break;

    case LED_STATE_UPDATE_SUCCESS: {
      /* Blink both LEDs 3× (300ms ON/OFF) */
      uint32_t local_tick = 0;

      while (local_tick < 18) { // 300ms ON + 300ms OFF = 6 ticks
        if ((local_tick / 3) % 2 == 0) {
          green_led_on();
          red_led_on();
        } else {
          green_led_off();
          red_led_off();
        }

        k_msleep(LED_TICK_MS);
        local_tick++;
      }

      green_led_off();
      red_led_off();

      /* After success pattern, return to NORMAL */
      /* Restore correct runtime state */
      if (is_ble_connected()) {
        atomic_set(&current_state, LED_STATE_BLE_CONNECTED);
      } else {
        atomic_set(&current_state, LED_STATE_NORMAL_APP);
      }
      break;
    }

    case LED_STATE_UPDATE_FAILED:
      /* LED1: OFF, LED2: ON */
      green_led_off();
      red_led_on();
      break;

    default:
      break;
    }

    tick++;
    k_msleep(LED_TICK_MS);
  }
}
/**
 * @brief Set LED indication state.
 *
 * Updates the current LED state in a thread-safe manner.
 * The LED indication thread will reflect the new state
 * on next scheduling cycle.
 *
 * @param state New LED state to apply.
 */
void led_set_state(led_state_t state) { atomic_set(&current_state, state); }