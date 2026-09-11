#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "button/user_button.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(user_button, LOG_LEVEL_DBG);

/* ---------- Buttons ---------- */
#define USER_BTN_NODE DT_ALIAS(user_button)

static const struct gpio_dt_spec user_btn =
    GPIO_DT_SPEC_GET(USER_BTN_NODE, gpios);

static struct gpio_callback user_cb;

/* ---------- Button Callback ---------- */
static void user_pressed(const struct device *dev, struct gpio_callback *cb,
                         uint32_t pins) {
  LOG_INF("User button pressed");
}

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
  err = gpio_pin_interrupt_configure_dt(&user_btn, GPIO_INT_EDGE_TO_ACTIVE);
  if (err) {
    LOG_ERR("Failed to configure user button interrupt (err %d)", err);
    return err;
  }

  /* Register callback */
  gpio_init_callback(&user_cb, user_pressed, BIT(user_btn.pin));
  err = gpio_add_callback(user_btn.port, &user_cb);
  if (err) {
    LOG_ERR("Failed to add user button callback (err %d)", err);
    return err;
  }

  return 0;
}