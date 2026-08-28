#include "gpio/gpio.h"
#include <errno.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio, LOG_LEVEL_DBG);

static bool irq_enabled     =    false;

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

static const struct gpio_dt_spec enable_akd = GPIO_DT_SPEC_GET(AKD_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_acc = GPIO_DT_SPEC_GET(ACC_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_pdm = GPIO_DT_SPEC_GET(PDM_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_akd_0V = GPIO_DT_SPEC_GET(AKD_0V_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_camera = GPIO_DT_SPEC_GET(CAM_ENB_NODE, gpios);

static const struct gpio_dt_spec enable_akd_async = GPIO_DT_SPEC_GET(AKD_ASYNC_ENB_NODE, gpios);

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
 * @brief Wrapper function to give the Akida async semaphore
 *
 * Lets a caller other than the ISR wake the async thread out of its wait, which
 * is how akida_init() gets it to leave its loop promptly at teardown instead of
 * waiting out the full timeout.
 */
void akd_async_sem_give(void) {
    k_sem_give(&akd_async_sem);
}

/**
 * @brief Wrapper function to reset the Akida async semaphore to empty
 *
 * The semaphore's count survives the thread that was waiting on it, so a
 * teardown that gives it to unblock that thread leaves a token behind whenever
 * the thread happened to be elsewhere. Resetting establishes the invariant a
 * newly created async thread depends on: it starts with no inherited token, so
 * its first take blocks rather than firing a fetch against an empty queue.
 */
void akd_async_sem_reset(void) {
    k_sem_reset(&akd_async_sem);
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

static void akd_async_isr_handler(const struct device* dev, struct gpio_callback* cb,
                                  uint32_t pins) {
    if (akd_in_learning()) {
        schedule_akd_learning_wq();
    } else {
        k_sem_give(&akd_async_sem);
    }
}
/* ---------------------------------------------------------------------------
 * AKD1500 wake reference count
 *
 * The active-high SLEEP pin gates the AKD1500's internal clocks (state and
 * programmed model are retained). This file is its ONLY writer: everyone else
 * takes a wake reference for as long as they need the chip running.
 *
 * The count and the pin are guarded by the same spinlock so they can never
 * disagree, and so the settle below cannot be skipped by a second holder that
 * arrives while the first is still waiting it out.
 * ------------------------------------------------------------------------ */
static struct k_spinlock akd_wake_lock;
static unsigned int akd_wake_refs;

/* Monotonic tallies of what the count actually did, so a caller that holds a
 * reference across a long operation can report the contention it survived from
 * observed state rather than inferring it from console line ordering:
 *   akd_wake_releases - every reference actually handed back;
 *   akd_wake_gates    - every 1->0 transition, i.e. every time SLEEP was
 *                       asserted. A holder that samples this either side of its
 *                       own operation and sees a change has had the chip
 *                       clock-gated underneath it, which is a broken refcount.
 * Both wrap at UINT32_MAX; callers use unsigned differences, which stay correct
 * across the wrap. */
static uint32_t akd_wake_releases;
static uint32_t akd_wake_gates;

/**
 * @brief Take a wake reference; de-asserts SLEEP on the 0->1 transition.
 */
void akd_wake_get(void) {
    k_spinlock_key_t key = k_spin_lock(&akd_wake_lock);
    if (akd_wake_refs++ == 0) {
        gpio_pin_set_dt(&akd_lp, 0);
        /* AN-002 asks for a short settle after de-asserting SLEEP and before the
         * first transaction; CONFIG_AKD_WAKE_SETTLE_US explains how it is sized.
         * Deliberately inside the critical section: a second holder that arrives
         * mid-settle then blocks here rather than returning to a chip whose clocks
         * have not restarted. It costs that holder the tail of one settle, which is
         * negligible beside the accesses this protects. */
        k_busy_wait(CONFIG_AKD_WAKE_SETTLE_US);
    }
    k_spin_unlock(&akd_wake_lock, key);
}

/**
 * @brief Return a wake reference; asserts SLEEP on the 1->0 transition.
 *
 * Returning a reference that was never taken is a programming error (some
 * holder released twice, or released one it did not own), not a runtime
 * condition, so it is reported and clamped at zero rather than wrapping the
 * count and pinning the chip awake forever.
 */
void akd_wake_put(void) {
    k_spinlock_key_t key = k_spin_lock(&akd_wake_lock);
    if (akd_wake_refs == 0) {
        k_spin_unlock(&akd_wake_lock, key);
        LOG_ERR("akd_wake_put() with no reference held");
        __ASSERT_NO_MSG(false);
        return;
    }
    akd_wake_releases++;
    if (--akd_wake_refs == 0) {
        gpio_pin_set_dt(&akd_lp, 1);
        akd_wake_gates++;
    }
    k_spin_unlock(&akd_wake_lock, key);
}

/**
 * @brief Number of outstanding wake references.
 */
unsigned int akd_wake_count(void) {
    k_spinlock_key_t key = k_spin_lock(&akd_wake_lock);
    unsigned int refs = akd_wake_refs;
    k_spin_unlock(&akd_wake_lock, key);
    return refs;
}

/**
 * @brief Monotonic count of wake references handed back.
 */
uint32_t akd_wake_release_count(void) {
    k_spinlock_key_t key = k_spin_lock(&akd_wake_lock);
    uint32_t releases = akd_wake_releases;
    k_spin_unlock(&akd_wake_lock, key);
    return releases;
}

/**
 * @brief Monotonic count of 1->0 transitions, i.e. of SLEEP being asserted.
 */
uint32_t akd_wake_gate_count(void) {
    k_spinlock_key_t key = k_spin_lock(&akd_wake_lock);
    uint32_t gates = akd_wake_gates;
    k_spin_unlock(&akd_wake_lock, key);
    return gates;
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
    gpio_init_callback(&akd_async_cb, akd_async_isr_handler, BIT(enable_akd_async.pin));
    err = gpio_add_callback(enable_akd_async.port, &akd_async_cb);
    if (err) {
        LOG_ERR("Failed to add AKD ASYNC callback (err %d)", err);
        return err;
    }

    /* The akd_lp configure above leaves the chip AWAKE, so the count has to start
     * at 1 or it would disagree with the hardware. That initial 1 is the boot
     * path's own wake reference: boot programs the model over the AKD1500 and
     * needs it running throughout. It is not a stray increment - removing it
     * clock-gates the chip mid-boot.
     *
     * Taken last, on the success path only, so the contract is exact: this
     * function returns 0 if and only if the boot reference is held. main() owns
     * it from there and hands it back however its boot sequence ends; a caller
     * that sees a non-zero return has nothing to release. */
    akd_wake_refs = 1;

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