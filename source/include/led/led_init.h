#ifndef _LED_INIT_H
#define _LED_INIT_H

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define LED_STACK_SIZE 512
#define LED_PRIORITY 7
/* Sleep time */
#define SLEEP_TIME_MS 1000
#define LED_OFF 0
#define LED_ON 1
#define RED_LED_NODE DT_NODELABEL(led_red)
#define GREEN_LED_NODE DT_NODELABEL(led_green)
#define LED_TICK_MS 100

typedef enum {
  LED_STATE_NORMAL_APP,
  LED_STATE_BLE_CONNECTED,
  LED_STATE_FOTA_RECEIVING,
  LED_STATE_FLASH_WRITE,
  LED_STATE_VERIFICATION,
  LED_STATE_UPDATE_SUCCESS,
  LED_STATE_UPDATE_FAILED,
} led_state_t;

typedef enum {
  BLE_NOT_CONNECTED = 0,
  BLE_CONNECTED = 1,
} ble_state;

int32_t led_init(void);
void led_set_state(led_state_t state);
void led_ind_thread(void *a, void *b, void *c);
#endif /* _LED_INIT_H */