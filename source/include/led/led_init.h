#ifndef _LED_INIT_H
#define _LED_INIT_H

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/* External declaration of the LED synchronization semaphore.*/
extern struct k_sem led_sem;

/**
 * @brief Stack size allocated for LED indication thread.
 */
#define LED_STACK_SIZE 512

/**
 * @brief Priority assigned to LED indication thread.
 */
#define LED_PRIORITY 7

/**
 * @brief LED control macros.
 */
#define LED_OFF 0
#define LED_ON 1

/**
 * @brief Device Tree node labels for LEDs.
 */
#define RED_LED_NODE DT_NODELABEL(led_red)
#define GREEN_LED_NODE DT_NODELABEL(led_green)

/**
 * @brief LED tick interval in milliseconds.
 *
 * Defines the periodic interval used for LED state updates
 * (e.g., blinking patterns).
 */
#define LED_TICK_MS 120

/**
 * @brief LED indication states.
 *
 * Defines different system states represented through
 * LED patterns.
 */
typedef enum {
  LED_STATE_NORMAL_APP,      /**< Normal application running */
  LED_STATE_BLE_CONNECTED,   /**< BLE connected */
  LED_STATE_MODEL_RECEIVING, /**< Model/firmware receiving over BLE */
  LED_STATE_FLASH_WRITE,     /**< Flash write in progress */
  LED_STATE_UPDATE_SUCCESS,  /**< Update completed successfully */
  LED_STATE_UPDATE_FAILED,   /**< Update failed */
  LED_STATE_LEARN_SPEAK_NOW, /**< Learning: prompting user to speak (Red ON) */
  LED_STATE_KEYWORD_TRIGGERED, /**< Inference: keyword detected (Red short
                                  flash) */
} led_state_t;

/**
 * @brief BLE connection state.
 */
typedef enum {
  BLE_NOT_CONNECTED = 0, /**< BLE not connected */
  BLE_CONNECTED = 1,     /**< BLE connected */
} ble_state;

/**
 * @brief Initialize LED GPIOs and create LED indication thread.
 *
 * @return 0 on success, negative error code on failure.
 */
int32_t led_init(void);

/**
 * @brief Set current LED state.
 *
 * Updates internal state variable to reflect the system status.
 *
 * @param state New LED state to be applied.
 */
void led_set_state(led_state_t state);

/**
 * @brief LED indication thread entry function.
 *
 * Handles LED patterns based on the current system state.
 *
 * @param a Unused
 * @param b Unused
 * @param c Unused
 */
void led_ind_thread(void *a, void *b, void *c);

void process_led(void);

/* Get current BLE connection status. */
bool is_ble_connected(void);

/**
 * @brief Notify the LED subsystem of a BLE connection state change.
 *
 * @param connected BLE_CONNECTED or BLE_NOT_CONNECTED
 */
void ble_connection_callback(bool connected);

#endif /* _LED_INIT_H */
