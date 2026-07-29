#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "button/user_button.h"

/* ---------- Buttons ---------- */
#define USER_BTN_NODE DT_ALIAS(user_button)

static const struct gpio_dt_spec user_btn =
    GPIO_DT_SPEC_GET(USER_BTN_NODE, gpios);

static struct gpio_callback user_cb;

/* ---------- State ---------- */
static struct k_timer hold_timer;
static struct k_timer double_click_timer;
static volatile bool hold_fired;
static volatile int click_count;

/**
 * @brief Timer handler for hold detection.
 *
 * Called when button held for HOLD_TIME_MS. Sets hold_fired, clears
 * click_count, stops double-click timer, and prints hold message.
 *
 * @param timer.
 */
static void hold_timer_handler(struct k_timer *timer) {
  hold_fired = true;
  click_count = 0;
  k_timer_stop(&double_click_timer);
  printk("User button hold pressed\n");
}
/**
 * @brief Timer handler for double-click detection window.
 *
 * Called when timer expires. Reports single press if click_count == 1,
 * then resets click_count.
 *
 * @param timer.
 */
static void double_click_timer_handler(struct k_timer *timer) {
  if (click_count == 1) {
    printk("User button pressed\n");
  }
  click_count = 0;
}

/**
 * @brief GPIO interrupt callback for user button.
 *
 * Called on both edges. On press: starts hold timer. On release: stops hold
 * timer, increments click count. First click starts double‑click timer; second
 * click within window reports double press. If hold timer expires before
 * release, reports hold.
 *
 * @param dev  The GPIO device that triggered the interrupt (unused).
 * @param cb   Callback structure (unused).
 * @param pins Bitmask of pins that triggered the interrupt (unused).
 */
static void user_pressed(const struct device *dev, struct gpio_callback *cb,
                         uint32_t pins) {
  int state = gpio_pin_get_dt(&user_btn); /* 1 = pressed, 0 = released */

  if (state) {
    /* ---- Button just pressed ---- */
    k_timer_start(&hold_timer, K_MSEC(HOLD_TIME_MS), K_NO_WAIT);
  } else {
    /* ---- Button just released ---- */
    k_timer_stop(&hold_timer);

    if (hold_fired) {
      /* hold already reported for this press, ignore the release */
      hold_fired = false;
      return;
    }

    click_count++;

    if (click_count == 1) {
      /* wait to see if a second press/release follows */
      k_timer_start(&double_click_timer, K_MSEC(DOUBLE_CLICK_MS), K_NO_WAIT);
    } else if (click_count == 2) {
      /* second release arrived within the window: it's a double press */
      k_timer_stop(&double_click_timer);
      click_count = 0;
      printk("User button double pressed\n");
    }
  }
}

/**
 * @brief Initialize the user button GPIO and interrupt handling.
 *
 * This function performs the complete setup for the user button:
 *  - Verifies that the button GPIO device is ready.
 *  - Configures the GPIO pin as an input.
 *  - Enables interrupts on both edges (rising and falling) to detect press and
 * release events.
 *  - Initializes the hold and double-click timers.
 *  - Registers the interrupt callback for the button.
 *
 * @return 0 on success
 * @return -ENODEV if the GPIO device is not ready
 * @return negative errno on GPIO configuration or callback registration failure
 */
int user_button_init(void) {
  int err;

  if (!gpio_is_ready_dt(&user_btn)) {
    printk("User button GPIO not ready\n");
    return -ENODEV;
  }

  /* Configure button as input */
  err = gpio_pin_configure_dt(&user_btn, GPIO_INPUT);
  if (err) {
    printk("Failed to configure user button (err %d)\n", err);
    return err;
  }

  /* Configure interrupt on BOTH edges so we can detect press and release */
  err = gpio_pin_interrupt_configure_dt(&user_btn, GPIO_INT_EDGE_BOTH);
  if (err) {
    printk("Failed to configure user button interrupt (err %d)\n", err);
    return err;
  }

  k_timer_init(&hold_timer, hold_timer_handler, NULL);
  k_timer_init(&double_click_timer, double_click_timer_handler, NULL);

  /* Register callback */
  gpio_init_callback(&user_cb, user_pressed, BIT(user_btn.pin));
  err = gpio_add_callback(user_btn.port, &user_cb);
  if (err) {
    printk("Failed to add user button callback (err %d)\n", err);
    return err;
  }

  return 0;
}