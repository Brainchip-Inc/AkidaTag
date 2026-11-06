#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include "led/led_init.h"
#include "button/user_button.h"
#include <stdbool.h>
#include "littlefs_storage.h"
#include "akd_spi_flash_handler.h"
#include "ble_services/ble_initialization.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(user_button, LOG_LEVEL_INF);
extern int kws_edge_trigger(int val);
/* ---------- Buttons ---------- */
#define USER_BTN_NODE DT_ALIAS(user_button)

static const struct gpio_dt_spec user_btn =
    GPIO_DT_SPEC_GET(USER_BTN_NODE, gpios);

static struct gpio_callback user_cb;

/* ---------- State ---------- */
static struct k_timer hold_timer_short;
static struct k_timer click_window_timer;
static volatile bool hold_fired;
static volatile int click_count;
static volatile int64_t press_start_ms;
static struct k_work toggle_work;
static volatile int toggle_work_arg;
static struct k_work long_press_work;
static struct k_work adv_start_work;
static bool start_learing = false;
/**
 * @brief Timer handler for hold detection.
 *
 * Called when button held for HOLD_TIME_MS_SHORT. Sets hold_fired, clears
 * click_count, and stops the click-window timer. Does not print or classify
 * the hold — that happens on release, once actual hold duration is known.
 *
 * @param timer.
 */
static void hold_timer_short_handler(struct k_timer *timer) {
  hold_fired = true;
  click_count = 0;
  k_timer_stop(&click_window_timer);
  /* Actual tier (3-5s vs 8-10s) is decided on release, not here */
}
/**
 * @brief Timer handler for the click-classification window.
 *
 * Called when the window expires without a further press. Classifies based
 * on click_count:
 *  - 1: submit toggle_work with arg 1 (single press).
 *  - 2: if BLE is connected OR edge learning is active, submit toggle_work
 *       with arg 3; otherwise start BLE advertising (if not already started).
 *  - 3: submit toggle_work with arg 2 (triple press).
 * Resets click_count after classification.
 *
 * @param timer.
 */
static void click_window_timer_handler(struct k_timer *timer) {
  if (click_count == 1) {
    LOG_INF("User button pressed\n");
    toggle_work_arg = 1;
    k_work_submit(&toggle_work);
    } else if (click_count == 2) {
      LOG_INF("User button double pressed\n");
        if (is_ble_connected() || start_learing) {
          toggle_work_arg = 3;
          k_work_submit(&toggle_work);
        }else{
          if(!adv_started){
            k_work_submit(&adv_start_work);
          }
        }
    } else if (click_count == 3) {
      LOG_INF("User button triple pressed\n");
      toggle_work_arg = 2;
      k_work_submit(&toggle_work);
    }
    click_count = 0;
}
/**
 * @brief Deferred work handler for long-press (8-10s): mkfs then full erase.
 *
 * Runs in system work queue context — required since storage_format() and
 * spi_flash_erase_helper_func() are blocking flash operations.
 */
static void long_press_work_handler(struct k_work *work) {
  led_set_state(LED_STATE_FULL_ERASE);
  int rc = storage_format();
  if (rc != 0) {
    LOG_ERR("long_press: storage_format failed (%d)\n\r", rc);
    return;
  }
  k_msleep(2000);
  if (spi_flash_erase_helper_func(0x1000, FLASH_MAX_16_MB_SIZE - 0x1000)) {
    LOG_ERR("long_press: full erase failed\n\r");
    return;
  }
  if (is_ble_connected()) {
    led_set_state(LED_STATE_BLE_CONNECTED);
  } else {
    led_set_state(LED_STATE_NORMAL_APP);
  }
}

/**
 * @brief Deferred work handler for edge-learning trigger. toggle_work_arg
 * selects the action: 3-5s hold = 0, single press = 1, triple press = 2,
 * double press while BLE-connected = 3. Runs in work queue context.
 * For the hold action (arg 0), toggles start_learing only if kws_edge_trigger
 * accepts the request (returns 0).
 */
