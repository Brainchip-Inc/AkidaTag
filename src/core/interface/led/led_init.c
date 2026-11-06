#include "led/led_init.h"
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include "ble_services/ble_initialization.h"
#include "akd_spi_flash_handler.h"
#include "fuel_gauge/fuel_gauge.h"
#ifdef CONFIG_DK_BOARD
#define RUN_STATUS_LED DK_LED1
#endif


LOG_MODULE_REGISTER(led_init, LOG_LEVEL_INF);
/*
 * Global semaphore used to synchronize the LED indication thread with
 * dmic_capture_thread
 */
K_SEM_DEFINE(led_sem, 0, 1);

/* GPIO specs */
static const struct gpio_dt_spec led_white = GPIO_DT_SPEC_GET(led_white_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(led_green_NODE, gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(led_red_NODE, gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(led_blue_NODE, gpios);

static atomic_t current_state = ATOMIC_INIT(LED_STATE_NORMAL_APP);
static atomic_t ble_connected = ATOMIC_INIT(0);

#ifdef CONFIG_DK_BOARD
static int blink_status = 0;
#endif
/**
 * @brief Turn ON the WHITE LED.
 *
 * Sets the white LED GPIO to active state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_white_on(void) { return gpio_pin_set_dt(&led_white, LED_ON); }
/**
 * @brief Turn OFF the WHITE LED.
 *
 * Sets the white LED GPIO to inactive state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_white_off(void) { return gpio_pin_set_dt(&led_white, LED_OFF); }

/**
 * @brief Turn ON the RGB GREEN LED.
 *
 * Sets the RGB green LED GPIO to active state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_green_on(void) { return gpio_pin_set_dt(&led_green, LED_ON); }

/**
 * @brief Turn OFF the RGB GREEN LED.
 *
 * Sets the RGB green LED GPIO to inactive state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_green_off(void) { return gpio_pin_set_dt(&led_green, LED_OFF); }

/**
 * @brief Turn ON the RGB RED LED.
 *
 * Sets the RGB red LED GPIO to active state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_red_on(void) { return gpio_pin_set_dt(&led_red, LED_ON); }

/**
 * @brief Turn OFF the RGB RED LED.
 *
 * Sets the RGB red LED GPIO to inactive state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_red_off(void) { return gpio_pin_set_dt(&led_red, LED_OFF); }

/**
 * @brief Turn ON the RGB BLUE LED.
 *
 * Sets the RGB blue LED GPIO to active state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_blue_on(void) { return gpio_pin_set_dt(&led_blue, LED_ON); }

/**
 * @brief Turn OFF the RGB BLUE LED.
 *
 * Sets the RGB blue LED GPIO to inactive state.
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_blue_off(void) { return gpio_pin_set_dt(&led_blue, LED_OFF); }

/** @brief Turn off all RGB channels. */
static void rgb_all_off(void) {
  led_green_off();
  led_red_off();
  led_blue_off();
}

/** @brief Show solid green (others off). */
static void rgb_green_on(void)  {
  rgb_all_off();
  led_green_on();
}

/** @brief Show solid red (others off). */
static void rgb_red_on(void)    {
  rgb_all_off();
  led_red_on();
}

/** @brief Show solid blue (others off). */
static void rgb_blue_on(void)   {
  rgb_all_off();
  led_blue_on();
}

/** @brief Show amber (red + green). */
static void rgb_amber_on(void)  {
  rgb_all_off();
  led_red_on();
  led_green_on();
}

/** @brief Show purple (red + blue). */
static void rgb_purple_on(void) {
  rgb_all_off();
  led_red_on();
  led_blue_on();
}

/** @brief Show yellow (red + green. */
static void rgb_yellow_on(void) {
  rgb_all_off();
  led_green_on();
  led_red_on();
}
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
bool is_ble_connected(void) {
    return atomic_get(&ble_connected);
}
/**
 * @brief Initialize all LED GPIOs (white + RGB).
 *
 * Verifies each LED GPIO device is ready, then configures every pin as
 * GPIO_OUTPUT_INACTIVE.
 *
 * @return 0 on success.
 * @return -ENODEV if any LED GPIO device is not ready.
 * @return negative errno on GPIO configuration failure.
 */
int32_t led_init(void) {

  int ret;

  if (!gpio_is_ready_dt(&led_white)) {
    LOG_ERR(" WHITE LED GPIO not ready (port=%s, pin=%d)\n",
           led_white.port->name, led_white.pin);
    return -ENODEV;
  }

  if (!gpio_is_ready_dt(&led_green)) {
    LOG_ERR(" RGB GREEN LED GPIO not ready (port=%s, pin=%d)\n",
           led_green.port->name, led_green.pin);
    return -ENODEV;
  }

  if (!gpio_is_ready_dt(&led_red)) {
    LOG_ERR(" RGB RED LED GPIO not ready (port=%s, pin=%d)\n", led_red.port->name,
           led_red.pin);
    return -ENODEV;
  }

  if (!gpio_is_ready_dt(&led_blue)) {
    LOG_ERR(" RGB BLUE LED GPIO not ready (port=%s, pin=%d)\n", led_blue.port->name,
           led_blue.pin);
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(&led_white, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    LOG_ERR(" Configuration failed WHITE LED (err=%d)\n", ret);
    return ret;
  }

  ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    LOG_ERR(" Configuration failed RGB GREEN (err=%d)\n", ret);
    return ret;
  }

  ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    LOG_ERR(" Configuration failed RGB RED (err=%d)\n", ret);
    return ret;
  }

  ret = gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
  if (ret < 0) {
    LOG_ERR(" Configuration failed RGB BLUE (err=%d)\n", ret);
    return ret;
  }
  LOG_INF(" LED INITIALIZATION SUCCESS\n");

  return 0;
}
/**
 * @brief Restore a transient state back to the runtime state.
 *
 * Expected to BLE_CONNECTED (if connected) or NORMAL_APP.
 * No-op if another thread already changed the state.
 *
 * @param expected Transient state to replace.
 */
static void restore_runtime_state(led_state_t expected) {
    atomic_cas(&current_state, expected,
               is_ble_connected() ? LED_STATE_BLE_CONNECTED : LED_STATE_NORMAL_APP);
}

/**
 * @brief Restore a learn-result state back to LEARN_LISTENING.
 *
 * Used after LEARN_SUCCESS/LEARN_FAILED so the device returns to
 * learn-select, not NORMAL_APP.
 *
 * @param expected Transient state to replace.
 */
static void restore_learn_select_state(led_state_t expected) {
    atomic_cas(&current_state, expected, LED_STATE_LEARN_LISTENING);
}
/**
 * @brief LED indication thread.
 *
 * This thread continuously monitors the current LED state
 * and updates LED patterns accordingly.
 *
 * State behavior:
 * - NORMAL_APP          : Green slow pulse ; White slow blinks 
 * - BLE_CONNECTED       : White ON; Green pulses.
 * - BLE_DISCONNECTED    : White slow blinks 
 * - MODEL_RECEIVING     : White fast blink (~500ms); RGB per BLE status.
 * - MODEL_UPDATE_SUCCESS: White per BLE + 3 green flashes, then restore.
 * - MODEL_UPDATE_FAILED : White per BLE + 3 red flashes, then restore.
 * - LEARN_LISTENING     : Blue fast blink (~500ms); White per BLE/adv.
 * - LEARN_PROGRESS      : Blue ON; White per BLE/adv.
 * - SPEECH_DETECTED     : RGB off; White per BLE/adv.
 * - LEARN_SUCCESS       : White per BLE status + 2 green flashes, then
 *                          restore to LEARN_LISTENING.
 * - LEARN_FAILED        : White per BLE status + 3 red flashes, then
 *                          restore to LEARN_LISTENING.
 * - KEYWORD_TRIGGERED   : Red ~500ms flash, then restore runtime state.
 * - FIRMWARE_UPDATE     : Purple slow blink (~2s).
 * - FULL_ERASE          : Yellow fast blink; White per BLE/adv.
 * - LOW_BATTERY         : Amber 1s-on / 3s-off; White per BLE/adv;
 *                          auto-restores when battery soc greater than 50%.
 *
 * Synchronization:
 * The thread waits for a signal from DMIC thread using a semaphore.
 * If the semaphore is given (k_sem_give), the LED thread wakes immediately
 * and updates the LED state. If no signal is received within LED_TICK_MS,
 * the wait call times out and the LED thread continues execution using the
 * timeout as a fallback timing mechanism.
 *
 * This allows the LED logic to operate in two modes:
 * 1) Synchronized with DMIC thread (when a signal is provided)
 * 2) Independently using a periodic timeout if that thread is not running
 *
 * @param a Unused
 * @param b Unused
 * @param c Unused
 */

void led_ind_thread(void* a, void* b, void* c) {
    if (led_init() < 0) {
        LOG_ERR("LED init failed");
        return;
    }
    /* Device powered on  (Solid, ~1 sec) */
    led_white_on();
    k_msleep(2000);
    led_white_off();
    uint32_t tick = 0;

    while (1) {
        /* Wait for a signal from DMIC thread to update the LED state. */
        k_sem_take(&led_sem, K_MSEC(LED_TICK_MS));

        led_state_t state = atomic_get(&current_state);

        switch (state) {
            case LED_STATE_NORMAL_APP:
                /* Green : Slow pulse , White: Advertising Slow blink  */
                button_locked = false;
                if(erase_complete == false){
                    led_set_state(LED_STATE_FULL_ERASE);
                }
                if(low_bat == true){
                    led_set_state(LED_STATE_LOW_BATTERY);
                    break;
                }
                rgb_all_off();
                if ((tick / 10) % 2 == 0){
                    if(adv_started == true){
                        led_white_on();
                    }
                    rgb_green_on();
                }else{
                    led_white_off();
                    rgb_all_off();
                }
                
                break;

            case LED_STATE_BLE_CONNECTED:
                /* White: ON*/
                if(erase_complete == false){
                    led_set_state(LED_STATE_FULL_ERASE);
                }
                if(low_bat == true){
                    led_set_state(LED_STATE_LOW_BATTERY);
                    break;
                }
                rgb_all_off();
                led_white_on();
                if ((tick / 10) % 2 == 0){
                    rgb_green_on();
                }else{
                    rgb_all_off();
                }
                break;

            case LED_STATE_BLE_DISCONNECTED:
                /* White: Fast blink*/
                if(erase_complete == false){
                    led_set_state(LED_STATE_FULL_ERASE);
                }
                if(low_bat == true){
                    led_set_state(LED_STATE_LOW_BATTERY);
                    break;
                }
                if ((tick / 1) % 2 == 0) {
                    led_white_on();
                } else {
                    led_white_off();
                }
                break;

            case LED_STATE_MODEL_RECEIVING:
                /* White: Fast blink (500ms), RGB off until success/failure */
                rgb_all_off();
                if ((tick / 2) % 2 == 0)
                    led_white_on();
                else
                    led_white_off();
                break;

            case LED_STATE_MODEL_UPDATE_SUCCESS:
                // Active BLE link to mobile app 
                if (is_ble_connected()) {
                    led_white_on();
                } else if ((tick / 10) % 2 == 0) {
                    led_white_on();
                } else {
                    led_white_off();
                }
                /* Green, 3 quick flashes, then restore normal/BLE state */
                for (int i = 0; i < 3; i++) {
                    rgb_green_on();
                    k_msleep(300);
                    rgb_all_off();
                    k_msleep(300);
                }
                restore_runtime_state(state);
                break;

            case LED_STATE_MODEL_UPDATE_FAILED:
                // Active BLE link to mobile app 
                  if (is_ble_connected()) {
                    led_white_on();
                  } else if ((tick / 10) % 2 == 0) {
                    led_white_on();
                  } else {
                    led_white_off();
                  }
                /* Red, 3 quick flashes, then restore normal/BLE state */
                for (int i = 0; i < 3; i++) {
                    rgb_red_on();
                    k_msleep(150);
                    rgb_all_off();
                    k_msleep(150);
                }
                restore_runtime_state(state);
                break;


            case LED_STATE_LEARN_LISTENING:
                // Active BLE link to mobile app
               if (is_ble_connected()) {
                    led_white_on();
                }else{
                    if ((tick / 10) % 2 == 0){
                        if(adv_started == true){
                            led_white_on();
                        }
                    }else{
                        led_white_off();
                    }
                }
                /* Blue fast blink (~500ms) - capture armed, prompting user to speak */
                if ((tick / 2) % 2 == 0) {
                    // rgb_blue_on();
                    rgb_blue_on();
                } else {
                    rgb_all_off();
                }
                break;
            
            case LED_STATE_LEARN_PROGRESS:
                // Active BLE link to mobile app 
                if (is_ble_connected()) {
                    led_white_on();
                }else{
                    if ((tick / 10) % 2 == 0){
                        if(adv_started == true){
                            led_white_on();
                        }
                    }else{
                        led_white_off();
                    }
                }
                rgb_blue_on();
                
                break;
            
            case LED_STATE_SPEECH_DETECTED:
                // Active BLE link to mobile app 
                if (is_ble_connected()) {
                    led_white_on();
                } else if ((tick / 10) % 2 == 0) {
                    led_white_on();
                } else {
                    led_white_off();
                }
                rgb_all_off();
                break;

            case LED_STATE_LEARN_SUCCESS:
                // Active BLE link to mobile app 
                if (is_ble_connected()) {
                    led_white_on();
                } else if ((tick / 10) % 2 == 0) {
                    led_white_on();
                } else {
                    led_white_off();
                }
                /* Green, 2 quick flashes, then restore normal/BLE state */
                for (int i = 0; i < 2; i++) {
                    rgb_green_on();
                    k_msleep(300);
                    led_green_off();
                    k_msleep(300);
                }
                restore_learn_select_state(state);
                break;
            
            case LED_STATE_LEARN_FAILED:
                // Active BLE link to mobile app 
                if (is_ble_connected()) {
                    led_white_on();
                } else if ((tick / 10) % 2 == 0) {
                    led_white_on();
                } else {
                    led_white_off();
                }
                /* Red, 3 quick flashes, then restore normal/BLE state */
                for (int i = 0; i < 3; i++) {
                    rgb_red_on();
                    k_msleep(150);
                    rgb_all_off();
                    k_msleep(150);
                }
                restore_learn_select_state(state);
                break;
            
            case LED_STATE_KEYWORD_TRIGGERED:
                /* Single ~500ms red flash; preserve White's behavior */
                bool ble = is_ble_connected();
                if (ble) {
                    led_white_on();
                }
                rgb_red_on(); 
                k_msleep(500);
                restore_runtime_state(LED_STATE_KEYWORD_TRIGGERED);
                if(button_locked){
                    led_set_state(LED_STATE_FIRMWARE_UPDATE);
                }
                break;

            case LED_STATE_FIRMWARE_UPDATE:
                if ((tick / 10) % 2 == 0) {
                    rgb_purple_on();
                } else {
                    rgb_all_off();
                }
                break;
                     
            case LED_STATE_FULL_ERASE:
                /* Yellow: Fast blink*/
                if (is_ble_connected()) {
                    led_white_on();
                }else{
                    if ((tick / 10) % 2 == 0){
                        if(adv_started == true){
                            led_white_on();
                        }
                    }else{
                        led_white_off();
                    }
                }
                /* No sleeps here: tick must advance at the same rate as in
                 * NORMAL_APP so the white blink timing matches */
                if ((tick / 1) % 2 == 0) {
                    rgb_yellow_on();
                } else {
                    rgb_all_off();
                }
                break;

            case LED_STATE_LOW_BATTERY:
                /* Battery recovered: leave the state */
                if (low_bat == false) {
                    restore_runtime_state(state);
                    break;
                }
                /* Time-based phase: 1 s on, 3 s off */
                bool lb_on = (k_uptime_get() % 4000) < 1000;

                /* White: solid when connected, blink while advertising */
                if (is_ble_connected()) {
                    led_white_on();
                } else if (lb_on && adv_started == true) {
                    led_white_on();
                } else {
                    led_white_off();
                }
                /* Amber: same phase as the white blink */
                if (lb_on) {
                    rgb_amber_on();
                } else {
                    rgb_all_off();
                }
                if(button_locked){
                    led_set_state(LED_STATE_FIRMWARE_UPDATE);
                }
                break;

            default:
                break;
        }

        tick++;
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
void led_set_state(led_state_t state) {
    atomic_val_t cur;

    do {
        cur = atomic_get(&current_state);

        /* While receiving a model, only a final result may change the state */
        if (cur == LED_STATE_MODEL_RECEIVING &&
            state != LED_STATE_MODEL_UPDATE_SUCCESS &&
            state != LED_STATE_MODEL_UPDATE_FAILED) {
            return;
        }

        /* Let the success/failure flashes finish; the LED thread restores
         * the runtime state itself afterwards */
        if ((cur == LED_STATE_MODEL_UPDATE_SUCCESS ||
             cur == LED_STATE_MODEL_UPDATE_FAILED) &&
            (state == LED_STATE_NORMAL_APP ||
             state == LED_STATE_BLE_CONNECTED ||
             state == LED_STATE_BLE_DISCONNECTED ||
             state == LED_STATE_FULL_ERASE ||
             state == LED_STATE_LOW_BATTERY)) {
            return;
        }
    } while (!atomic_cas(&current_state, cur, state));
}
/**
 * @brief Toggle the run status LED to indicate system is alive
 *
 * Pure LED toggle — no sleep. The caller is responsible for pacing
 * (e.g. a k_msleep in the worker loop). On AkidaTag this is a no-op since
 * there is no dedicated run status LED.
 */
void process_led(void) {
#ifdef CONFIG_DK_BOARD
    dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
#endif
}