static void toggle_work_handler(struct k_work *work) {
    int arg = toggle_work_arg;
    int ret = kws_edge_trigger(arg);

    /* Long press toggles edge learning on/off; flip only if accepted */
    if (arg == 0 && ret == 0) {
        start_learing = !start_learing;
    }
}


/**
 * @brief Deferred work handler to start BLE advertising on double press.
 *
 * Runs in system work queue context (not ISR) since bt_le_adv_start()
 * should not be called directly from GPIO interrupt context.
 */
static void adv_start_work_handler(struct k_work *work) {
  ble_adv_start();
}
/**
 * @brief GPIO interrupt callback for user button.
 *
 * Called on both edges. If button_locked is set, logs and returns early.
 * On press: records press_start_ms and starts hold timer.
 * On release: stops hold timer; if a hold was detected, classifies it by
 * duration (3-5s = short hold -> toggle_work arg 0, 8-10s = long hold ->
 * long_press_work) and returns. Otherwise increments click_count and
 * (re)starts the click-window timer, which classifies single/double/triple
 * press once the window expires without a further press.
 *
 * @param dev  The GPIO device that triggered the interrupt (unused).
 * @param cb   Callback structure (unused).
 * @param pins Bitmask of pins that triggered the interrupt (unused).
 */
static void user_pressed(const struct device *dev, struct gpio_callback *cb,
                         uint32_t pins) {
  if (button_locked) {
    LOG_INF("User button locked\n");
    return;
  }
  int state = gpio_pin_get_dt(&user_btn); /* 1 = pressed, 0 = released */

  if (state) {
    /* ---- Button just pressed ---- */
    press_start_ms = k_uptime_get();
    k_timer_start(&hold_timer_short, K_MSEC(HOLD_TIME_MS_SHORT), K_NO_WAIT);
  } else {
    /* ---- Button just released ---- */
    k_timer_stop(&hold_timer_short);

    if (hold_fired) {
      /* Hold was detected — decide which tier based on actual duration */
      int64_t held_ms = k_uptime_get() - press_start_ms;

      if (held_ms >= HOLD_TIME_MS_LONG) {
        LOG_INF("User button long press 8-10 sec\n");
        k_work_submit(&long_press_work);
      } else if (held_ms >= HOLD_TIME_MS_SHORT) {
        LOG_INF("User button long press 3-5 sec\n");
        toggle_work_arg = 0;
        k_work_submit(&toggle_work);
      }

      hold_fired = false;
      return;
    }

    click_count++;
    k_timer_start(&click_window_timer, K_MSEC(DOUBLE_CLICK_MS), K_NO_WAIT);
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
 *  - Initializes the hold and click-window timers.
 *  - Registers the interrupt callback for the button.
 *
 * @return 0 on success
 * @return -ENODEV if the GPIO device is not ready
 * @return negative errno on GPIO configuration or callback registration failure
 */
/* ---------- Button Init ---------- */
int user_button_init(void) {
  int err;

  if (!gpio_is_ready_dt(&user_btn)) {
    LOG_ERR("User button GPIO not ready");
    return -ENODEV;
  }

  /* Configure button as input */
  err = gpio_pin_configure_dt(&user_btn, GPIO_INPUT);
  if (err) {
    LOG_ERR("Failed to configure user button (err %d)", err);
    return err;
  }

  /* Configure interrupt */
  err = gpio_pin_interrupt_configure_dt(&user_btn, GPIO_INT_EDGE_BOTH);
  if (err) {
    LOG_ERR("Failed to configure user button interrupt (err %d)", err);
    return err;
  }

  k_timer_init(&hold_timer_short, hold_timer_short_handler, NULL);
  k_timer_init(&click_window_timer, click_window_timer_handler, NULL);
  k_work_init(&toggle_work, toggle_work_handler);
  k_work_init(&long_press_work, long_press_work_handler);
  k_work_init(&adv_start_work, adv_start_work_handler);
  
  /* Register callback */
  gpio_init_callback(&user_cb, user_pressed, BIT(user_btn.pin));
  err = gpio_add_callback(user_btn.port, &user_cb);
  if (err) {
    LOG_ERR("Failed to add user button callback (err %d)", err);
    return err;
  }

  return 0;
}