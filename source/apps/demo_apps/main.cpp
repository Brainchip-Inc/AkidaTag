/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <errno.h>
#include <inttypes.h>
#include <soc.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/types.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include <akd1500/akd1500_spi_driver.h>
#include <hardware_device_impl.h>
#include <infra/system.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <cmath>
#include <new>
#include "akd_spi_flash.h"
#include "akd_spi_flash_handler.h"
#include "akida.h"
#include "akida/hardware_device.h"
#include "io_objects.h"
#include "nrf_spi.h"
#include "sample_input/kws/kws_inputs.h"

#include "infer_utils.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "audio_processor.h"
#include "ble_services/ble_initialization.h"
#include "ble_services/edge_learning.h"
#include "ble_services/file_transfer.h"
#include "boot_manager.h"
#include "error.h"
#include "kws_app.h"
#include "kws_config.h"
#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
#include "imu_h/imu.h"
#endif
/* gpio.h guards its own AkidaTag-only contents and provides no-op inlines for the
 * rest (akd_wake_get / akd_wake_put / akd_wake_count), so it is included for
 * both boards. infer() and the AKD1500 clock and sleep shell commands below
 * take wake references unguarded. */
#include "gpio/gpio.h"
#ifdef CONFIG_AKIDATAG_BOARD
#include "battery/battery.h"
#include "ble_services/battery_service.h"
#include "button/user_button.h"
#include "current_ic/current_ic.h"
#endif
#include "led_init.h"
#include "littlefs_storage.h"
#include "pdm_mic.h"
#if IS_ENABLED(CONFIG_CAMERA_ENABLE_THREAD)
#include "camera/spi_camera.h"
#endif
#if IS_ENABLED(CONFIG_WDT_ENABLE)
#include "watchdog_h/watchdog.h"
#endif
#ifdef __cplusplus
}
#endif

// Extern variables for audio processor configuration
extern int rms_threshold;
extern int speech_active_time_ms;

extern "C" {
int file_transfer_load_meta(int app_idx, model_meta_t* meta_out);
int infer(int app_index_l);
}

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

void cli_worker_proc_thread(void* a, void* b, void* c);

#ifdef CONFIG_AKIDATAG_BOARD
/* Thread for processing Akida async results */
#define AKD_ASYNC_STACK_SIZE 2048
#define AKD_ASYNC_PRIORITY 5
K_THREAD_STACK_DEFINE(akd_async_stack, AKD_ASYNC_STACK_SIZE);
static struct k_thread akd_async_thread_data;
static k_tid_t akd_async_tid;
#endif
/*
FLash offset indices
KWS - 0
*/

static void reset_kws_spectrogram(void);

/** Input batch size, picked an arbitrary value */
#define INPUT_BATCH_SIZE (CONFIG_BATCH_SIZE)
#define GET_SEC_TO_USEC(x) (x * 1000000)

#define AKIDA_FREQUENCY_MHZ 400

#define SAMPLING_RATE CONFIG_SAMPLING_RATE

#define MFCC_SAMPLE_COUNT CONFIG_MFCC_SAMPLE_COUNT

#define NUM_INA_BUFF (2)
#define NUM_INA_SAMPLES (50)
#define INA_BUFF_MESH_OFFSET(idx) (idx)
#define INA_BUFF_IO_OFFSET(idx) (NUM_INA_SAMPLES + idx)

/** Possible states of the application */
#define STATE_COUNT (4)
/** Inference mode */
#define STATE_INFERENCE (0)
/** Learning class selection mode */
#define STATE_LEARN_SELECT (1)
/** Learning mode */
#define STATE_LEARNING (2)
/** No operation mode */
#define STATE_STOPPED (3)

/** Indexes of novel classes ranges from 12-14 */
#define KWS_EDGE_NOVEL_CLASS_BASE_ID 12
#define KWS_EDGE_MAX_NOVEL_CLASS_ID 14

/** long button pressed event */
#define LONG_PRESS_EVENT 0

/** short button pressed event */
#define SHORT_PRESS_EVENT 1

#define LEARN_WEIGHTS_FILE_NAME "/ext/kwswts.bin"

#define USER_INPUT_LP(BUTTON_ID) (LONG_PRESS_EVENT + (BUTTON_ID * 2))
#define USER_INPUT_SP(BUTTON_ID) (SHORT_PRESS_EVENT + (BUTTON_ID * 2))

#define CONFIG_USER_BUTTON_COUNT 2

/** Experimentally determined energy_threshold */
#define DEFAULT_ENERGY_THRESHOLD 666
/** Experimentally determined first binary of MFCC spectogram */
#define DEFAULT_BIN0_THRESHOLD -39
/** Learning delay is set to an arbitrary value */
#define DEFAULT_LEARNING_DELAY 1000

/*Akida Async */
#define DEFAULT_API_SELECTION_ASYNC 1
/*Akida Sync */
#define DEFAULT_API_SELECTION_SYNC 0

/** Timeout for enqueue operation in milliseconds */
#define ENQUEUE_TIMEOUT_MS 5000
/** Default KWS API mode (Async) */
static uint8_t kws_api_selection = DEFAULT_API_SELECTION_ASYNC;

static uint64_t last_trigger_time_ms = 0ULL;
int verbose_on = 0;
/** Current state of the application */
static uint32_t cur_kws_edge_state = STATE_STOPPED;
/** Current novel class id, selected for learning*/
static uint32_t cur_kws_edge_novel_class = KWS_EDGE_NOVEL_CLASS_BASE_ID;

static bool is_kws_inference_started = false;

static bool kws_threads_suspended = false;

/** learn weights size */
static uint32_t mesh_learn_weights_size = 0;

/** Timestamp of last sample enqueued for learning */
static uint64_t last_learn_ts = 0;
/*---------------------------------------------------------------------------
 * Structured Edge Learning - sub-state machine, capture buffer, augmentation
 *---------------------------------------------------------------------------*/

/** Sub-states within STATE_LEARNING for structured multi-utterance flow */
typedef enum {
    LEARN_SUB_WAITING_FOR_SPEECH, /**< Prompting user, waiting for speech */
    LEARN_SUB_CAPTURING,          /**< Speech detected, accumulating MFCC frames */
    LEARN_SUB_PROCESSING,         /**< Speech ended, generating augmented samples */
    LEARN_SUB_COMPLETE            /**< All utterances done */
} learn_sub_state_t;

#define LEARN_CAPTURE_MAX_FRAMES 80 /**< ~1.6s of MFCC frames */
#define LEARN_NUM_UTTERANCES 5      /**< User must speak keyword this many times */
#define LEARN_SILENCE_TIMEOUT_MS                 \
    5000 /**< No-speech timeout before re-prompt \
          */
#define LEARN_SPEECH_END_GAP_MS                                                   \
    500                                 /**< Gap after last MFCC cb to detect end \
                                         */
#define LEARN_SPEECH_ACTIVE_TIME_MS 400 /**< Shorter VAD timeout during learning */
#define LEARN_MIN_KEYWORD_FRAMES 10     /**< ~200ms minimum utterance */
#define LEARN_NUM_AUG_TYPES 8           /**< Number of augmentation types to cycle */

typedef struct {
    learn_sub_state_t sub_state;
    uint8_t current_utterance; /**< 0 to LEARN_NUM_UTTERANCES-1 */
    bool speech_detected;
    uint16_t total_fit_calls;             /**< Running total (up to 150) */
    uint16_t augmentations_per_utterance; /**< 2 * g_num_neurons_per_class */
    uint8_t dummy[2];
    uint64_t waiting_since_ts; /**< When we started waiting for speech */
    uint64_t last_callback_ts; /**< Last time learning_on_spectrogram fired */
    float captured_mfcc[LEARN_CAPTURE_MAX_FRAMES][SPECTROGRAM_RES]; /**< ~3.2KB */
    int capture_write_idx; /**< Write index into captured_mfcc */
    int current_aug_idx;   /**< Which augmentation is currently in-flight */
    int num_augs;          /**< Total augmentations for this utterance */
    int keyword_len;       /**< Trimmed keyword length (needed by fetch handler) */
} structured_learn_state_t;

static structured_learn_state_t learn_state;

/** Saved speech_active_time_ms value to restore when leaving learning mode */
static int saved_speech_active_time_ms;

/** Spectrogram index at the time of the most recent do_inference() call */
static int current_spectrogram_index = 0;

/** Simple LCG PRNG for augmentation randomness */
static uint32_t learn_rng_state;
static float learn_rand_float(void) {
    learn_rng_state = learn_rng_state * 1664525u + 1013904223u;
    return (float)(learn_rng_state & 0xFFFF) / 65535.0f;
}

static inline uint8_t clamp_uint8(float v) {
    if (v < 0.0f)
        return 0;
    if (v > 255.0f)
        return 255;
    return (uint8_t)v;
}

/* ---------------------------------------------------------------------------
 * AKD1500 wake references held by this application
 *
 * gpio.c counts the holders that need the AKD1500 running and drives the SLEEP
 * pin off that count (see akd_wake_get). Most call sites below take a reference
 * and hand it straight back on the same path, but three holders cannot pair
 * theirs by control flow, so each gets a small owner that makes taking and
 * returning idempotent:
 *
 *  - the async KWS inference: taken on the audio thread before akida_enqueue(),
 *    handed back on akd_async_thread once the matching fetch completes, and
 *    handed back by that same thread when a fetch never arrives at all;
 *  - the learning session: taken when an utterance starts processing, handed
 *    back by kws_set_edge_state() on any transition out of STATE_LEARNING;
 *  - the shell's debug wake: taken by the clock commands that need the chip
 *    readable and parked awake afterwards, handed back by `akd_sleep 1`.
 *
 * All three are read-modify-written from different threads (audio,
 * akd_async_thread, the system workqueue, the shell) and each has to decide
 * whether to touch the gpio.c count, so the decision and the call are made
 * together under this lock. None of them run in ISR context.
 * ------------------------------------------------------------------------ */
K_MUTEX_DEFINE(akd_wake_owner_lock);

/* Take an owner's single reference, or do nothing if it already holds one. */
static void akd_wake_owner_take(bool* held) {
    k_mutex_lock(&akd_wake_owner_lock, K_FOREVER);
    if (!*held) {
        *held = true;
        akd_wake_get();
    }
    k_mutex_unlock(&akd_wake_owner_lock);
}

/* Hand an owner's reference back, or do nothing if it holds none. */
static void akd_wake_owner_release(bool* held) {
    k_mutex_lock(&akd_wake_owner_lock, K_FOREVER);
    if (*held) {
        *held = false;
        akd_wake_put();
    }
    k_mutex_unlock(&akd_wake_owner_lock);
}

static bool akd_learn_wake_held;
static bool akd_dbg_wake_held;

/* One reference per inference handed to akida_enqueue() on the async path,
 * with the moment it was taken, oldest first. Counted rather than flagged
 * because nothing stops a second utterance being enqueued while the first is
 * still in flight, and timestamped because the only safe reason to reclaim one
 * of these is that THAT enqueue has outlived its own budget.
 *
 * The AKD1500 completes enqueued inferences in order, so a fetch retires the
 * oldest entry. Which physical inference an entry belongs to does not matter -
 * what has to stay right is the number outstanding, because that is what holds
 * the pin.
 *
 * Sized well above the one or two inferences the audio thread can have in
 * flight. A reference taken while the ring is full is still taken, because the
 * chip must stay awake for it, but carries no deadline and so can never be
 * reclaimed by the timeout path: leaking a reference costs power and shows up
 * in the wake count, whereas dropping a live one clock-gates the chip
 * mid-inference and yields a wrong keyword with no error at all. */
#define AKD_ASYNC_WAKE_MAX 8

/* How long an enqueued inference may go without its completion interrupt before
 * its wake reference is treated as stranded, measured from that reference's own
 * timestamp. */
#define AKD_ASYNC_FETCH_TIMEOUT_MS 5000

static int64_t akd_async_wake_ts[AKD_ASYNC_WAKE_MAX];
static unsigned int akd_async_wake_first;
static unsigned int akd_async_wake_tracked;
static unsigned int akd_async_wake_untracked;

static void akd_async_wake_take(void) {
    bool overflow = false;

    k_mutex_lock(&akd_wake_owner_lock, K_FOREVER);
    akd_wake_get();
    if (akd_async_wake_tracked < AKD_ASYNC_WAKE_MAX) {
        unsigned int slot = (akd_async_wake_first + akd_async_wake_tracked) % AKD_ASYNC_WAKE_MAX;
        akd_async_wake_ts[slot] = k_uptime_get();
        akd_async_wake_tracked++;
    } else {
        akd_async_wake_untracked++;
        overflow = true;
    }
    k_mutex_unlock(&akd_wake_owner_lock);

    if (overflow) {
        LOG_ERR(
            "more than %d async inferences in flight; this wake reference has "
            "no deadline and will only be handed back by a fetch",
            AKD_ASYNC_WAKE_MAX);
    }
}

/* Hand back the reference belonging to one completed (or abandoned) inference,
 * retiring the oldest outstanding entry. Silent when none is outstanding, so an
 * abort path may call it without having to know whether the fetch already
 * claimed it. */
static void akd_async_wake_give(void) {
    k_mutex_lock(&akd_wake_owner_lock, K_FOREVER);
    if (akd_async_wake_tracked > 0) {
        akd_async_wake_first = (akd_async_wake_first + 1) % AKD_ASYNC_WAKE_MAX;
        akd_async_wake_tracked--;
        akd_wake_put();
    } else if (akd_async_wake_untracked > 0) {
        akd_async_wake_untracked--;
        akd_wake_put();
    }
    k_mutex_unlock(&akd_wake_owner_lock);
}

#ifdef CONFIG_AKIDATAG_BOARD
/* Hand back the reference of any enqueued inference that has outlived its own
 * AKD_ASYNC_FETCH_TIMEOUT_MS, and only those.
 *
 * akd_async_sem_take() timing out is NOT evidence that a completion interrupt
 * is missing: its window free-runs from the previous loop iteration, so it can
 * expire microseconds after a perfectly healthy enqueue. Deciding staleness
 * from the wait rather than from the enqueue is what let a live reference be
 * dropped, gating the chip mid-inference. So the deadline is per reference,
 * measured from when that reference was taken, and the entries are ordered
 * oldest first: once the head is not stale, nothing behind it is either.
 *
 * Only akd_async_thread calls this. */
static void akd_async_wake_reap_stranded(void) {
    const int64_t now = k_uptime_get();

    for (;;) {
        int64_t held_ms;

        k_mutex_lock(&akd_wake_owner_lock, K_FOREVER);
        if (akd_async_wake_tracked == 0) {
            k_mutex_unlock(&akd_wake_owner_lock);
            return;
        }
        held_ms = now - akd_async_wake_ts[akd_async_wake_first];
        if (held_ms < AKD_ASYNC_FETCH_TIMEOUT_MS) {
            k_mutex_unlock(&akd_wake_owner_lock);
            return;
        }
        akd_async_wake_first = (akd_async_wake_first + 1) % AKD_ASYNC_WAKE_MAX;
        akd_async_wake_tracked--;
        akd_wake_put();
        k_mutex_unlock(&akd_wake_owner_lock);

        LOG_WRN(
            "async fetch never arrived: handed back an AKD1500 wake reference "
            "held for %d ms",
            (int)held_ms);
    }
}

/* Reclaim every outstanding async reference regardless of deadline.
 *
 * Sound only on the teardown path. akd_async_thread is the only thing that ever
 * retires these, so once it has exited no fetch and no reap can hand them back
 * and they would pin the AKD1500 awake for the rest of the boot session - which
 * is exactly what a switch to sync mode used to do, since it stops the thread
 * and creates no replacement. Call only after the thread has been joined:
 * running this while it is alive would race its own akd_async_wake_give() and
 * release the same reference twice. */
static void akd_async_wake_reclaim_all(void) {
    unsigned int stranded;

    /* Timed rather than K_FOREVER, uniquely among the users of this lock: this is
     * the one call site that can run after the abort fallback below, and a thread
     * aborted inside a critical section keeps the mutex. Blocking here would turn
     * a power leak into a hang of the BLE model-update path that called
     * akida_init(). Uncontended this takes well under a millisecond. */
    if (k_mutex_lock(&akd_wake_owner_lock, K_MSEC(100)) != 0) {
        LOG_ERR(
            "could not reclaim async wake references: akd_wake_owner_lock is "
            "held; the AKD1500 may not sleep again");
        return;
    }
    stranded = akd_async_wake_tracked + akd_async_wake_untracked;
    for (unsigned int i = 0; i < stranded; i++) {
        akd_wake_put();
    }
    akd_async_wake_first = 0;
    akd_async_wake_tracked = 0;
    akd_async_wake_untracked = 0;
    k_mutex_unlock(&akd_wake_owner_lock);

    if (stranded) {
        LOG_WRN(
            "async mode torn down with %u inference wake reference(s) "
            "outstanding; reclaimed",
            stranded);
    }
}
#endif

/* Holds a wake reference for a scope. infer() leaves by any of ten paths and
 * every one of them has to hand the reference back. */
class AkdWakeScope {
   public:
    AkdWakeScope() {
        akd_wake_get();
    }
    ~AkdWakeScope() {
        akd_wake_put();
    }
    AkdWakeScope(const AkdWakeScope&) = delete;
    AkdWakeScope& operator=(const AkdWakeScope&) = delete;
};

/* Owns the boot wake reference that a successful gpio_init() leaves held, and
 * hands it back however main()'s boot sequence ends.
 *
 * Adopts rather than takes: gpio_init() has to start the count at 1 because it
 * is what configures akd_lp awake, so by the time main() can hold anything the
 * reference already exists.
 *
 * A single release statement is the wrong shape here. main() leaves by seven
 * early returns before the app goes idle, and the first of them is taken by a
 * board with no model in LittleFS - which is precisely the board a BLE model
 * update exists to serve, so the most common first-use path was the one that
 * pinned the AKD1500 awake for the rest of the session. A destructor cannot be
 * bypassed by an eighth early return added later.
 *
 * release() exists because main() also returns normally, long after the boot
 * sequence is over: the success path has to hand the reference back at the
 * moment the app goes idle, not when main() finally returns. Calling it twice,
 * or not at all, is safe. */
class AkdBootWakeScope {
   public:
    AkdBootWakeScope() = default;
    ~AkdBootWakeScope() {
        release();
    }
    void release(void) {
        if (held) {
            held = false;
            akd_wake_put();
        }
    }
    AkdBootWakeScope(const AkdBootWakeScope&) = delete;
    AkdBootWakeScope& operator=(const AkdBootWakeScope&) = delete;

   private:
    bool held = true;
};

/** Work items for structured learning */
static struct k_work_delayable learn_speech_end_work;
static struct k_work learn_process_work;

/* The single writer of cur_kws_edge_state.
 *
 * The learning session's wake reference is taken per utterance in
 * learn_process_handler() and can only be handed back once the state machine
 * has actually left STATE_LEARNING. Hanging that release off switch_mode()
 * alone was wrong: switch_mode() is not the only writer. kws_app_start(),
 * kws_app_stop() (which the phone reaches through CMD_DEPLOY_STOP, and seven
 * shell paths reach directly) and initiate_kws_inference() all move the state
 * themselves, and a learning session cut short by any of them stranded the
 * reference, so the AKD1500 never slept again with nothing logged to say why.
 *
 * Routing every write through here puts the release on the transition itself,
 * where no direct assignment can bypass it.
 *
 * The write happens UNDER akd_wake_owner_lock, and akd_learn_wake_take() tests
 * the state under that same lock, so the two cannot interleave: a take either
 * sees STATE_LEARNING, in which case the release below is guaranteed to find
 * the reference held, or it sees the state that replaced it and takes nothing.
 * Without that pairing a take running on the system workqueue could land just
 * after a transition driven from the BT RX or shell thread, and its reference
 * would have no route home.
 *
 * A caller that keeps touching the AKD1500 after the transition must hold a
 * reference of its own: learning_on_user_input() reads the learned weights out
 * of the Akida mesh after switch_mode() returns, which is why it opens an
 * AkdWakeScope for the whole handler. */
static void kws_set_edge_state(uint32_t next) {
    bool left_learning;

    k_mutex_lock(&akd_wake_owner_lock, K_FOREVER);
    const uint32_t prev = cur_kws_edge_state;
    cur_kws_edge_state = next;
    left_learning = (prev == STATE_LEARNING && next != STATE_LEARNING);
    if (left_learning && akd_learn_wake_held) {
        akd_learn_wake_held = false;
        akd_wake_put();
    }
    k_mutex_unlock(&akd_wake_owner_lock);

    if (left_learning) {
        /* The session is over, so no queued learning work may still run: without
         * this, kws_app_stop() left learn_speech_end_work armed and it went on
         * submitting learn_process_work into a stopped app. Cancelled outside the
         * lock, and the handlers re-check the state themselves because neither
         * cancel waits for an instance that is already running. */
        k_work_cancel_delayable(&learn_speech_end_work);
        k_work_cancel(&learn_process_work);
    }
}

/* Take the learning session's single wake reference, but only while that
 * session is still live. Returns true if the reference is held on return.
 *
 * The state test shares akd_wake_owner_lock with kws_set_edge_state(), which is
 * what makes "still live" a decision rather than a guess: see the comment
 * above. A caller that gets false must not touch the AKD1500, because nothing
 * is keeping it awake. */
static bool akd_learn_wake_take(void) {
    k_mutex_lock(&akd_wake_owner_lock, K_FOREVER);
    const bool live = (cur_kws_edge_state == STATE_LEARNING);
    if (live && !akd_learn_wake_held) {
        akd_learn_wake_held = true;
        akd_wake_get();
    }
    k_mutex_unlock(&akd_wake_owner_lock);
    return live;
}

/** Forward declarations for structured learning */
static void learn_speech_end_handler(struct k_work* work);
static void learn_process_handler(struct k_work* work);
static void complete_structured_learning(void);
/* Forward declarations */
static void learn_utterance_complete(void);
#if IS_ENABLED(CONFIG_WDT_ENABLE)
/** Handle for the watchdog device  */
static const struct device* wdt;
static int wdt_channel_id;
#endif

/**
 *  Pointer to individual novel class learn weights data.
 */
uint8_t* learn_weights_buff_ptr;

/**
 * Learn Weights structure declaration.
 */
typedef struct _learn_weights {
    /** this will hold the size of the learn weights data */
    int32_t learn_weights_size;
    /** this will tell if the present data pointed by learn_weights_buff_ptr
     * a valid learned data
     */
    uint32_t label_learnt_val;
} learn_weights;

/**
 * Saved learn Weights structure declaration.
 *
 */
typedef struct _saved_learn_weights {
    /** this will hold the CRC of the complete structure */
    uint32_t crc;
    /** this will hold the size of the data (learned weights) along with CRC to be
     * stored in flash
     */
    uint32_t total_saved_learn_weights_size;
    /** this will hold the size learn_weights data information */
    learn_weights learn_weights_data;
} saved_learn_weights;

/**
 * This pointer points saved_learn_weights structure data. The learned weights
 * are stored right after the size of saved_learn_weights structure data.
 * learn_weights_buff_ptr points to the learned weights
 * address.
 */

static saved_learn_weights* saved_learn_weights_ptr = NULL;

static int32_t save_weights_from_mesh(uint8_t* lbl_wts_ptr, uint32_t size);

/**
 * This pointer points base label weights data.
 */
static uint8_t* base_labels_wts_ptr = NULL;

/** Spectrogram holding the required spectrogram for inference (prior to
  normalization) */
static float __aligned(4) spectrogram[SPECTROGRAM_COUNT][SPECTROGRAM_RES];
/** spectrogram dimensions */
static uint8_t __aligned(4) spectrogram_dims[2] = {SPECTROGRAM_COUNT, SPECTROGRAM_RES};

/** Last class detected */
static int current_class = -1;

/** data structure to represent possible actions in a state */
typedef struct {
    /**
     * @brief Invoked in Inference/learning mode on availability of mfcc output.
     * if state machine is in Inference mode it performs inference.
     *
     * @param input       - input sample data
     * @param input_shape - shape of input data
     */
    int32_t (*on_mfcc_output)(uint8_t* input, uint32_t* input_shape);

    /**
     * @brief Invoked on user input.
     * Based on the user input identified corresponding event callback function is
     * invoked.
     *
     * @param input_type  - type of input user input identified
     */
    void (*on_user_input)(int input_type);
} kws_edge_state_processor;

/**
 * @brief  Callback functions in Inference state
 * The state machine in Inference mode, performs inference on mfcc output.
 *
 * @param input       - input sample data
 * @param input_shape - shape of input data
 *
 * @return returns the inference status
 */
static int32_t inference_on_mfcc_output(uint8_t* input, uint32_t* input_shape);

/**
 * @brief  Callback functions in Inference state
 * The state machine in Inference mode, switches the mode to STATE_LEARN_SELECT.
 *
 * @param input_type - type of user input identified
 */
static void inference_on_user_input(int input_type);

/**
 * @brief  Callback functions in Learn select state
 * The state machine in Learn select mode, performs selection of label Id.
 *
 * @param input_type - type of user input identified
 */
static void learn_select_on_user_input(int input_type);

/**
 * @brief  Callback functions in Learning state
 * The state machine in Learning mode, performs edge learning.
 *
 * @param input       - input sample data
 * @param input_shape - shape of input data
 *
 * @return int32_t returns SUCCESS
 */
static void learning_on_spectrogram(int spectrogram_index);

/**
 * @brief  Callback functions in Learning state
 * The state machine in Learning mode, changes the mode depending on passed
 * input. if the input is long press on B1, then changes mode to STATE_INFERENCE
 * else if the input is short press on B1 or B2, then changes mode to
 * STATE_LEARN_SELECT
 * @param input_type - type of user input identified
 */
static void learning_on_user_input(int input_type);

static void switch_learning_delayed(struct k_work* work);

static kws_edge_state_processor kws_edge_state[STATE_COUNT] = {
    [STATE_INFERENCE] = {inference_on_mfcc_output, inference_on_user_input},
    [STATE_LEARN_SELECT] = {NULL, learn_select_on_user_input},
    [STATE_LEARNING] = {NULL, learning_on_user_input},
};

static const uint32_t dims[] = {SPECTROGRAM_COUNT, SPECTROGRAM_RES, 1};

const unsigned char* inputs[] = {kws_inputs};

uint32_t g_num_classes = 0;
uint32_t g_num_neurons_per_class = 1;
uint32_t g_num_edge_learn_classes = 0;
uint32_t g_input_size = 0;
// int32_t akida_output[NUM_CLASSES * NUM_NEURONS_PER_CLASS] = {0};
int32_t* akida_output;
float* akida_output_dq;
uint32_t akd_op_size = 0;
static uint8_t is_el_model = 0;

/*Metadata of the loaded model used for app_info reporting*/
model_meta_t kws_meta;
model_data_meta_t kws_data_meta;

#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
/* IMU thread variables*/
K_THREAD_STACK_DEFINE(imu_stack, IMU_STACK_SIZE);
struct k_thread imu_thread;
k_tid_t imu_tid;
#endif

#ifdef CONFIG_AKIDATAG_BOARD
/* CURRENT thread variables*/
K_THREAD_STACK_DEFINE(current_stack, CURRENT_STACK_SIZE);
struct k_thread current_thread;
k_tid_t current_tid;
#endif
const struct device* wdt_dev;  // Global watchdog device
void kick_watchdog(void) {
    if (wdt_dev) {
        // wdt_feed(wdt_dev, 0); // Feed the watchdog
        LOG_INF("Watchdog fed");
    }
}

/* helper function to swap the endianness */
uint32_t swap_endian(uint32_t value) {
    return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) |
           ((value << 8) & 0x00FF0000) | ((value << 24) & 0xFF000000);
}

// int spi_flash_erase_helper_func(uint32_t offset, uint32_t size);

extern "C" int64_t time_ms() {
    return k_uptime_get();
}

extern "C" void msleep(uint32_t duration) {
    k_sleep(K_MSEC(duration));
}

extern "C" void reset_spectrogram_index(void);

void panic(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

static bool kws_model_present = false;

/**
 * @brief Function to switch operation mode of application.
 *
 * @param mode  - mode to be switched to.
 */
static void switch_mode(int mode);

#ifdef CONFIG_AKIDATAG_BOARD
/**
 * @brief Restore LED to its default runtime state based on BLE connectivity.
 *        Used to clear transient states like LEARN_SPEAK_NOW.
 */
static inline void restore_default_led_state(void) {
    led_set_state(is_ble_connected() ? LED_STATE_BLE_CONNECTED : LED_STATE_NORMAL_APP);
}
#endif

// MFCC normalisation scalar — no built-in default. It divides every input
// feature, so it must come from the model metadata (info.yaml). A KWS model
// without a valid mfcc_fs is rejected in update_model_params().
float mfcc_fs = 0.0f;

// KWS class indices — supplied by model metadata (info.yaml), not hardcoded.
// -1 means "not yet configured"; update_model_params() sets them at load time.
static int g_silence_class = -1;
static int g_unknown_class = -1;

// Softmax EMA smoothing and chiming trigger parameters
#define SMOOTHING_ALPHA 0.7f
#define SCORE_THRESHOLD 0.5f
#define CHIMING_THRESHOLD 3

float smoothing_alpha = SMOOTHING_ALPHA;    // EMA factor (0.0-1.0, higher = less smoothing)
float score_threshold = SCORE_THRESHOLD;    // Smoothed softmax score threshold
int chiming_threshold = CHIMING_THRESHOLD;  // Consecutive detections needed to trigger

static float* smoothed_scores = nullptr;
static int* chiming_counters = nullptr;
static float* softmax_scores = nullptr;

// Metrics mode: show confidence and timing details on keyword detection
int metrics_on = 0;

static int check_model_compatibility(uint8_t is_el_model_l, model_meta_t kws_meta) {
    if (is_el_model_l) {
        if (kws_meta.is_edge_learned) {
            LOG_INF(" model is edge learn capable");
        } else {
            LOG_ERR("model in-compatability, FS and model to be updated correctly");
            return -1;
        }
    } else {
        if (kws_meta.is_edge_learned == 0) {
            LOG_INF(" model is not edge learn capable");
        } else {
            LOG_ERR("model in-compatability, FS and model to be updated correctly");
            return -1;
        }
    }
    return SUCCESS;
}

void do_inference(int spectrogram_index) {
    current_spectrogram_index = spectrogram_index;

    /* Learning pipeline: dispatch directly to learning handler with raw
     * float spectrogram access (no uint8 normalization needed here). */
    if (cur_kws_edge_state == STATE_LEARNING) {
        learning_on_spectrogram(spectrogram_index);
        return;
    }

    /* Inference pipeline: normalize spectrogram to uint8 and dispatch. */
    if (kws_edge_state[cur_kws_edge_state].on_mfcc_output) {
        __aligned(32) static uint8_t akida_input[SPECTROGRAM_COUNT][SPECTROGRAM_RES];
        for (int i = 0; i < SPECTROGRAM_COUNT; i++) {
            int idx = (i + spectrogram_index) % SPECTROGRAM_COUNT;
            for (int j = 0; j < SPECTROGRAM_RES; j++) {
                float normalized = ((spectrogram[idx][j] / mfcc_fs) + 1.0f) * 128.0f;
                if (normalized < 0.0f)
                    normalized = 0.0f;
                else if (normalized > 255.0f)
                    normalized = 255.0f;

                akida_input[i][j] = (uint8_t)normalized;
            }
        }

        kws_edge_state[cur_kws_edge_state].on_mfcc_output((uint8_t*)akida_input, (uint32_t*)dims);
    }
}

K_THREAD_STACK_DEFINE(capture_stack, CAPTURE_STACK_SIZE);
K_THREAD_STACK_DEFINE(process_stack, PROCESS_STACK_SIZE);

K_THREAD_STACK_DEFINE(cli_worker_stack, CONFIG_SHELL_STACK_SIZE);
K_THREAD_STACK_DEFINE(led_stack, LED_STACK_SIZE);

struct k_thread capture_thread;
struct k_thread process_thread;
struct k_thread cli_worker_thread;

#if IS_ENABLED(CONFIG_CAMERA_ENABLE_THREAD)
struct k_thread camera_thread;
k_tid_t camera_thread_id;
K_THREAD_STACK_DEFINE(camera_stack, CAMERA_STACK_SIZE);
#endif
struct k_thread led_thread;

k_tid_t capture_tid;
k_tid_t process_tid;
k_tid_t cli_worker_tid;
k_tid_t led_tid;
#define CLI_WORKER_PRIORITY 4

const struct device* uart;

void uart_init(void) {
    uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
    if (!device_is_ready(uart)) {
        LOG_ERR("UART not ready");
    } else {
        LOG_INF("UART is ready");
    }
}

static int start_dmic_audio_proc(void) {
    dmic_init();
    audio_processor_init(SAMPLING_RATE);

    int is_audio_started = audio_processor_start(false, (float*)spectrogram, spectrogram_dims,
                                                 MFCC_SAMPLE_COUNT, do_inference);
    if (is_audio_started == EFAILURE) {
        LOG_ERR("audio_processor not started");
        return EFAILURE;
    }

    capture_tid = k_thread_create(&capture_thread, capture_stack, CAPTURE_STACK_SIZE,
                                  dmic_capture_thread, NULL, NULL, NULL, CAPTURE_PRIORITY, K_USER,
                                  K_FOREVER  // START SUSPENDED
    );

    process_tid = k_thread_create(&process_thread, process_stack, PROCESS_STACK_SIZE,
                                  audio_process_thread, NULL, NULL, NULL, PROCESS_PRIORITY, K_USER,
                                  K_FOREVER  // START SUSPENDED
    );

    k_thread_start(capture_tid);
    k_thread_start(process_tid);

    return 0;
}

/* kws_app: runtime start/stop of the inference pipeline. The heavy init
 * (DMIC configure, MFCC init, thread create) runs once from
 * start_dmic_audio_proc() at boot; these just trigger/untrigger DMIC capture
 * and flip the state gate so BLE DEPLOY_START/STOP and UART "app start/stop"
 * are cheap and quick. */
extern "C" void reset_stale_inference_data(void);
static bool kws_app_running = false;

extern "C" int kws_app_start(void) {
    if (kws_app_running) {
        return 0;
    }
    int rc = dmic_start();
    if (rc < 0) {
        LOG_ERR("kws_app_start: dmic_start failed %d", rc);
        return rc;
    }
    reset_stale_inference_data();
    dmic_reset_dc_state();
    kws_set_edge_state(STATE_INFERENCE);
    kws_app_running = true;
    kws_config_notify_dmic_started();
    LOG_INF("kws_app: started");
    return 0;
}

extern "C" int kws_app_stop(void) {
    if (!kws_app_running) {
        return 0;
    }
    kws_config_notify_dmic_stopped();
    kws_set_edge_state(STATE_STOPPED);
    stop_dmic();
    kws_app_running = false;
    LOG_INF("kws_app: stopped");
    return 0;
}

extern "C" bool kws_app_is_running(void) {
    return kws_app_running;
}
#if IS_ENABLED(CONFIG_CAMERA_ENABLE_THREAD)
static int initialize_spi_camera_interface(void) {
    camera_thread_id =
        k_thread_create(&camera_thread, camera_stack, CAMERA_STACK_SIZE, camera_capture_thread,
                        NULL, NULL, NULL, CAMERA_PRIORITY, K_USER,
                        K_FOREVER  // START SUSPENDED
        );

    k_thread_start(camera_thread_id);

    return 0;
}
#endif
/**
 * @brief Initialize the learn weights memory
 *
 * This function initializes the saved_learn_weights structure pointer to valid
 * data and data pointers.
 * @param layer_mem_size size of the layer in terms of bytes
 */
static void init_learn_weights_mem(uint32_t layer_mem_size) {
    saved_learn_weights_ptr->total_saved_learn_weights_size = sizeof(saved_learn_weights);

    /* store the last layer size information */
    saved_learn_weights_ptr->learn_weights_data.learn_weights_size = layer_mem_size;

    /* initialize learn_weights_buff_ptr to the next location after the
     * structure saved_learn_weights size */
    learn_weights_buff_ptr = (uint8_t*)((uint8_t*)saved_learn_weights_ptr +
                                        saved_learn_weights_ptr->total_saved_learn_weights_size);

    saved_learn_weights_ptr->learn_weights_data.label_learnt_val = 0;
    for (int i = 0; i < saved_learn_weights_ptr->learn_weights_data.learn_weights_size; i++) {
        learn_weights_buff_ptr[i] = 0;
    }
    saved_learn_weights_ptr->total_saved_learn_weights_size +=
        saved_learn_weights_ptr->learn_weights_data.learn_weights_size;
}

static struct k_work_delayable switch_delayed_work;

/* Learning-specific async work items for interrupt-driven augmentation chaining
 */
static struct k_work akd_learn_fetch_work;
static void akd_learn_fetch_handler(struct k_work* work);

static void switch_learning_delayed(struct k_work* work) {
    ARG_UNUSED(work);

    if (cur_kws_edge_state == STATE_LEARN_SELECT) {
        kws_set_edge_state(STATE_LEARNING);
        /* Started ACK is sent only when BLE is connected and the KWS application is
         * deployed */
        if (is_ble_connected() && event_flag) {
            learning_started();
        }
        LOG_INF("learn_select -> learning");
        last_learn_ts = time_ms();

        /* Initialize structured learning state */
        memset(&learn_state, 0, sizeof(learn_state));
        learn_state.sub_state = LEARN_SUB_WAITING_FOR_SPEECH;
        learn_state.augmentations_per_utterance = 2 * g_num_neurons_per_class;
        learn_state.waiting_since_ts = time_ms();
        learn_rng_state = (uint32_t)k_uptime_get();
#ifdef CONFIG_AKIDATAG_BOARD
        led_set_state(LED_STATE_LEARN_SPEAK_NOW);
#endif

        /* Use shorter VAD timeout during learning for tighter capture */
        saved_speech_active_time_ms = speech_active_time_ms;
        speech_active_time_ms = LEARN_SPEECH_ACTIVE_TIME_MS;

        k_work_init_delayable(&learn_speech_end_work, learn_speech_end_handler);
        k_work_init(&learn_process_work, learn_process_handler);
        k_work_init(&akd_learn_fetch_work, akd_learn_fetch_handler);

        /* Start polling for silence timeout */
        k_work_reschedule(&learn_speech_end_work, K_MSEC(LEARN_SPEECH_END_GAP_MS));

        LOG_INF(
            "learn: structured learning for class %d (%d inputs/utterance, %d "
            "utterances)",
            cur_kws_edge_novel_class, learn_state.augmentations_per_utterance,
            LEARN_NUM_UTTERANCES);
        LOG_INF("learn: say keyword 1/%d", LEARN_NUM_UTTERANCES);
    }
    /* Removed: auto-transition back to learn_select after 5s timeout.
     * The structured learning flow manages its own timeouts via
     * learn_speech_end_work. */
}

static void switch_mode(int mode) {
    switch (cur_kws_edge_state) {
        case STATE_INFERENCE:
            if (STATE_LEARN_SELECT == mode) {
                akida_learn_mode(true);

                kws_set_edge_state(mode);
            }
            break;
        case STATE_LEARN_SELECT:
            if (STATE_INFERENCE == mode) {
                k_work_cancel_delayable(&switch_delayed_work);
                akida_learn_mode(false);

                kws_set_edge_state(mode);
            } else if (STATE_LEARNING == mode) {
                akida_learn_mode(true);
                k_work_reschedule(&switch_delayed_work, K_SECONDS(1));
            }
            break;
        case STATE_LEARNING:
            k_work_cancel_delayable(&switch_delayed_work);
            k_work_cancel_delayable(&learn_speech_end_work);
            k_work_cancel(&learn_process_work);
            /* Restore original VAD timeout */
            speech_active_time_ms = saved_speech_active_time_ms;
            akida_learn_mode(false);
            /* kws_set_edge_state() hands the session's wake reference back on the
             * transition itself, which is why both akida_learn_mode() writes sit above
             * it: they still need the chip running. This is one route out of a learning
             * session but not the only one, so the release deliberately lives in the
             * setter rather than here. */
            if (STATE_LEARN_SELECT == mode) {
                kws_set_edge_state(mode);
            } else if (STATE_INFERENCE == mode) {
                akida_learn_mode(false);
                kws_set_edge_state(mode);
            }
            break;
        default:
            break;
    }
}

static void reset_saved_weights() {
    init_learn_weights_mem(mesh_learn_weights_size);

    akida_learn_mode(true);
    /* save the base weights into base_labels_wts_ptr location  */
    save_weights_from_mesh(base_labels_wts_ptr, mesh_learn_weights_size);

    akida_learn_mode(false);
}

/**
 * @brief Function to update learned weights to mesh.
 *
 */
static void update_weights_to_mesh() {
    /* update the learned weights into the last layer before the inference */
    saved_learn_weights_ptr->learn_weights_data.learn_weights_size =
        akida_update_learn_weights((uint32_t*)learn_weights_buff_ptr,
                                   saved_learn_weights_ptr->learn_weights_data.learn_weights_size);
    if (saved_learn_weights_ptr->learn_weights_data.learn_weights_size !=
        (int32_t)akida_learn_mem_size()) {
        LOG_ERR(
            " there is an issue for the learned class, as weights are not "
            "stored properly in Akida Neuron Fabric");
        /* as there is an error, initialize the memory again */
        init_learn_weights_mem(mesh_learn_weights_size);
    }
}

static void read_learn_weights_from_flash(void) {
    struct fs_file_t file;
    fs_file_t_init(&file);

    int ret = fs_open(&file, LEARN_WEIGHTS_FILE_NAME, FS_O_READ);

    if (ret == 0) {
        ret = fs_read(&file, (uint8_t*)saved_learn_weights_ptr,
                      saved_learn_weights_ptr->total_saved_learn_weights_size);

        fs_close(&file);

        /* if same number of bytes are read from flash, then check CRC else user to
         * do re-learning*/
        if (ret == (int)saved_learn_weights_ptr->total_saved_learn_weights_size) {
            uint32_t crc32 =
                crc32_ieee((uint8_t*)saved_learn_weights_ptr + 4,
                           (saved_learn_weights_ptr->total_saved_learn_weights_size - 4));
            /* if CRC is failed then user to do re-learning*/
            if (crc32 != saved_learn_weights_ptr->crc) {
                LOG_ERR(
                    "learn weights CRC check failed, %d bytes read from flash and "
                    "there is an error in reading learning data, user need to "
                    "perform learning again ",
                    ret);
                reset_saved_weights();
            } else {
                LOG_INF(
                    "%d bytes are read from flash (learn weights) to "
                    "saved_learn_weights_ptr location ",
                    ret);
                if (saved_learn_weights_ptr->learn_weights_data.label_learnt_val) {
                    update_weights_to_mesh();
                }
            }
        } else {
            LOG_WRN("incorrect number of bytes read from flash, user need to re-learn ");
            reset_saved_weights();
        }
    } else {
        LOG_ERR("read_learn_weights_from_flash: saved learn weights file open failed ");
    }
}

static int initiate_kws_inference(uint8_t is_el_model_l) {
    if (is_el_model_l) {
        k_work_init_delayable(&switch_delayed_work, switch_learning_delayed);
        mesh_learn_weights_size = akida_learn_mem_size();

        LOG_INF("mesh_learn_weights_size = %" PRIu32, mesh_learn_weights_size);

        // Free any prior allocation to avoid memory leak on re-init
        if (saved_learn_weights_ptr) {
            delete[] reinterpret_cast<uint8_t*>(saved_learn_weights_ptr);
            saved_learn_weights_ptr = NULL;
            learn_weights_buff_ptr = NULL;
        }
        if (base_labels_wts_ptr) {
            delete[] base_labels_wts_ptr;
            base_labels_wts_ptr = NULL;
        }

        // allocating memory for structure (this will hold crc, size etc ) + learn
        // weights data together to place them in contiguous locations
        saved_learn_weights_ptr = reinterpret_cast<saved_learn_weights*>(
            new uint8_t[sizeof(saved_learn_weights) + mesh_learn_weights_size]);

        base_labels_wts_ptr = new uint8_t[mesh_learn_weights_size];

        if ((saved_learn_weights_ptr == NULL) || (base_labels_wts_ptr == NULL)) {
            LOG_ERR(
                "dynamic memory allocation failed for weights data and hence "
                "application is not running ");
            return -EFAILURE;
        }
        // initialize the learn_weights_mem structure
        reset_saved_weights();

        read_learn_weights_from_flash();
    }

    kws_set_edge_state(STATE_INFERENCE);
    /* adding additional 1200ms to last_trigger_time_ms to increase the debouce
     * time at during the initialization to suppress any noise from dmic */
    last_trigger_time_ms = time_ms() + 1200ULL;
    start_dmic_audio_proc();
    kws_config_notify_dmic_started();
    kws_app_running = true;
    return SUCCESS;
}
#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
static int start_imu_proc(void) {
    imu_tid = k_thread_create(&imu_thread, imu_stack, IMU_STACK_SIZE, imu_data_thread, NULL, NULL,
                              NULL, IMU_PRIORITY, K_USER,
                              K_FOREVER  // START SUSPENDED
    );

    k_thread_start(imu_tid);
    return 0;
}
#endif
void check_reset_reason(void) {
    uint32_t cause = 0;

    if (hwinfo_get_reset_cause(&cause) != 0) {
        LOG_ERR("Failed to read reset cause");
        return;
    }

    LOG_INF("Reset cause: 0x%08x", cause);

    if (cause & RESET_LOW_POWER_WAKE) {
        LOG_INF("Wakeup from System OFF");
    }
    if (cause & RESET_PIN) {
        LOG_INF("Reset from RESET pin");
    }
    if (cause & RESET_WATCHDOG) {
        LOG_INF("Reset from Watchdog");
    }
    if (cause & RESET_SOFTWARE) {
        LOG_INF("Reset from software reset");
    }

    /* Do not clear here — init_boot_count() reads and clears later */
}
/**
 * @brief Create and start the LED indication thread.
 *
 * This function creates the LED indication thread with the configured
 * stack size and priority.
 *
 * @return 0 on successful thread creation and start.
 */
static int start_led_ind(void) {
    led_tid = k_thread_create(&led_thread, led_stack, LED_STACK_SIZE, led_ind_thread, NULL, NULL,
                              NULL, LED_PRIORITY, K_USER,
                              K_FOREVER  // START SUSPENDED
    );

    k_thread_start(led_tid);
    return 0;
}
#ifdef CONFIG_AKIDATAG_BOARD
static int start_current_proc(void) {
    current_tid =
        k_thread_create(&current_thread, current_stack, CURRENT_STACK_SIZE, current_data_thread,
                        NULL, NULL, NULL, CURRENT_PRIORITY, K_USER, K_FOREVER);

    k_thread_start(current_tid);
    return 0;
}
#endif
/* Defined later in this file; forward-declared so update_model_params() can
 * apply the inference mode carried in the model metadata. */
void akida_init(int mode);

static int update_model_params(model_meta_t kws_meta) {
    g_input_size = kws_meta.input_shape[0] * kws_meta.input_shape[1] * kws_meta.input_shape[2];
    g_num_classes = kws_meta.output_shape[0] * kws_meta.output_shape[1] * kws_meta.output_shape[2];

    LOG_INF("kws_meta.num_edge_classes %x ", kws_meta.num_edge_classes);
    g_num_neurons_per_class = (kws_meta.num_edge_classes & 0xFFFF0000) >> 16;
    if (g_num_neurons_per_class == 0) {
        g_num_neurons_per_class = 1;
    }

    g_num_classes = g_num_classes / g_num_neurons_per_class;
    g_num_edge_learn_classes = (kws_meta.num_edge_classes & 0xFFFF);

    LOG_INF(
        "kws_meta.input_shape[0] %d, kws_meta.input_shape[1] %d, "
        "kws_meta.input_shape[2] %d",
        kws_meta.input_shape[0], kws_meta.input_shape[1], kws_meta.input_shape[2]);

    LOG_INF(
        "kws_meta.output_shape[0] %d, kws_meta.output_shape[1] %d, "
        "kws_meta.output_shape[2] %d",
        kws_meta.output_shape[0], kws_meta.output_shape[1], kws_meta.output_shape[2]);

    LOG_INF(
        "g_input_size %d, g_num_classes %d, g_num_neurons_per_class %d, "
        "g_num_edge_learn_classes %d ",
        g_input_size, g_num_classes, g_num_neurons_per_class, g_num_edge_learn_classes);

    delete[] akida_output;
    akida_output = new (std::nothrow) int32_t[g_num_classes * g_num_neurons_per_class];
    delete[] akida_output_dq;
    akida_output_dq = new (std::nothrow) float[g_num_classes * g_num_neurons_per_class];
    akd_op_size = sizeof(int32_t) * g_num_classes * g_num_neurons_per_class;

    delete[] smoothed_scores;
    smoothed_scores = new (std::nothrow) float[g_num_classes]();
    delete[] chiming_counters;
    chiming_counters = new (std::nothrow) int[g_num_classes]();
    delete[] softmax_scores;
    softmax_scores = new (std::nothrow) float[g_num_classes]();

    if (akida_output == nullptr || akida_output_dq == nullptr || smoothed_scores == nullptr ||
        chiming_counters == nullptr || softmax_scores == nullptr) {
        LOG_ERR("update_model_params: buffer allocation failed for %u classes", g_num_classes);
        return -EFAILURE;
    }

    /* mfcc_fs is mandatory: it divides every input feature, so a missing/zero
     * value (model uploaded without mfcc_fs in info.yaml) would produce garbage
     * inference. Reject such a model instead of guessing a default. */
    if (kws_meta.mfcc_fs_bits == 0) {
        LOG_ERR(
            "KWS model metadata missing mfcc_fs — refusing to run. "
            "Regenerate the model with mfcc_fs in info.yaml.");
        return -EFAILURE;
    }
    memcpy(&mfcc_fs, &kws_meta.mfcc_fs_bits, sizeof(float));

    g_silence_class = (int)kws_meta.silence_class;
    g_unknown_class = (int)kws_meta.unknown_class;

    /* Apply the inference mode carried in the model metadata (info.yaml).
     * akida_init() sets kws_api_selection and manages the async thread/IRQ, so
     * the inference path and `kws_mode_get` stay consistent. A manual
     * `kws_mode sync|async` still overrides this at runtime. */
    uint32_t requested_mode = kws_meta.inference_mode; /* 0=sync, 1=async */
#ifndef CONFIG_AKIDATAG_BOARD
    if (requested_mode == DEFAULT_API_SELECTION_ASYNC) {
        LOG_WRN(
            "info.yaml requests ASYNC but this board only supports SYNC; "
            "falling back to SYNC");
        requested_mode = DEFAULT_API_SELECTION_SYNC;
    }
#endif
    akida_init((int)requested_mode);
    LOG_INF("Inference mode from metadata: %s",
            requested_mode == DEFAULT_API_SELECTION_ASYNC ? "ASYNC" : "SYNC");

    return SUCCESS;
}

static void kws_post_processing(uint32_t dma_time, uint32_t inf_time);

volatile uint32_t inference_start_dma_ts = 0;
volatile uint64_t inference_start_ts = 0;
#ifdef CONFIG_AKIDATAG_BOARD
/* Returns true when the system is actively learning (called from ISR context)
 */

bool akd_in_learning(void) {
    return cur_kws_edge_state == STATE_LEARNING;
}

/* Submits the learning fetch work item (called from ISR via gpio.c) */

void schedule_akd_learning_wq(void) {
    k_work_submit(&akd_learn_fetch_work);
}

/* Set by akida_init() to ask akd_async_thread to leave its loop. See the
 * teardown in akida_init() for why it is asked rather than aborted. */
static volatile bool akd_async_stop_requested;

static void akd_async_thread(void* a, void* b, void* c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (!akd_async_stop_requested) {
        int ret = akd_async_sem_take(K_SECONDS(5));

        if (akd_async_stop_requested) {
            break;
        }

        if (ret == -EAGAIN) {
            akd_async_wake_reap_stranded();
            continue;
        }

        uint64_t fetch_start_ts = time_ms();
        if (-EFAILURE != akida_fetch((uint8_t*)akida_output_dq, akd_op_size, true)) {
            uint64_t fetch_end_ts = time_ms();
            uint32_t fetch_time = (uint32_t)(fetch_end_ts - fetch_start_ts);
            if (verbose_on) {
                LOG_INF("fetch: done (cpu=%ums)", fetch_time);
            }
            /* Clock counter is in Akida cycles; convert to us like the sync path. */
            uint32_t inference_dma_ts =
                (akida_get_clock_counter() - inference_start_dma_ts) / AKIDA_FREQUENCY_MHZ;
            uint32_t inference_time = fetch_end_ts - inference_start_ts;
            kws_post_processing(inference_dma_ts, inference_time);
        } else {
            LOG_ERR("Fetch returned EFAILURE or Error");
        }
        akd_async_wake_give(); /* the inference is done with the chip */
    }
}
#endif

/**
 * @brief Initialize Akida API mode (Sync / Async)
 *
 * This function configures the Akida execution mode based on the selected mode
 * and board configuration.
 *
 * Behavior:
 * - On CONFIG_AKIDATAG_BOARD:
 *   - Supports both Sync and Async modes.
 *   - If Async mode is selected:
 *       - Enables GPIO interrupt (used for async triggering).
 *       - Creates a dedicated thread for async result processing.
 *   - If Sync mode is selected:
 *       - Disables GPIO interrupt.
 *       - Runs in blocking/synchronous mode.
 *   - If an async thread is already running, it is safely stopped before
 * switching modes.
 *
 * - On non-AkidaTag boards:
 *   - Only Sync mode is supported.
 *   - Async mode is not allowed and is ignored.
 *
 * @param mode
 *   - DEFAULT_API_SELECTION_ASYNC : Enables async mode (interrupt +
 * thread-based processing)
 *   - DEFAULT_API_SELECTION_SYNC  : Enables sync mode (blocking execution)
 */
void akida_init(int mode) {
#ifdef CONFIG_AKIDATAG_BOARD

    /* Stop existing async thread if running.
     *
     * Asked to leave its loop rather than aborted where it stands: it holds
     * akd_wake_owner_lock across every akd_wake_put(), and Zephyr does not
     * release a mutex owned by an aborted thread, so an abort landing inside one
     * of those critical sections would wedge the lock permanently and every later
     * take - audio, learning, shell - would block on it forever. Letting it reach
     * the top of its loop guarantees it owns nothing when it exits. The
     * semaphore give is what breaks it out of its 5 s wait immediately, so this
     * costs no extra latency on the model-update path that calls akida_init().
     */
    if (akd_async_tid != NULL) {
        /* Silence the completion interrupt first: it is the other producer of
         * akd_async_sem, and a token it posts after the reset below would be
         * inherited by the replacement thread just the same. Re-enabled below for
         * async mode; akd_irq_enable/disable are idempotent. */
        akd_irq_disable();
        akd_async_stop_requested = true;
        akd_async_sem_give();
        if (k_thread_join(akd_async_tid, K_SECONDS(2)) != 0) {
            LOG_ERR(
                "async thread did not exit; aborting it (akd_wake_owner_lock may "
                "be left held)");
            k_thread_abort(akd_async_tid);
        }
        akd_async_tid = NULL;
        akd_async_stop_requested = false;
        /* Only now is the thread definitively gone, so nothing can retire what it
         * still owned. Reclaiming before the join would race its own
         * akd_async_wake_give(). */
        akd_async_wake_reclaim_all();
        /* The give above is only consumed if the thread was actually waiting on it,
         * and the semaphore's count outlives the thread. Reset so a newly created
         * async thread starts with no inherited token: otherwise its first take
         * returns immediately and it fetches against an empty queue, contending for
         * the AKD1500 SPI bus with whatever the caller is about to program. */
        akd_async_sem_reset();
        LOG_INF("Stopped existing async thread");
    }

    if (mode == DEFAULT_API_SELECTION_ASYNC) {
        akd_irq_enable();
        kws_api_selection = DEFAULT_API_SELECTION_ASYNC;

        akd_async_tid =
            k_thread_create(&akd_async_thread_data, akd_async_stack, AKD_ASYNC_STACK_SIZE,
                            akd_async_thread, NULL, NULL, NULL, AKD_ASYNC_PRIORITY, 0, K_NO_WAIT);

        k_thread_name_set(akd_async_tid, "akd_async");

        LOG_INF("Akida Async is initialized");
    } else {
        akd_irq_disable();
        kws_api_selection = DEFAULT_API_SELECTION_SYNC;
        LOG_INF("Akida Sync is initialized");
    }

#else
    /* Non-AkidaTag boards: only sync supported */
    kws_api_selection = DEFAULT_API_SELECTION_SYNC;

    LOG_INF("Akida Sync is initialized");
    LOG_INF("Note: Async mode is not supported on this board configuration");

#endif
}

int main(void) {
    check_reset_reason();
    /* LOG_INF("App Core Version: %s", CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION); */
    /* Image IDs defined by MCUboot */
    print_image_version(FLASH_AREA_ID(image_0), "App Core");

    /*print_image_version(FLASH_AREA_ID(image_1), "Net Core");*/
#if IS_ENABLED(CONFIG_WDT_ENABLE)
    watchdog_init(&wdt, &wdt_channel_id);
#endif

#ifdef CONFIG_AKIDATAG_BOARD
    int err_gpio = gpio_init();
    if (err_gpio) {
        printf("GPIO init failed (err %d)\n", err_gpio);
        return -1;
    }
    int err_button = user_button_init();
    if (err_button) {
        LOG_ERR("User button init failed");
    }
    akidatag_peripherals_power_enable();
    int ret = battery_init();
    if (ret) {
        printf("Battery init failed (err %d)\n", ret);
    }
#endif
    /* Declared after gpio_init() so a failure there, which leaves no reference
     * held, is not followed by a release. Declared outside the board guard
     * because on the DK the whole wake API is a no-op stub. */
    AkdBootWakeScope boot_wake;

    uart_init();
    start_led_ind();
    led_set_state(LED_STATE_NORMAL_APP);
    LOG_INF("Akida TAG Application");
    confirm_image_if_needed();
    init_setting_sub_system();
    kws_config_init();
    shared_buf_init();
    file_transfer_init();
    ble_init();

    init_akd_object();
    akida_spiflash_init();

    /* Get the SPI NOR flash device defined in the device tree (node label:
     * ext_flash) and verify that the driver has initialized successfully before
     * using it.
     */
    const struct device* spi_flash = DEVICE_DT_GET(DT_NODELABEL(ext_flash));

    if (!device_is_ready(spi_flash)) {
        LOG_ERR("SPI flash not ready");
    } else {
        LOG_INF("SPI flash device ready: %s", spi_flash->name);
    }
    int err = storage_init();
    if (err != 0) {
        LOG_ERR("LittleFS mount failed %d", err);
    } else {
        LOG_INF("LittleFS mount succeeded %d", err);
    }

    init_boot_count();
#ifdef CONFIG_AKIDATAG_BOARD
    /* Battery thread runs fuel_gauge_init in the background — boot continues. */
    battery_start();
    start_current_proc();
#endif

    cli_worker_tid =
        k_thread_create(&cli_worker_thread, cli_worker_stack, CONFIG_SHELL_STACK_SIZE,
                        cli_worker_proc_thread, NULL, NULL, NULL, CLI_WORKER_PRIORITY, K_USER,
                        K_FOREVER  // START SUSPENDED
        );
    k_thread_start(cli_worker_tid);
#ifdef CONFIG_AKIDATAG_BOARD
    akida_init(DEFAULT_API_SELECTION_ASYNC);
#else
    akida_init(DEFAULT_API_SELECTION_SYNC);
#endif
    /* Load model metadata from LittleFS (written there by a previous BLE upload).
     * The metadata contains the flash address and program_info binary so we do
     * not need to rely on compile-time flash_offsets[] or hardcoded program_info
     * arrays.
     *
     * Boot validation sequence:
     *   1. Read header only (no sram_upload_buffer usage) to get flash_address.
     *   2. Load model_data meta (3rd file): CRC, first 4 bytes, length, name.
     *   3. Validate model_name against expected slot (whitelist check).
     *   4. Full SPI flash CRC validation (overwrites sram_upload_buffer).
     *   5. Reload full meta + program_info into sram_upload_buffer.
     *   6. Program Akida.
     */

    /* Step 1: read header struct only to get flash_address */
    int hdr_ret = file_transfer_read_meta_hdr_only(0, &kws_meta);
    if (hdr_ret != 0) {
        LOG_ERR("Metadata header unavailable (err %d)", hdr_ret);
        return -1;
    }
    uint32_t kws_flash_addr = kws_meta.flash_address;

    /* Step 3: validate model name from the header (model_meta_t.model_name) */
    if (file_transfer_check_model_name(0, kws_meta.model_name) != 0) {
        LOG_ERR("Model name mismatch: stored='%s', expected for slot 1='kws'", kws_meta.model_name);
        kws_model_present = false;
        return -1;
    }
    LOG_INF("Model name: stored='%s', ", kws_meta.model_name);
    /* Step 2&4: load data meta and validate flash contents */
    int dm_ret = file_transfer_load_data_meta(0, &kws_data_meta);
    if (dm_ret == 0) {
        /* Step 4: full SPI flash CRC validation. Routing is not set here: the
         * validation reads through spi_flash_read_helper_func(), whose FlashClaim
         * routes the S2M feedthrough and holds a wake reference per chunk. */
        int val_ret = file_transfer_validate_flash_data(kws_flash_addr, &kws_data_meta);
        if (val_ret != 0) {
            LOG_ERR("Model data validation FAILED will not program Akida");
            kws_model_present = false;
            return -1;
        }
    } else {
        LOG_ERR("model_data file is not present and returning");
        return -1;
    }

    /* Step 5: reload full meta + program_info into sram_upload_buffer.
     * This is necessary because file_transfer_validate_flash_data() may have
     * overwritten sram_upload_buffer during the CRC read loop. */
    int meta_ret = file_transfer_load_meta(0, &kws_meta);
    if (meta_ret != 0) {
        LOG_ERR("Metadata reload failed (err %d)", meta_ret);
        return -1;
    }

    /* Step 6: program Akida */
    LOG_INF("Model data found at 0x%08X", kws_flash_addr);
    akida_toggle_clock_counter(true);
    LOG_INF("Programming model info into AKD1500");

    /* Use the CONFIG-DMA counter for programming (the HRC/event counter read by
     * akida_get_clock_counter() is idle during programming -> would show 0).
     * Read the absolute value after programming: it latches this model's
     * config-DMA cycle count (~2.1M), Akida-internal / host-clock-independent. */
    uint64_t start_time = time_ms();

    akida_program_flash(sram_upload_buffer, (int)kws_meta.info_data_len, kws_meta.flash_address,
                        &is_el_model);

    uint32_t prog_time = (uint32_t)(time_ms() - start_time);
    uint32_t delta_cycle = akida_get_config_clock_counter();
    uint32_t dma_time = delta_cycle / AKIDA_FREQUENCY_MHZ;
    LOG_INF("Model program: %u config-dma cycles, %u us dma, %u ms cpu (SPI %u Hz)", delta_cycle,
            dma_time, prog_time, akd_spi_get_frequency());

    akida_batch_size(1, true);
    kws_model_present = true;
    if (update_model_params(kws_meta) != SUCCESS) {
        kws_model_present = false;
        return -1;
    }

    if (check_model_compatibility(is_el_model, kws_meta) != SUCCESS) {
        return -1;
    }

    initiate_kws_inference(is_el_model);
    is_kws_inference_started = true;
    /* Hand back the boot wake reference gpio_init() left held: the model is
     * programmed and the app is now idle until the first utterance. Released here
     * rather than left to the destructor so the moment is unchanged - main()
     * returns much later, and the chip must be allowed to sleep as soon as boot
     * stops needing it. */
    boot_wake.release();

    LOG_INF("data to check : model_size %d, class %d ",
            kws_meta.info_data_len + kws_data_meta.data_length, g_num_classes);
#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
    start_imu_proc();
#endif

#if IS_ENABLED(CONFIG_CAMERA_ENABLE_THREAD)
    initialize_spi_camera_interface();
#endif
    // ... inside a function like main() or a separate initialization function
    LOG_INF("Current CPU frequency: %u MHz", SystemCoreClock / 1000000);
    // You can also inspect the NRF_CLOCK_S->HFCLKCTRL register value
    LOG_INF("NRF_CLOCK_S->HFCLKCTRL: %d", NRF_CLOCK_S->HFCLKCTRL);

    return 0;
}

void cli_worker_proc_thread(void* a, void* b, void* c) {
    LOG_INF("CLI Worker: ");

    while (1) {
#if IS_ENABLED(CONFIG_WDT_ENABLE)
        if (all_threads_healthy()) {
            wdt_feed(wdt, wdt_channel_id);
        }
#endif
#ifdef CONFIG_AKIDATAG_BOARD
        if (is_ble_connected() && app_start_flag) {
            battery_service_send(CMD_STREAM_STS);
        }
#endif
        process_led();
        k_msleep(1000);
    }
}

#define DEBOUNCE_COOLDOWN_MS 300  // Cooldown period after a trigger (changed from 1000ms)

uint32_t kws_debounce_time = DEBOUNCE_COOLDOWN_MS;
bool feature_buff_full = false;

extern "C" void reset_stale_inference_data(void) {
    /* During learning, skip the spectrogram reset — learning has its own
     * capture buffer and the spectrogram is still needed for ongoing capture. */
    if (cur_kws_edge_state != STATE_LEARNING) {
        reset_kws_spectrogram();
    }
    memset(smoothed_scores, 0, g_num_classes * sizeof(float));
    memset(chiming_counters, 0, g_num_classes * sizeof(int));
    if (verbose_on) {
        LOG_INF("reset: clearing stale inference data");
    }
    return;
}

extern "C" uint8_t is_kws_debounce_complete(void) {
    uint8_t is_debounce = 0;
    /* DEBOUNCING (Preventing multiple rapid triggers)*/
    if ((uint64_t)(time_ms()) > last_trigger_time_ms + kws_debounce_time)
        is_debounce = 1;

    return is_debounce;
}

extern "C" void set_feature_buff_full(void) {
    feature_buff_full = 1;
}

extern "C" uint8_t is_feature_buff_full(void) {
    return feature_buff_full;
}

static void reset_kws_spectrogram(void) {
    memset(spectrogram, 0, sizeof(spectrogram));
    // feature_buff_full = false;
    reset_spectrogram_index();
}

static void kws_post_processing(uint32_t dma_time, uint32_t inf_time) {
    // Step 1: Per-class max pooling from dequantized output
    int num_cls_capped = (int)g_num_classes;
    compute_per_class_max(akida_output_dq, num_cls_capped, (int)g_num_neurons_per_class,
                          softmax_scores);

    // Find argmax before softmax (monotonic — result is the same after)
    int found = 0;
    for (int c = 1; c < num_cls_capped; c++) {
        if (softmax_scores[c] > softmax_scores[found]) {
            found = c;
        }
    }

    // Step 2: Softmax over per-class max values
    softmax(softmax_scores, (uint32_t)num_cls_capped);

    // Step 3: EMA smoothing of softmax scores
    for (int c = 0; c < num_cls_capped; c++) {
        smoothed_scores[c] =
            smoothing_alpha * softmax_scores[c] + (1.0f - smoothing_alpha) * smoothed_scores[c];
    }

    // Step 4: Update chiming counters for keyword classes
    // (skip silence and unknown classes)
    int triggered_class = -1;
    float triggered_score = 0.0f;
    for (int c = 0; c < num_cls_capped; c++) {
        if (c == g_silence_class || c == g_unknown_class) {
            continue;
        }
        /* Uniform detection threshold for all classes (base and edge-learned),
         * runtime-tunable via `app score`. */
        if (smoothed_scores[c] >= score_threshold) {
            chiming_counters[c]++;
        } else {
            chiming_counters[c] = 0;
        }
        // Check if this class has reached the chiming threshold
        if (chiming_counters[c] >= chiming_threshold) {
            if (triggered_class == -1 || smoothed_scores[c] > triggered_score) {
                triggered_class = c;
                triggered_score = smoothed_scores[c];
            }
        }
    }

    if (verbose_on) {
        LOG_INF(
            "scores: argmax=%d (%s) softmax=%.2f smoothed=%.2f "
            "chiming=%d/%d",
            found, (found < kws_new_tags_count) ? kws_new_tags[found] : "?", softmax_scores[found],
            smoothed_scores[found], (found < num_cls_capped) ? chiming_counters[found] : 0,
            chiming_threshold);
    }

    // Step 5: Trigger if chiming threshold reached
    if (triggered_class >= 0) {
        if (verbose_on) {
            LOG_INF("trigger: keyword=%s chiming=%d/%d",
                    (triggered_class < kws_new_tags_count) ? kws_new_tags[triggered_class] : "?",
                    chiming_counters[triggered_class], chiming_threshold);
        }
        current_class = triggered_class;
        float confidence = smoothed_scores[triggered_class];

        LOG_INF("Keyword Detected: %s",
                (triggered_class < kws_new_tags_count) ? kws_new_tags[triggered_class] : "?");
        if (metrics_on) {
            LOG_INF(
                "  confidence=%.1f%% smoothed=%.1f%% chiming=%d cpu=%ums "
                "dma=%uus",
                confidence * 100.0f, triggered_score * 100.0f, chiming_counters[triggered_class],
                inf_time, dma_time);
#ifdef CONFIG_AKIDATAG_BOARD
            current_sense_reading_t pwr;
            current_sense_get_latest(&pwr);
            LOG_INF("  power: 1V8=%.1fmW 0V8=%.1fmW", (double)pwr.power_mw[CURRENT_RAIL_1V8],
                    (double)pwr.power_mw[CURRENT_RAIL_0V8]);
#endif
        }
        /* KWS data is sent only when BLE is connected and the KWS application
         * is deployed */
        if (is_ble_connected() && event_flag) {
            send_event(CMD_DEPLOY_START, kws_new_tags[triggered_class], confidence * 100.0f);
        }
        last_trigger_time_ms = time_ms();
#ifdef CONFIG_AKIDATAG_BOARD
        led_set_state(LED_STATE_KEYWORD_TRIGGERED);
        k_sem_give(&led_sem);
#endif
        reset_stale_inference_data();
    }
    return;
}

static int32_t inference_on_mfcc_output(uint8_t* input, uint32_t* input_shape) {
    int ret = 0;

    if (kws_api_selection == DEFAULT_API_SELECTION_SYNC) {
        akd_wake_get(); /* wake for inference */
        inference_start_ts = time_ms();
        uint32_t s_dma_cycls = akida_get_clock_counter();

        int num_outputs = g_num_classes * g_num_neurons_per_class;
        int pred_ret =
            akida_predict(input, input_shape, akida_output_dq, num_outputs * (int)sizeof(float));
        if (pred_ret == SUCCESS) {
            uint32_t inf_time = time_ms() - inference_start_ts;
            uint32_t delta_cycle = akida_get_clock_counter() - s_dma_cycls;
            uint32_t dma_time = delta_cycle / AKIDA_FREQUENCY_MHZ;
            if (verbose_on) {
                LOG_INF("inference: done (cpu=%ums dma=%uus)", inf_time, dma_time);
            }
            kws_post_processing(dma_time, inf_time);
        } else {
            LOG_ERR("akida_predict failed");
            reset_stale_inference_data();
        }
        akd_wake_put(); /* done with the chip */
    } else {
        /* Taken once, outside the retry loop, and handed back by akd_async_thread
         * after the matching fetch. A retried enqueue must not take a second. */
        akd_async_wake_take();
        uint64_t start_time = time_ms();
        do {
            inference_start_ts = time_ms();
            inference_start_dma_ts = akida_get_clock_counter();
            ret = akida_enqueue(input, input_shape, NULL);
            uint32_t enq_time = (uint32_t)(time_ms() - inference_start_ts);
            if (verbose_on) {
                LOG_INF("enqueue: done (cpu=%ums)", enq_time);
            }
            {
                // uint32_t power_tmp;
                /* clear , accumulated power before enqueue */
                // capture_power(true, &power_tmp, &power_tmp);
            }
            // Check timeout
            if ((time_ms() - start_time) > ENQUEUE_TIMEOUT_MS) {
                LOG_ERR("akida_enqueue timeout after %d ms", ENQUEUE_TIMEOUT_MS);
                if (ret) {
                    /* Nothing was accepted, so no completion interrupt is coming for it.
                     * Hand a reference back now rather than leaving it for the timeout
                     * path, which would make the chip wait out a full deadline first. */
                    akd_async_wake_give();
                }
                ret = EFAILURE;
                learn_utterance_complete();  // graceful abort
                break;
            }
        } while (ret);
    }
    return ret;
}

static void inference_on_user_input(int input_type) {
    switch (input_type) {
        case USER_INPUT_LP(0):
            switch_mode(STATE_LEARN_SELECT);
            LOG_INF("inference -> learn_select");
            break;
        default:
            LOG_INF(" wrong input");
            break;
    }
}

/**
 * @brief Function to save learned weights to flash.
 *
 */
static void save_weights_to_flash() {
    /* compute the CRC before storing into flash */
    uint32_t cur_ts = k_cycle_get_32();

    saved_learn_weights_ptr->crc =
        crc32_ieee((uint8_t*)saved_learn_weights_ptr + 4,
                   (saved_learn_weights_ptr->total_saved_learn_weights_size - 4));
    if (verbose_on) {
        LOG_INF(" the computed CRC = %x", saved_learn_weights_ptr->crc);
    }
    /* save the learned weights into flash */

    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, LEARN_WEIGHTS_FILE_NAME, FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        fs_write(&file, (uint8_t*)saved_learn_weights_ptr,
                 saved_learn_weights_ptr->total_saved_learn_weights_size);
        fs_close(&file);

        LOG_INF(
            "%d learned weight bytes are programmed to flash at "
            "LEARN_WEIGHTS_FILE_NAME",
            saved_learn_weights_ptr->total_saved_learn_weights_size);
    } else {
        LOG_ERR("save_weights_to_flash: fail open failed ");
    }
    uint32_t flash_ts = (uint32_t)(k_cycle_get_32() - cur_ts);
    uint64_t duration_us = k_cyc_to_us_floor64(flash_ts);
    if (verbose_on) {
        LOG_INF("Duration = %" PRIu64 " us", duration_us);
    }
}

/**
 * @brief Resets the learn weights memory
 *
 * This function update the saved_learn_weights with base_labels_wts_ptr and
 * corrupts the learn weights file in flash
 * @param lbl_wts - holds the base class weights
 */

static void reset_learned_weights(uint8_t* lbl_wts) {
    /* reset learn_weights_size and label_learnt_val and CRC to 0 */
    /* once the contents are reset, then stay in the same state, so that user can
     * do the learning */
    saved_learn_weights_ptr->learn_weights_data.label_learnt_val = 0;
    for (int i = 0; i < saved_learn_weights_ptr->learn_weights_data.learn_weights_size; i++) {
        learn_weights_buff_ptr[i] = lbl_wts[i];
    }
    saved_learn_weights_ptr->crc = 0;

    /* storing only 4 bytes without computing CRC, upon next reboot, the CRC check
     * will
     * fail and message will be displayed for the user to relearn the classes */
    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, LEARN_WEIGHTS_FILE_NAME, FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        fs_write(&file, (uint8_t*)saved_learn_weights_ptr, 4);
        fs_close(&file);
    }
    /* reset weights in model also */
    update_weights_to_mesh();
    LOG_INF("learn weights content in Flash have been reset");
}

static void learn_select_on_user_input(int input_type) {
    LOG_INF("learn_select_on_user_input");
    switch (input_type) {
        case USER_INPUT_LP(0):
            /*  change the state to inference */
            switch_mode(STATE_INFERENCE);
            LOG_INF("learn_select -> inference");
            /* save the weights to flash only when labels are learnt and state chages to
             * STATE_INFERENCE */
            if (saved_learn_weights_ptr->learn_weights_data.label_learnt_val) {
                LOG_INF("Weights saved from MEM->FLASH");
                save_weights_to_flash();
            }
            break;
        case USER_INPUT_SP(0):
            if (cur_kws_edge_novel_class <= KWS_EDGE_MAX_NOVEL_CLASS_ID) {
                switch_mode(STATE_LEARNING);
            }
            break;
        case USER_INPUT_SP(1):
            cur_kws_edge_novel_class++;
            if (cur_kws_edge_novel_class > KWS_EDGE_MAX_NOVEL_CLASS_ID) {
                cur_kws_edge_novel_class = KWS_EDGE_NOVEL_CLASS_BASE_ID;
            }
            LOG_INF("class ID selected is %d", cur_kws_edge_novel_class);
            break;
        case USER_INPUT_LP(1):
            reset_learned_weights(base_labels_wts_ptr);
            break;
        default:
            LOG_INF("input_type default");
            break;
    }
}

/*---------------------------------------------------------------------------
 * Structured Edge Learning - Augmented Input Generation
 *---------------------------------------------------------------------------*/

/**
 * @brief Generate a single augmented 49x10 uint8 input from captured MFCC
 * frames.
 *
 * Applies time-shifting (unique position per index) and one of 8 augmentation
 * types, cycled via (aug_index % LEARN_NUM_AUG_TYPES).
 *
 * @param captured      Float MFCC frames captured during utterance
 * @param keyword_len   Number of frames in the captured keyword
 * @param aug_index     Augmentation index (0..augmentations_per_utterance-1)
 * @param num_augs      Total augmentations per utterance
 * @param output        Output buffer [SPECTROGRAM_COUNT][SPECTROGRAM_RES]
 */
static void generate_augmented_input(float captured[][SPECTROGRAM_RES], int keyword_len,
                                     int aug_index, int num_augs,
                                     uint8_t output[SPECTROGRAM_COUNT][SPECTROGRAM_RES]) {
    int available_padding = SPECTROGRAM_COUNT - keyword_len;
    if (available_padding < 0)
        available_padding = 0;

    /* --- Time shift: spread keyword across all positions evenly --- */
    int target_start;
    if (num_augs <= 1 || available_padding == 0)
        target_start = available_padding / 2;
    else
        target_start = (aug_index * available_padding) / (num_augs - 1);

    if (target_start < 0)
        target_start = 0;
    if (target_start + keyword_len > SPECTROGRAM_COUNT)
        target_start = SPECTROGRAM_COUNT - keyword_len;

    /* --- Determine augmentation type (cycle through 8 types) --- */
    int aug_type = aug_index % LEARN_NUM_AUG_TYPES;

    /* Augmentation parameters */
    float gain = 1.0f;
    float bg_noise_scale = 0.0f; /* background noise across whole window */
    int freq_mask_bin = -1;
    bool do_time_stretch = false;
    bool do_time_compress = false;
    int stretch_pos = -1;
    int compress_pos = -1;

    switch (aug_type) {
        case 0: /* Clean - no augmentation */
            break;
        case 1: /* Background noise across entire window */
            bg_noise_scale = 0.03f + learn_rand_float() * 0.05f; /* 3-8% */
            break;
        case 2:                                        /* Gain scaling */
            gain = 0.75f + learn_rand_float() * 0.50f; /* 0.75 - 1.25 */
            break;
        case 3:                                                  /* Background noise + gain */
            bg_noise_scale = 0.02f + learn_rand_float() * 0.03f; /* 2-5% */
            gain = 0.80f + learn_rand_float() * 0.40f;           /* 0.8 - 1.2 */
            break;
        case 4: /* Slight time-stretch (duplicate 1-2 frames) */
            do_time_stretch = true;
            stretch_pos = (int)(learn_rand_float() * (keyword_len - 1));
            break;
        case 5: /* Slight time-compress (skip 1-2 frames) */
            do_time_compress = true;
            if (keyword_len > LEARN_MIN_KEYWORD_FRAMES + 2)
                compress_pos = 1 + (int)(learn_rand_float() * (keyword_len - 2));
            break;
        case 6: /* Frequency masking (1 random MFCC bin) */
            freq_mask_bin = (int)(learn_rand_float() * (SPECTROGRAM_RES - 1));
            break;
        case 7: /* Heavy combined: bg noise + gain + freq mask */
            bg_noise_scale = 0.02f + learn_rand_float() * 0.02f; /* 2-4% */
            gain = 0.85f + learn_rand_float() * 0.30f;           /* 0.85 - 1.15 */
            freq_mask_bin = (int)(learn_rand_float() * (SPECTROGRAM_RES - 1));
            break;
    }

    /* --- Step 1: Fill entire window with silence or background noise --- */
    for (int i = 0; i < SPECTROGRAM_COUNT; i++) {
        for (int j = 0; j < SPECTROGRAM_RES; j++) {
            if (bg_noise_scale > 0.0f) {
                float noise = (learn_rand_float() - 0.5f) * 2.0f * bg_noise_scale * mfcc_fs;
                output[i][j] = clamp_uint8(((noise / mfcc_fs) + 1.0f) * 128.0f);
            } else {
                output[i][j] = 128; /* silence = normalized zero */
            }
        }
    }

    /* --- Step 2: Place keyword at target_start with augmentation --- */
    int dst = target_start;
    for (int src = 0; src < keyword_len && dst < SPECTROGRAM_COUNT; src++) {
        /* Time-compress: skip this frame */
        if (do_time_compress && src == compress_pos)
            continue;

        for (int j = 0; j < SPECTROGRAM_RES; j++) {
            if (j == freq_mask_bin) {
                output[dst][j] = 128; /* masked bin */
                continue;
            }
            float val = captured[src][j] * gain;
            if (bg_noise_scale > 0.0f) {
                /* Add signal on top of existing background noise */
                float existing = ((float)output[dst][j] / 128.0f - 1.0f) * mfcc_fs;
                val = val + existing;
            }
            float normalized = ((val / mfcc_fs) + 1.0f) * 128.0f;
            output[dst][j] = clamp_uint8(normalized);
        }
        dst++;

        /* Time-stretch: duplicate this frame */
        if (do_time_stretch && src == stretch_pos && dst < SPECTROGRAM_COUNT) {
            for (int j = 0; j < SPECTROGRAM_RES; j++) {
                if (j == freq_mask_bin) {
                    output[dst][j] = 128;
                    continue;
                }
                float val = captured[src][j] * gain;
                float normalized = ((val / mfcc_fs) + 1.0f) * 128.0f;
                output[dst][j] = clamp_uint8(normalized);
            }
            dst++;
        }
    }
}

/*---------------------------------------------------------------------------
 * Structured Edge Learning - Shared Buffers & Async Handlers
 *---------------------------------------------------------------------------*/

/* Shared augmented input buffer (accessed by learn_process_handler and
 * akd_learn_fetch_handler across work item invocations) */
__aligned(32) static uint8_t learn_aug_buf[SPECTROGRAM_COUNT][SPECTROGRAM_RES];

/**
 * @brief Learning fetch + chain handler. Runs once per augmentation.
 *        Fetches the Akida learning result, then either chains the next
 *        augmentation or completes the utterance.
 */
static void akd_learn_fetch_handler(struct k_work* work) {
    ARG_UNUSED(work);

    int32_t label = cur_kws_edge_novel_class;

    /* Retrieve learning result for the completed augmentation */
    if (-EFAILURE == akida_fetch((uint8_t*)learn_aug_buf, akd_op_size, false)) {
        LOG_ERR("learn: fetch EFAILURE for aug %d — retrying", learn_state.current_aug_idx);
        return;
    }

    learn_state.total_fit_calls++;
    int idx = ++learn_state.current_aug_idx;

    if ((idx % 10 == 0) || (idx == learn_state.num_augs)) {
        LOG_INF("learn: fit %d/%d done", idx, learn_state.num_augs);
    }

    if (idx < learn_state.num_augs) {
        /* Generate next augmentation and enqueue it */
        generate_augmented_input(learn_state.captured_mfcc, learn_state.keyword_len, idx,
                                 learn_state.num_augs, learn_aug_buf);

        uint64_t t0 = time_ms();
        int ret;
        do {
            ret = akida_enqueue((uint8_t*)learn_aug_buf, (uint32_t*)dims, &label);
            if ((time_ms() - t0) > ENQUEUE_TIMEOUT_MS) {
                LOG_ERR("learn enqueue timeout at aug %d", idx);
                learn_utterance_complete(); /* graceful abort */
                return;
            }
        } while (ret);

    } else {
        /* All augmentations fetched — advance utterance state machine */
        learn_utterance_complete();
    }
}

/*---------------------------------------------------------------------------
 * Structured Edge Learning - Processing & Completion Handlers
 *---------------------------------------------------------------------------*/

/**
 * @brief Complete the structured learning process: save weights and return
 *        to learn-select mode. The user must explicitly issue `el 0`
 *        (long-press button 0) to leave learn-select and resume inference.
 */
static void complete_structured_learning(void) {
    LOG_INF("learn: COMPLETE - %d utterances, %d total fit() calls", LEARN_NUM_UTTERANCES,
            learn_state.total_fit_calls);

    akida_learn_mode(true);
    if (SUCCESS ==
        save_weights_from_mesh(learn_weights_buff_ptr,
                               saved_learn_weights_ptr->learn_weights_data.learn_weights_size)) {
        saved_learn_weights_ptr->learn_weights_data.label_learnt_val |=
            1 << (cur_kws_edge_novel_class - KWS_EDGE_NOVEL_CLASS_BASE_ID);
        LOG_INF("learn: weights saved MESH->MEM");

        save_weights_to_flash();
        LOG_INF("learn: weights saved MEM->FLASH");

        learning_completed();
    } else {
        LOG_ERR("learn: akida_save_learn_weights failed for class %d", cur_kws_edge_novel_class);
    }
    akida_learn_mode(false);

    switch_mode(STATE_LEARN_SELECT);
#ifdef CONFIG_AKIDATAG_BOARD
    restore_default_led_state();
#endif
}

/**
 * @brief Advance to next utterance or complete learning.
 *        Called by both sync path (after for-loop) and async path
 *        (from akd_learn_fetch_handler when all augs done).
 */
static void learn_utterance_complete(void) {
    learn_state.current_utterance++;
    if (learn_state.current_utterance >= LEARN_NUM_UTTERANCES) {
        learn_state.sub_state = LEARN_SUB_COMPLETE;
        complete_structured_learning();
    } else {
        learn_state.sub_state = LEARN_SUB_WAITING_FOR_SPEECH;
        learn_state.waiting_since_ts = time_ms();
        learn_state.capture_write_idx = 0;
        learn_state.speech_detected = false;
        LOG_INF("learn: say keyword %d/%d", learn_state.current_utterance + 1,
                LEARN_NUM_UTTERANCES);
        k_work_reschedule(&learn_speech_end_work, K_MSEC(LEARN_SPEECH_END_GAP_MS));
#ifdef CONFIG_AKIDATAG_BOARD
        led_set_state(LED_STATE_LEARN_SPEAK_NOW);
#endif
    }
}

/**
 * @brief Trim captured MFCC buffer to just the keyword region using
 *        per-frame energy analysis.  Shifts keyword frames to the start
 *        of the buffer and returns the trimmed length.
 *
 * @param captured    Float MFCC capture buffer
 * @param total_frames Number of frames currently in the buffer
 * @return Trimmed keyword length (0 if no speech detected)
 */
static int trim_captured_keyword(float captured[][SPECTROGRAM_RES], int total_frames) {
    float max_energy = 0.0f;
    int max_idx = 0;
    float energies[LEARN_CAPTURE_MAX_FRAMES];

    for (int i = 0; i < total_frames; i++) {
        float energy = 0.0f;
        for (int j = 0; j < SPECTROGRAM_RES; j++) {
            float v = captured[i][j];
            energy += (v < 0.0f) ? -v : v;
        }
        energies[i] = energy;
        if (energy > max_energy) {
            max_energy = energy;
            max_idx = i;
        }
    }

    if (max_energy < 1.0f)
        return 0; /* no speech detected */

    float threshold = max_energy * 0.2f;

    /* Find start: scan backward from peak */
    int start = max_idx;
    while (start > 0 && energies[start - 1] > threshold)
        start--;

    /* Find end: scan forward from peak */
    int end = max_idx;
    while (end < total_frames - 1 && energies[end + 1] > threshold)
        end++;

    /* Add 2 frames padding on each side */
    start = (start > 2) ? start - 2 : 0;
    end = (end < total_frames - 3) ? end + 2 : total_frames - 1;

    int trimmed_len = end - start + 1;

    /* Shift keyword to beginning of buffer */
    if (start > 0) {
        memmove(captured[0], captured[start], trimmed_len * SPECTROGRAM_RES * sizeof(float));
    }

    return trimmed_len;
}

/**
 * @brief Work handler: generate augmented inputs and call fit() for one
 *        utterance.  Runs off the audio thread so fit() calls don't block
 *        audio capture.
 */
static void learn_process_handler(struct k_work* work) {
    ARG_UNUSED(work);

    int raw_len = learn_state.capture_write_idx;
    int32_t label = cur_kws_edge_novel_class;

    int keyword_len = trim_captured_keyword(learn_state.captured_mfcc, raw_len);
    LOG_INF("learn: trimmed to %d frames (was %d)", keyword_len, raw_len);

    if (keyword_len < LEARN_MIN_KEYWORD_FRAMES) {
        LOG_WRN("learn: utterance too short (%d frames), try again", keyword_len);
        learn_state.sub_state = LEARN_SUB_WAITING_FOR_SPEECH;
        learn_state.waiting_since_ts = time_ms();
        learn_state.capture_write_idx = 0;
        learn_state.speech_detected = false;
        LOG_INF("learn: say keyword %d/%d", learn_state.current_utterance + 1,
                LEARN_NUM_UTTERANCES);
        k_work_reschedule(&learn_speech_end_work, K_MSEC(LEARN_SPEECH_END_GAP_MS));
#ifdef CONFIG_AKIDATAG_BOARD
        led_set_state(LED_STATE_LEARN_SPEAK_NOW);
#endif
        return;
    }

    /* Cache context for akd_learn_fetch_handler to use across callbacks */
    learn_state.keyword_len = keyword_len;
    learn_state.num_augs = learn_state.augmentations_per_utterance;
    learn_state.current_aug_idx = 0;

    LOG_INF("learn: processing %d augmented inputs for utterance %d/%d", learn_state.num_augs,
            learn_state.current_utterance + 1, LEARN_NUM_UTTERANCES);
    /* Stays awake for the rest of the session, and is handed back by
     * kws_set_edge_state() on the transition out of STATE_LEARNING. This work
     * item can outlive the session it was queued for - kws_app_stop() and
     * switch_mode() both reach the transition from other threads - so the take
     * doubles as the liveness check: if it declines, the session is already over
     * and there is no reference keeping the chip awake for the enqueues below. */
    if (!akd_learn_wake_take()) {
        LOG_WRN(
            "learn: session ended before this utterance was processed, "
            "dropping it");
        return;
    }

    if (kws_api_selection == DEFAULT_API_SELECTION_ASYNC) {
        /* Async path: generate aug[0], enqueue, arm first fake ISR */
        generate_augmented_input(learn_state.captured_mfcc, keyword_len, 0, learn_state.num_augs,
                                 learn_aug_buf);
        uint64_t t0 = time_ms();
        int ret;
        do {
            ret = akida_enqueue((uint8_t*)learn_aug_buf, (uint32_t*)dims, &label);
            if ((time_ms() - t0) > ENQUEUE_TIMEOUT_MS) {
                LOG_ERR("learn initial enqueue timeout");
                return;
            }
        } while (ret);

    } else {
        /* Sync path: run all augmentations inline (unchanged) */
        for (int i = 0; i < learn_state.num_augs; i++) {
            generate_augmented_input(learn_state.captured_mfcc, keyword_len, i,
                                     learn_state.num_augs, learn_aug_buf);
            akida_fit((uint8_t*)learn_aug_buf, (uint32_t*)dims, &label);
            learn_state.total_fit_calls++;
            if ((i + 1) % 10 == 0 || i == learn_state.num_augs - 1) {
                LOG_INF("learn: fit %d/%d done", i + 1, learn_state.num_augs);
            }
        }
        learn_utterance_complete();
    }
}

/**
 * @brief Delayable work handler for speech-end detection and silence timeout.
 *
 * Fires LEARN_SPEECH_END_GAP_MS (500ms) after the last MFCC callback.
 * - In CAPTURING state: speech ended -> submit processing work.
 * - In WAITING state: check for 5s silence timeout -> re-prompt user.
 */
static void learn_speech_end_handler(struct k_work* work) {
    ARG_UNUSED(work);

    /* This handler re-arms itself, so a cancel alone cannot stop it if the cancel
     * lands while it is running. Checking the state here is what actually ends
     * the loop, and it also stops a stopped app being handed new work. */
    if (cur_kws_edge_state != STATE_LEARNING) {
        return;
    }

    if (learn_state.sub_state == LEARN_SUB_CAPTURING) {
        /* Speech ended - transition to processing */
        learn_state.sub_state = LEARN_SUB_PROCESSING;
        k_work_submit(&learn_process_work);

    } else if (learn_state.sub_state == LEARN_SUB_WAITING_FOR_SPEECH) {
        uint64_t elapsed = time_ms() - learn_state.waiting_since_ts;
        if (elapsed >= LEARN_SILENCE_TIMEOUT_MS) {
            LOG_WRN("learn: no utterance detected (timeout %dms), try again",
                    LEARN_SILENCE_TIMEOUT_MS);
            /* Reset and re-prompt */
            learn_state.waiting_since_ts = time_ms();
            LOG_INF("learn: say keyword %d/%d", learn_state.current_utterance + 1,
                    LEARN_NUM_UTTERANCES);
        }
        /* Keep polling for timeout */
        k_work_reschedule(&learn_speech_end_work, K_MSEC(LEARN_SPEECH_END_GAP_MS));
    }
}

/**
 * @brief Function to save learned weights from mesh into location pointed by
 * passed argument.
 * @param lbl_wts_ptr  - Weights to be saved.
 * @param size - number of bytes to be saved.
 */
static int32_t save_weights_from_mesh(uint8_t* lbl_wts_ptr, uint32_t size) {
    int32_t ret_val = -EFAILURE;
    /* save the learned weights from last layer and update them after programming
     * the model again */
    saved_learn_weights_ptr->learn_weights_data.learn_weights_size =
        akida_save_learn_weights((uint32_t*)lbl_wts_ptr, size);

    if (saved_learn_weights_ptr->learn_weights_data.learn_weights_size ==
        (int32_t)akida_learn_mem_size()) {
        ret_val = SUCCESS;
    }
    return ret_val;
}

/**
 * @brief Learning pipeline callback — receives raw spectrogram index directly
 *        from do_inference(), bypassing the uint8 normalization path.
 */
static void learning_on_spectrogram(int spectrogram_index) {
    /* Number of MFCC frames produced between consecutive do_inference() calls.
     * MFCC_PER_BLOCK (3) * g_inference_period (default 3) = 9 frames per cb. */
    const int frames_per_cb = get_audio_frames_cb();

    switch (learn_state.sub_state) {
        case LEARN_SUB_WAITING_FOR_SPEECH:
            /* The audio_process_thread only calls audio_processor() (and therefore
             * do_inference / this callback) when RMS >= threshold, so receiving a
             * callback here means speech has started. */
            learn_state.sub_state = LEARN_SUB_CAPTURING;
            learn_state.speech_detected = true;
            learn_state.capture_write_idx = 0;
            learn_state.last_callback_ts = time_ms();
            LOG_INF("learn: speech detected, capturing utterance %d/%d...",
                    learn_state.current_utterance + 1, LEARN_NUM_UTTERANCES);
#ifdef CONFIG_AKIDATAG_BOARD
            restore_default_led_state();
#endif
            /* Fall through to capture the first batch of frames */
            /* fallthrough */

        case LEARN_SUB_CAPTURING: {
            /* Copy the latest frames from the circular spectrogram into our linear
             * capture buffer.  spectrogram_index points to where the NEXT frame
             * will be written, so the most recent `frames_per_cb` frames are at
             * indices (spectrogram_index - frames_per_cb) .. (spectrogram_index - 1).
             */
            for (int i = frames_per_cb; i > 0; i--) {
                int src = (spectrogram_index - i + SPECTROGRAM_COUNT) % SPECTROGRAM_COUNT;
                if (learn_state.capture_write_idx < LEARN_CAPTURE_MAX_FRAMES) {
                    for (int j = 0; j < SPECTROGRAM_RES; j++) {
                        learn_state.captured_mfcc[learn_state.capture_write_idx][j] =
                            spectrogram[src][j];
                    }
                    learn_state.capture_write_idx++;
                }
            }
            learn_state.last_callback_ts = time_ms();

            /* Reschedule speech-end timer: if no callback for 500ms, speech ended */
            k_work_reschedule(&learn_speech_end_work, K_MSEC(LEARN_SPEECH_END_GAP_MS));
            break;
        }

        case LEARN_SUB_PROCESSING:
        case LEARN_SUB_COMPLETE:
            /* Do nothing - processing happens in work handler */
            break;
    }

    last_learn_ts = time_ms();
}

static void learning_on_user_input(int input_type) {
    uint32_t cur_ts;
    uint32_t mesh_mem;
    uint64_t duration_us;
    /* Both exit paths below call switch_mode() first, which hands the learning
     * session's wake reference back, and then read the learned weights out of the
     * Akida mesh. Hold a reference of our own across the whole handler so that
     * read cannot land on a clock-gated chip and come back as zeros. */
    AkdWakeScope akd_wake;
    switch (input_type) {
        case USER_INPUT_LP(0):
            switch_mode(STATE_INFERENCE);
            LOG_INF("learning->inference");
            akida_learn_mode(true);

            /* save the weights to flash only when labels are learnt and state changes
             * to STATE_INFERENCE */
            if (SUCCESS == save_weights_from_mesh(
                               learn_weights_buff_ptr,
                               saved_learn_weights_ptr->learn_weights_data.learn_weights_size)) {
                saved_learn_weights_ptr->learn_weights_data.label_learnt_val |=
                    1 << (cur_kws_edge_novel_class - KWS_EDGE_NOVEL_CLASS_BASE_ID);
                save_weights_to_flash();
                LOG_INF("Save Weights from MEM->FLASH");
            } else {
                LOG_ERR("Sync:akida_save_learn_weights function has failed for label %d ",
                        cur_kws_edge_novel_class);
            }

            akida_learn_mode(false);
#ifdef CONFIG_AKIDATAG_BOARD
            restore_default_led_state();
#endif

            break;
        case USER_INPUT_SP(0):
        case USER_INPUT_SP(1):
            cur_ts = k_cycle_get_32();

            switch_mode(STATE_LEARN_SELECT);
            LOG_INF("learning-> learnselect");

            akida_learn_mode(true);

            if (SUCCESS == save_weights_from_mesh(
                               learn_weights_buff_ptr,
                               saved_learn_weights_ptr->learn_weights_data.learn_weights_size)) {
                saved_learn_weights_ptr->learn_weights_data.label_learnt_val |=
                    1 << (cur_kws_edge_novel_class - KWS_EDGE_NOVEL_CLASS_BASE_ID);
                LOG_INF("Save Weights from MESH->MEM");
            } else {
                LOG_ERR("Sync:akida_save_learn_weights function has failed for label %d ",
                        cur_kws_edge_novel_class);
            }

            akida_learn_mode(false);
            mesh_mem = (k_cycle_get_32() - cur_ts);
            duration_us = k_cyc_to_us_floor64(mesh_mem);
            LOG_INF("mesh_mem = %" PRIu64 " us", duration_us);
#ifdef CONFIG_AKIDATAG_BOARD
            restore_default_led_state();
#endif

            break;
        default:
            break;
    }
}

/* function to run the inference */
extern "C" int infer(int app_index_l) {
    if (app_index_l > 0) {
        LOG_ERR("Illegal model index %d", app_index_l);
        return -1;
    }
    AkdWakeScope akd_wake; /* awake for programming + inference, every exit path */

    /* [bench] app end-to-end starts here (whole infer() call). */
    uint64_t bench_t0 = time_ms();
    uint32_t validate_ms = 0, program_ms = 0, prog_cfg_cycles = 0;

    /* Step 1: read header only to get flash_address without touching
     * sram_upload_buffer */
    model_meta_t infer_meta;
    int hdr_ret = file_transfer_read_meta_hdr_only(app_index_l, &infer_meta);
    if (hdr_ret != 0) {
        LOG_ERR("Metadata header unavailable (err %d)", hdr_ret);
        return -1;
    }
    uint32_t use_flash_addr = infer_meta.flash_address;

    /* Step 3: validate model name from the header (model_meta_t.model_name) */
    if (file_transfer_check_model_name(app_index_l, infer_meta.model_name) != 0) {
        LOG_ERR("Model name mismatch for slot %d: '%s'", app_index_l, infer_meta.model_name);
        return -1;
    }

    /* Step 2&4: load data meta and validate flash contents */
    model_data_meta_t infer_data_meta;
    int dm_ret = file_transfer_load_data_meta(app_index_l, &infer_data_meta);
    if (dm_ret == 0) {
        /* Step 4: full SPI flash CRC validation (part of "model loading"). Routing
         * is owned per chunk by the FlashClaim inside spi_flash_read_helper_func(),
         * not by this caller. */
        uint64_t t_val0 = time_ms();
        int val_ret = file_transfer_validate_flash_data(use_flash_addr, &infer_data_meta);
        validate_ms = (uint32_t)(time_ms() - t_val0);
        if (val_ret != 0) {
            LOG_ERR("Flash data validation FAILED for slot %d", app_index_l);
            return -1;
        }
    } else {
        /* Legacy fallback: 4-byte check only */
        LOG_ERR("No data meta file (err %d) ", dm_ret);
        return -1;
    }

    /* Step 5: reload full meta + program_info into sram_upload_buffer.
     * file_transfer_validate_flash_data() may have overwritten it. */
    int meta_ret = file_transfer_load_meta(app_index_l, &infer_meta);
    if (meta_ret != 0) {
        LOG_ERR("Metadata reload failed (err %d)", meta_ret);
        return -1;
    }
    if (update_model_params(infer_meta) != SUCCESS) {
        return -1;
    }

    /* Step 6: program Akida (model loading). Time it with the CONFIG-DMA counter
     * + wall clock (the wall time is what shrinks as the host SPI clock rises). */
    akida_toggle_clock_counter(true);
    uint64_t t_prog0 = time_ms();
    akida_program_flash(sram_upload_buffer, (int)infer_meta.info_data_len, infer_meta.flash_address,
                        &is_el_model);
    program_ms = (uint32_t)(time_ms() - t_prog0);
    /* Absolute config-DMA counter (it latches the model's config-DMA cycle count
     * ~2.1M; a before/after delta reads as noise because it does not advance on a
     * same-model re-program). This count is Akida-internal, host-clock-independent. */
    prog_cfg_cycles = akida_get_config_clock_counter();

    akida_batch_size(1, true);
    app_index = app_index_l;

    if (check_model_compatibility(is_el_model, infer_meta) != SUCCESS) {
        return -1;
    }

    /* clock counter already enabled before programming above */

    uint32_t s_dma_cycls = 0;
    uint64_t s_tick = 0;
    uint64_t e_tick = 0;
    uint32_t inf_time = 0;
    uint32_t e_dma_cycls = 0;
    uint32_t delta_cycle = 0;
    int num_classes = 10;
    int num_neurons_per_class = 1;

    num_classes = g_num_classes;
    num_neurons_per_class = g_num_neurons_per_class;

    int class_id = -1;
    uint32_t inp_shap[] = {infer_meta.input_shape[0], infer_meta.input_shape[1],
                           infer_meta.input_shape[2]};
#ifdef CONFIG_AKIDATAG_BOARD
    if (kws_api_selection == DEFAULT_API_SELECTION_ASYNC) {
        akd_irq_disable();
    }
#endif
    s_dma_cycls = akida_get_clock_counter();
    uint32_t fwd_cyc0 = k_cycle_get_32();
    s_tick = time_ms();
    int ret =
        akida_forward((uint8_t*)inputs[app_index_l], inp_shap, (uint8_t*)akida_output, akd_op_size);
    e_tick = time_ms();
    uint32_t fwd_wall_us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - fwd_cyc0);
    e_dma_cycls = akida_get_clock_counter();
    delta_cycle = e_dma_cycls - s_dma_cycls;
    inf_time = e_tick - s_tick;
    if (ret == SUCCESS) {
        class_id = get_inferred_class(akida_output, num_classes, num_neurons_per_class);
    } else {
        LOG_ERR(" inference failed ");
        return -1;
    }
    if (app_index_l == 0) {  // kws
        LOG_PRINTK("Class : %d\n", class_id);
        LOG_PRINTK("Word : %s\n",
                   (class_id >= 0 && class_id < kws_new_tags_count) ? kws_new_tags[class_id] : "?");
        kws_model_present = true;
        if (!is_kws_inference_started) {
            initiate_kws_inference(is_el_model);
        } else if (kws_threads_suspended) {
            k_thread_resume(capture_tid);
            k_thread_resume(process_tid);
            kws_model_present = false;
        }
    }

    /* [bench] consolidated per-frequency breakdown for the SPI-clock sweep. */
    uint32_t app_ms = (uint32_t)(time_ms() - bench_t0);
    const char* word =
        (class_id >= 0 && class_id < kws_new_tags_count) ? kws_new_tags[class_id] : "?";
    LOG_PRINTK("[bench] SPI=%u Hz (FREQ.reg=0x%08X)\n", akd_spi_get_frequency(),
               akd_spi_read_freq_reg());
    LOG_PRINTK(
        "[bench]  model-load : crc-validate=%u ms  program=%u ms  "
        "(config-dma=%u cyc ~%u us @%d MHz)\n",
        validate_ms, program_ms, prog_cfg_cycles, prog_cfg_cycles / AKIDA_FREQUENCY_MHZ,
        AKIDA_FREQUENCY_MHZ);
    LOG_PRINTK("[bench]  inference  : dma=%u cyc (%u us @%d MHz)  cpu-wall=%u us\n", delta_cycle,
               delta_cycle / AKIDA_FREQUENCY_MHZ, AKIDA_FREQUENCY_MHZ, fwd_wall_us);
    LOG_PRINTK("[bench]  app-e2e    : %u ms   result: class=%d word=%s\n", app_ms, class_id, word);

    LOG_PRINTK("APP Inference Completed\n");
#ifdef CONFIG_AKIDATAG_BOARD
    if (kws_api_selection == DEFAULT_API_SELECTION_ASYNC) {
        akd_irq_enable();
    }
#endif
    return 0;
}

/* shell cli function to invoke infer function */
static int cmd_infer(const struct shell* shell, size_t argc, char** argv) {
    if (argc != 2) {
        LOG_INF("Usage: infer <string>");
        return -EINVAL;
    }

    char* string = argv[1];
    if (!strcmp(string, "kws")) {
        app_index = 0;
        LOG_INF("inference kws requested, app index %d", app_index);
    } else {
        LOG_ERR("Illegal model inference request");
        return -EINVAL;
    }

    /* Pause the background continuous-KWS threads so this one-shot infer does not
     * race the async thread on the single shared Akida device / SPI bus. */
    bool was_running = kws_app_running;
    if (was_running) {
        kws_app_stop();
    }
    int rc = infer(app_index);
    if (was_running) {
        kws_app_start();
    }
    return rc;
}

/* shell cli function to set external host MCU/AKD1500 as SPI master */
static int cmd_set(const struct shell* shell, size_t argc, char** argv) {
    if (argc != 2) {
        LOG_INF("Usage: set <bool>");
        return -EINVAL;
    }
    size_t value = strtoul(argv[1], NULL, 0);

    LOG_INF("Value = %d", value);
    if (value != 0 && value != 1) {
        LOG_INF("Invalid <bool> value %d", value);
        return -EINVAL;
    }
    if (value == 1) {
        akida_config_spi(value);
        spi_flash_read_id(spi_driver);
    } else {
        akida_config_spi(value);
    }

    return 0;
}

/* ===================================================================== */
/* AKD1500 high-freq SPI investigation helpers (exploration only).        */
/* ===================================================================== */

#define AKD_DEVICE_ID_REG 0xFCC00000U
/* akd1500.read() returns the native-uint32 word; the AKD1500 device ID, shown
 * MSB-first as bytes "09 03 A1 BC", reads back as the word 0xBCA10309. */
#define AKD_DEVICE_ID_VAL 0xBCA10309U
/* AKD1500 clock/reset controller (base 0xFCE0_1000) + Chip-Info (0xFCE0_0010) */
#define AKD_CHIP_INFO_REG 0xFCE00010U   /* [9:8]=OP_MODE, [13]=SEL_CLK */
#define AKD_CLK_GENCTRL_REG 0xFCE01000U /* [0]=PLLCLK_SEL [1]=SEL_MAN [4]=BYPASS */
#define AKD_CLK_PLLCTRL_REG 0xFCE01010U
#define AKD_CLK_PLLSTAT_REG 0xFCE01014U /* [0]=PLL_LOCK */
#define AKD_CLK_DIVUPD_REG 0xFCE0102CU  /* [0]=DIV_UPDATE_EN */
#define AKD_CLK_SYSDIV_REG 0xFCE01030U
#define AKD_CLK_SPISDIV_REG 0xFCE0103CU /* [7:0]=SPIS_DIV_RATIO (reset 2) */

static uint32_t akd_reg_rd(uint32_t addr) {
    uint32_t v = 0;
    akd1500.read(addr, &v, 4);
    return v;
}
static void akd_reg_wr(uint32_t addr, uint32_t val) {
    akd1500.write(addr, &val, 4);
}

/* Read one 32-bit AKD1500 register at the current SPI clock. */
static int cmd_akida_rd(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(sh, "Usage: akida_rd <hexaddr>");
        return -EINVAL;
    }
    uint32_t addr = strtoul(argv[1], NULL, 0);
    shell_print(sh, "AKD[0x%08X] = 0x%08X", addr, akd_reg_rd(addr));
    return 0;
}

/* Write one 32-bit AKD1500 register, then read it back. */
static int cmd_akida_wr(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 3) {
        shell_print(sh, "Usage: akida_wr <hexaddr> <hexval>");
        return -EINVAL;
    }
    uint32_t addr = strtoul(argv[1], NULL, 0);
    uint32_t val = strtoul(argv[2], NULL, 0);
    akd_reg_wr(addr, val);
    shell_print(sh, "AKD[0x%08X] <= 0x%08X (readback 0x%08X)", addr, val, akd_reg_rd(addr));
    return 0;
}

#ifdef CONFIG_AKIDATAG_BOARD
/* akd_sleep <0|1>: take (0) or release (1) the SHELL'S OWN wake reference.
 *
 * It does not drive the SLEEP pin. The pin follows a reference count (see
 * akd_wake_get in gpio.h) and the shell is one holder among several, so `1`
 * gives the shell's reference back and the chip sleeps only once every other
 * holder — a KWS inference, a learning session, an in-progress flash access —
 * has given theirs back too. The command prints the remaining count so the
 * difference is visible. Both directions are idempotent, and the AKD1500 clock
 * commands take this same single reference, which is how they leave the chip
 * readable for follow-up probes.
 *
 * This is why the command no longer refuses while KWS is running: it cannot
 * fight the per-inference duty cycle any more, because it can only give back
 * what the shell itself took. */
static int cmd_akd_sleep(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(sh, "Usage: akd_sleep <0|1>");
        return -EINVAL;
    }
    bool sleep = strtoul(argv[1], NULL, 0) != 0;
    if (sleep) {
        akd_wake_owner_release(&akd_dbg_wake_held);
    } else {
        akd_wake_owner_take(&akd_dbg_wake_held);
    }
    unsigned int refs = akd_wake_count();
    shell_print(sh, "shell wake reference %s; %u reference(s) held, AKD1500 %s",
                sleep ? "released" : "taken", refs, refs ? "awake" : "sleeping");
    return 0;
}
#endif

/* Drop to a safe 1.4 MHz clock and dump/decode the AKD1500 clock state. This
 * tells us whether the SPI_S core is on the fast PLL or the slow bypass clock
 * (the ¼-rule then sets the true max host SCK). Leaves the clock at 1.4 MHz. */
static int cmd_akd_clkinfo(const struct shell* sh, size_t argc, char** argv) {
    if (kws_app_running) {
        kws_app_stop();
    }
    /* Hold the shell's wake reference: an asleep chip reads all-0 (misleading
     * decode), and the command leaves the chip readable for the follow-up probes
     * it recommends. `akd_sleep 1` gives the reference back. */
    akd_wake_owner_take(&akd_dbg_wake_held);
    uint32_t op_hz = akd_spi_get_frequency(); /* the operating host clock */
    akd_spi_set_frequency(1400000);
    (void)akd_reg_rd(AKD_DEVICE_ID_REG); /* flush the pointer-swap reconfigure */

    uint32_t devid = akd_reg_rd(AKD_DEVICE_ID_REG);
    uint32_t chip = akd_reg_rd(AKD_CHIP_INFO_REG);
    uint32_t gen = akd_reg_rd(AKD_CLK_GENCTRL_REG);
    uint32_t pllc = akd_reg_rd(AKD_CLK_PLLCTRL_REG);
    uint32_t plls = akd_reg_rd(AKD_CLK_PLLSTAT_REG);
    uint32_t divu = akd_reg_rd(AKD_CLK_DIVUPD_REG);
    uint32_t sysd = akd_reg_rd(AKD_CLK_SYSDIV_REG);
    uint32_t spisd = akd_reg_rd(AKD_CLK_SPISDIV_REG);

    uint32_t op_mode = (chip >> 8) & 0x3;
    uint32_t sel_clk = (chip >> 13) & 0x1;
    uint32_t pllclk_sel = gen & 0x1;
    uint32_t pllclk_sel_man = (gen >> 1) & 0x1;
    uint32_t pll_bypass = (gen >> 4) & 0x1;
    uint32_t pll_lock = plls & 0x1;
    uint32_t spis_div = spisd & 0xff;
    uint32_t sys_div = sysd & 0xff;
    uint32_t divf_val = (pllc & 0x1ff) + 1;
    uint32_t divr_val = ((pllc >> 16) & 0x3f) + 1;
    uint32_t divq_val = 1u << ((pllc >> 24) & 0x7);
    uint32_t pllout_mhz = (50u * divf_val) / (divr_val * divq_val);
    uint32_t vco_mhz = (50u * divf_val) / divr_val;
    uint32_t pllclk_mhz = pllclk_sel ? pllout_mhz : 25u;

    shell_print(sh, "--- AKD1500 clock state (operating host ~%u Hz; read @1.4 MHz) ---", op_hz);
    shell_print(sh, "DEVICE_ID   0x%08X (%s)", devid,
                devid == AKD_DEVICE_ID_VAL ? "OK" : "BAD-read");
    shell_print(sh, "CHIP_INFO   0x%08X : OP_MODE=%u (bit9=OP_MODE1=%s), SEL_CLK=%u", chip, op_mode,
                (op_mode & 0x2) ? "SAFE" : "normal", sel_clk);
    shell_print(sh, "GEN_CTRL    0x%08X : PLLCLK_SEL=%u PLLCLK_SEL_MAN=%u PLL_BYPASS=%u", gen,
                pllclk_sel, pllclk_sel_man, pll_bypass);
    shell_print(sh, "PLL_CTRL    0x%08X : DIVF=%u DIVR=%u DIVQ=%u => PLLOUT ~%u MHz (VCO ~%u MHz)",
                pllc, divf_val, divr_val, divq_val, pllout_mhz, vco_mhz);
    shell_print(sh, "PLL_STATUS  0x%08X : PLL_LOCK=%u", plls, pll_lock);
    shell_print(sh, "DIV_UPDATE  0x%08X   SYS_DIV 0x%08X (ratio=%u)   SPIS_DIV 0x%08X (ratio=%u)",
                divu, sysd, sys_div, spisd, spis_div);

    /* Infer the SPI_S core clock and the ¼-rule host ceiling, plus the core clock. */
    const char* src = pllclk_sel ? "PLL" : "REF(25MHz)";
    uint32_t spis_core_mhz = spis_div ? pllclk_mhz / spis_div : pllclk_mhz;
    uint32_t sys_core_mhz = sys_div ? pllclk_mhz / sys_div : pllclk_mhz;
    shell_print(sh, "=> SPI_S core src=%s div=%u => ~%u MHz => host SCK ceiling ~%u MHz", src,
                spis_div, spis_core_mhz, spis_core_mhz / 4);
    shell_print(sh, "=> core (SYS) src=%s div=%u => ~%u MHz", src, sys_div, sys_core_mhz);
    shell_print(sh, "(clock left at 1.4 MHz; use akd_pll_on then spi_freq/akd_probe)");
    return 0;
}

/* Manually switch the AKD1500 SPI_S core clock onto the 400 MHz PLL (needed in
 * Safe Mode, where the auto-switch is disabled). Runs entirely at 1.4 MHz. Per
 * AKD500 clkrst spec §9.1.2. Leaves the clock at 1.4 MHz. */
static int cmd_akd_pll_on(const struct shell* sh, size_t argc, char** argv) {
    if (kws_app_running) {
        kws_app_stop();
    }
    akd_spi_set_frequency(1400000);
    (void)akd_reg_rd(AKD_DEVICE_ID_REG);

    uint32_t gen = akd_reg_rd(AKD_CLK_GENCTRL_REG);
    shell_print(sh, "before: GEN_CTRL=0x%08X PLL_LOCK=%u", gen,
                akd_reg_rd(AKD_CLK_PLLSTAT_REG) & 1);

    akd_core_clock_to_pll(); /* shared with the boot path */

    uint32_t genr = akd_reg_rd(AKD_CLK_GENCTRL_REG);
    uint32_t spisd = akd_reg_rd(AKD_CLK_SPISDIV_REG) & 0xff;
    uint32_t core = spisd ? 800 / spisd : 800;
    shell_print(
        sh, "after:  GEN_CTRL=0x%08X PLLCLK_SEL=%u SPIS_DIV=%u => core ~%u MHz => ceiling ~%u MHz",
        genr, genr & 1, spisd, core, core / 4);
    shell_print(sh, "(clock left at 1.4 MHz; now raise with spi_freq/akd_probe)");
    return 0;
}

/* Set the AKD1500 core clock (Hz). Refuse while KWS runs — it sleeps the chip
 * per-inference (clocks gated), so a change would race and not land; stop it
 * first (app stop). Wakes the chip so the write lands after a stop. */
static int cmd_akd_coreclk(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(sh, "Usage: akd_coreclk <hz>");
        return -EINVAL;
    }
    if (kws_app_running) {
        shell_print(sh, "KWS running — stop it first (app stop)");
        return -EBUSY;
    }
    uint32_t hz = strtoul(argv[1], NULL, 0);
    akd_wake_owner_take(&akd_dbg_wake_held);
    int rc = akd_core_clock_set(hz);
    if (rc) {
        shell_error(sh, "akd_coreclk %u failed (err %d)", hz, rc);
        return rc;
    }
    uint32_t v = akd_reg_rd(AKD_DEVICE_ID_REG);
    shell_print(sh, "core clock %u Hz; DEVICE_ID 0x%08X (%s)", hz, v,
                v == AKD_DEVICE_ID_VAL ? "OK" : "BAD-read");
    return 0;
}

/* Reprogram the AKD1500 PLL output (Hz), 600e6..800e6 in 12.5 MHz steps. Drops
 * host SPI during the switch, so refuse while KWS runs. Leaves host at 1.4 MHz. */
static int cmd_akd_pll(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(sh, "Usage: akd_pll <hz>  (600e6..800e6, 12.5 MHz steps)");
        return -EINVAL;
    }
    if (kws_app_running) {
        shell_print(sh, "KWS running — stop it first (app stop)");
        return -EBUSY;
    }
    uint32_t hz = strtoul(argv[1], NULL, 0);
    akd_wake_owner_take(&akd_dbg_wake_held);
    int rc = akd_pll_set(hz);
    if (rc) {
        shell_error(sh, "akd_pll %u failed (err %d)", hz, rc);
        return rc;
    }
    uint32_t v = akd_reg_rd(AKD_DEVICE_ID_REG);
    shell_print(sh, "PLL %u Hz; DEVICE_ID 0x%08X (%s) (host @1.4 MHz; raise with spi_freq)", hz, v,
                v == AKD_DEVICE_ID_VAL ? "OK" : "BAD-read");
    return 0;
}

/* Set the core (SYS) divider off the current PLLCLK: akd_sysdiv <n>. Refuse while
 * KWS runs (chip sleeps per-inference); wakes the chip so the write lands. */
static int cmd_akd_sysdiv(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(sh, "Usage: akd_sysdiv <n>");
        return -EINVAL;
    }
    if (kws_app_running) {
        shell_print(sh, "KWS running — stop it first (app stop)");
        return -EBUSY;
    }
    uint32_t n = strtoul(argv[1], NULL, 0);
    akd_wake_owner_take(&akd_dbg_wake_held);
    int rc = akd_sys_div_set(n);
    if (rc) {
        shell_error(sh, "akd_sysdiv %u rejected (err %d)", n, rc);
        return rc;
    }
    uint32_t v = akd_reg_rd(AKD_DEVICE_ID_REG);
    shell_print(sh, "SYS_DIV=%u; DEVICE_ID 0x%08X (%s)", n, v,
                v == AKD_DEVICE_ID_VAL ? "OK" : "BAD-read");
    return 0;
}

/* Run the AKD1500 from the 25 MHz reference (1) or back onto the PLL (0). Drops
 * host SPI, so refuse while KWS runs. Leaves host at 1.4 MHz. */
static int cmd_akd_clkref(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(sh, "Usage: akd_clkref <0|1>");
        return -EINVAL;
    }
    if (kws_app_running) {
        shell_print(sh, "KWS running — stop it first (app stop)");
        return -EBUSY;
    }
    bool on = strtoul(argv[1], NULL, 0) != 0;
    akd_wake_owner_take(&akd_dbg_wake_held);
    int rc = akd_clk_use_ref(on);
    uint32_t v = akd_reg_rd(AKD_DEVICE_ID_REG);
    shell_print(sh, "AKD1500 clock: %s; DEVICE_ID 0x%08X (%s) (host @1.4 MHz)",
                on ? "25 MHz ref" : "PLL", v, v == AKD_DEVICE_ID_VAL ? "OK" : "BAD-read");
    return rc;
}

/* Read the device-ID <count> times at the CURRENT clock; report mismatches. */
static int cmd_akd_rdtest(const struct shell* sh, size_t argc, char** argv) {
    uint32_t count = (argc >= 2) ? strtoul(argv[1], NULL, 0) : 100;
    uint32_t bad = 0, sample = 0;
    uint32_t first = akd_reg_rd(AKD_DEVICE_ID_REG);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = akd_reg_rd(AKD_DEVICE_ID_REG);
        if (v != AKD_DEVICE_ID_VAL) {
            bad++;
            sample = v;
        }
    }
    shell_print(
        sh, "rdtest @%u Hz (FREQ.reg=0x%08X): %u reads, %u BAD, first=0x%08X sample=0x%08X => %s",
        akd_spi_get_frequency(), akd_spi_read_freq_reg(), count, bad, first, sample,
        bad == 0 ? "PASS" : "FAIL");
    return 0;
}

/* Self-contained sweep point: set freq, flush reconfigure, apply rx-delay,
 * then read device-ID <count> times. Usage: akd_probe <hz> <rxdelay> [count] */
static int cmd_akd_probe(const struct shell* sh, size_t argc, char** argv) {
    if (argc < 3) {
        shell_print(sh, "Usage: akd_probe <hz> <rxdelay 0-7> [count=200]");
        return -EINVAL;
    }
    uint32_t hz = strtoul(argv[1], NULL, 0);
    uint32_t rxd = strtoul(argv[2], NULL, 0);
    uint32_t count = (argc >= 4) ? strtoul(argv[3], NULL, 0) : 200;

    if (kws_app_running) {
        kws_app_stop();
    }
    if (akd_spi_set_clock(hz) != 0) {
        shell_error(sh, "freq %u rejected", hz);
        return -EINVAL;
    }
    (void)akd_reg_rd(AKD_DEVICE_ID_REG); /* flush reconfigure (applies freq) */
    akd_spi_apply_rxdelay(rxd);          /* apply rx-delay after freq settles */

    uint32_t bad = 0, sample = 0, first = akd_reg_rd(AKD_DEVICE_ID_REG);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = akd_reg_rd(AKD_DEVICE_ID_REG);
        if (v != AKD_DEVICE_ID_VAL) {
            bad++;
            sample = v;
        }
    }
    shell_print(sh,
                "probe req=%u eff=%u FREQ.reg=0x%08X rxd=%u : %u reads %u BAD first=0x%08X "
                "sample=0x%08X => %s",
                hz, akd_spi_get_frequency(), akd_spi_read_freq_reg(), rxd, count, bad, first,
                sample, bad == 0 ? "PASS" : "FAIL");
    return 0;
}

/* Set IFTIMING.RXDELAY at runtime (apply after a spi_freq change). */
static int cmd_spi_rxdelay(const struct shell* sh, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(sh, "Usage: spi_rxdelay <0-7>");
        return -EINVAL;
    }
    akd_spi_apply_rxdelay(strtoul(argv[1], NULL, 0));
    return 0;
}

/* shell cli function to set the AKD1500 host SPI clock at runtime, for sweeping
 * rates (e.g. 8/16/32 MHz) to find the reliable maximum. Stops KWS to avoid
 * racing an in-flight transfer, then reads back the device ID at the new clock
 * as an integrity check. */
static int cmd_spi_freq(const struct shell* shell, size_t argc, char** argv) {
    if (argc != 2) {
        shell_print(shell, "Usage: spi_freq <hz>  (current: %u Hz)", akd_spi_get_frequency());
        return -EINVAL;
    }
    uint32_t hz = strtoul(argv[1], NULL, 0);

    bool was_running = kws_app_running;
    if (was_running) {
        kws_app_stop();
    }

    int rc = akd_spi_set_clock(hz);
    if (rc == 0) {
        /* Pure register read at the new clock; garbage/timeout here means the rate
         * is too high for the current SPIM instance / wiring. */
        get_akida_device_id(shell);
        shell_print(shell,
                    "SPI freq now %u Hz (SPIM4 FREQUENCY reg=0x%08X) - check the "
                    "device ID above is valid",
                    akd_spi_get_frequency(), akd_spi_read_freq_reg());
    } else {
        shell_error(shell, "spi_freq failed (err %d); still %u Hz", rc, akd_spi_get_frequency());
    }

    if (was_running) {
        kws_app_start();
    }
    return rc;
}

/* shell cli function to invoke erase function */
static int cmd_full_erase(const struct shell* shell, size_t argc, char** argv) {
    led_set_state(LED_STATE_FLASH_WRITE);
    if (spi_flash_erase_helper_func(0x1000, FLASH_MAX_16_MB_SIZE - 0x1000)) {
        return 1;
    }
    /* After success pattern, return to NORMAL */
    /* Restore correct runtime state */
    if (is_ble_connected()) {
        led_set_state(LED_STATE_BLE_CONNECTED);
    } else {
        led_set_state(LED_STATE_NORMAL_APP);
    }
    return 0;
}

/* shell cli function to invoke erase function */
#ifdef CONFIG_AKIDATAG_BOARD
/* `app stop` deep idle: PLL off (25 MHz ref) + sleep. */
static void kws_enter_low_power(void) {
    akd_wake_get();
    akd_clk_use_ref(true);
    akd_wake_put();
}
/* `app start` operating state: PLL on (800 MHz, 400 MHz core), host clock
 * restored, asleep until the first inference. */
static void kws_exit_low_power(void) {
    akd_wake_get();
    akd_clk_use_ref(false);
    akd_spi_set_clock(CONFIG_AKD_SPI_FREQ_HZ);
    akd_wake_put();
}
#endif

static int cmd_app(const struct shell* shell, size_t argc, char** argv) {
    LOG_INF("cmd exec argc %d", argc);
    if (argc > 1) {
        if (argc > 2 && !strcmp(argv[1], "verbose")) {
            verbose_on = atoi(argv[2]);
            LOG_INF("verbose_on = %d", verbose_on);
        } else if (!strcmp(argv[1], "stop")) {
            kws_app_stop();
#ifdef CONFIG_AKIDATAG_BOARD
            kws_enter_low_power();
#endif
        } else if (!strcmp(argv[1], "start")) {
#ifdef CONFIG_AKIDATAG_BOARD
            kws_exit_low_power();
#endif
            kws_app_start();
        } else if (!strcmp(argv[1], "el")) {
            if (argc > 2) {
                LOG_INF(" cur_kws_edge_state %d", cur_kws_edge_state);
                if (is_el_model == 0) {
                    LOG_ERR(" illegal request, this is not an edge learning model");
                    return 0;
                }

                if (cur_kws_edge_state == STATE_STOPPED) {
                    LOG_INF(" cur_kws_edge_state is STATE_STOPPED user input not possible");
                    return 0;
                }
                kws_edge_state[cur_kws_edge_state].on_user_input(atoi(argv[2]));
            }
        } else if (argc > 2 && !strcmp(argv[1], "rms")) {
            kws_cfg_err_t e = kws_config_set_from_string(KWS_PARAM_RMS, argv[2]);
            if (e == KWS_CFG_OK || e == KWS_CFG_ERR_NVS)
                LOG_INF("rms_threshold = %d", rms_threshold);
            else
                LOG_ERR("rms set failed (err %d)", (int)e);
        } else if (argc > 2 && !strcmp(argv[1], "debounce")) {
            kws_cfg_err_t e = kws_config_set_from_string(KWS_PARAM_DEBOUNCE_MS, argv[2]);
            if (e == KWS_CFG_OK || e == KWS_CFG_ERR_NVS)
                LOG_INF("kws_debounce_time = %u ms", kws_debounce_time);
            else
                LOG_ERR("debounce set failed (err %d)", (int)e);
        } else if (argc > 2 && !strcmp(argv[1], "alpha")) {
            smoothing_alpha = atof(argv[2]);
            if (smoothing_alpha < 0.0f)
                smoothing_alpha = 0.0f;
            if (smoothing_alpha > 1.0f)
                smoothing_alpha = 1.0f;
            LOG_INF("smoothing_alpha = %.2f", smoothing_alpha);
        } else if (argc > 2 && !strcmp(argv[1], "chiming")) {
            kws_cfg_err_t e = kws_config_set_from_string(KWS_PARAM_CHIMING, argv[2]);
            if (e == KWS_CFG_OK || e == KWS_CFG_ERR_NVS)
                LOG_INF("chiming_threshold = %d", chiming_threshold);
            else
                LOG_ERR("chiming set failed (err %d)", (int)e);
        } else if (argc > 2 && !strcmp(argv[1], "score")) {
            score_threshold = atof(argv[2]);
            if (score_threshold < 0.0f)
                score_threshold = 0.0f;
            if (score_threshold > 1.0f)
                score_threshold = 1.0f;
            LOG_INF("score_threshold = %.2f", score_threshold);
        } else if (argc > 2 && !strcmp(argv[1], "speech")) {
            kws_cfg_err_t e = kws_config_set_from_string(KWS_PARAM_SPEECH_TIMEOUT, argv[2]);
            if (e == KWS_CFG_OK || e == KWS_CFG_ERR_NVS)
                LOG_INF("speech_active_time_ms = %d ms", speech_active_time_ms);
            else
                LOG_ERR("speech set failed (err %d)", (int)e);
        } else if (!strcmp(argv[1], "reset")) {
            kws_cfg_err_t e = kws_config_reset_to_defaults();
            LOG_INF("app reset: defaults restored%s",
                    e == KWS_CFG_ERR_NVS ? " (NVS save warned)" : "");
            LOG_INF("  rms_threshold    = %d", rms_threshold);
            LOG_INF("  debounce_time    = %u ms", kws_debounce_time);
            LOG_INF("  smoothing_alpha  = %.2f", smoothing_alpha);
            LOG_INF("  score_threshold  = %.2f", score_threshold);
            LOG_INF("  chiming_threshold= %d", chiming_threshold);
            LOG_INF("  speech_timeout   = %d ms", speech_active_time_ms);
        } else if (argc > 2 && !strcmp(argv[1], "metrics")) {
            metrics_on = atoi(argv[2]);
            LOG_INF("metrics_on = %d", metrics_on);
        } else if (argc > 2 && !strcmp(argv[1], "blkms")) {
            /* Change the PDM capture block size (ms). Stop the pipeline, reconfigure
             * the DMA, then restart if it was running. Must be a multiple of 20 ms. */
            uint32_t ms = strtoul(argv[2], NULL, 0);
            bool was_running = kws_app_running;
            if (was_running) {
                kws_app_stop();
            }
            int rc = audio_set_block_ms(ms);
            if (rc != 0) {
                LOG_ERR("blkms failed (err %d); still %u ms", rc, audio_get_block_ms());
            } else {
                LOG_INF("audio block size = %u ms", audio_get_block_ms());
            }
            if (was_running) {
                kws_app_start();
            }
        } else if (!strcmp(argv[1], "show")) {
            LOG_INF("=== App Parameters (app <cmd> <val>) ===");
            LOG_INF("  verbose          = %d          [app verbose <0|1|2>]", verbose_on);
            LOG_INF("  rms_threshold    = %d          [app rms <val>]", rms_threshold);
            LOG_INF("  debounce_time    = %u ms       [app debounce <ms>]", kws_debounce_time);
            LOG_INF("  smoothing_alpha  = %.2f        [app alpha <0.0-1.0>]", smoothing_alpha);
            LOG_INF("  score_threshold  = %.2f        [app score <0.0-1.0>]", score_threshold);
            LOG_INF("  chiming_threshold= %d          [app chiming <n>]", chiming_threshold);
            LOG_INF("  speech_timeout   = %d ms       [app speech <ms>]", speech_active_time_ms);
            LOG_INF("  metrics          = %d          [app metrics <0|1>]", metrics_on);
            LOG_INF("  block_size       = %u ms       [app blkms <20|40|60|80>]",
                    audio_get_block_ms());
            LOG_INF("=========================================");
        } else {
            LOG_INF("App Commands:");
            LOG_INF("  app verbose <0|1|2>  (0=off, 1=pipeline, 2=+idle rms)");
            LOG_INF("  app rms <val>");
            LOG_INF("  app debounce <ms>");
            LOG_INF("  app alpha <0.0-1.0>");
            LOG_INF("  app score <0.0-1.0>");
            LOG_INF("  app chiming <n>");
            LOG_INF("  app speech <ms>");
            LOG_INF("  app reset                (restore all KWS params to defaults)");
            LOG_INF("  app metrics <0|1>");
            LOG_INF("  app blkms <ms>           (PDM block size, multiple of 20)");
            LOG_INF("  app show");
            LOG_INF("  app start                (resume KWS pipeline)");
            LOG_INF("  app stop                 (halt KWS pipeline)");
            LOG_INF("  app el <n>");
        }
    }

    return 0;
}
void edge_learning_cmd_process(uint8_t value) {
    LOG_INF("cur_kws_edge_state %d", cur_kws_edge_state);

    kws_edge_state[cur_kws_edge_state].on_user_input(value);
}

#if IS_ENABLED(CONFIG_WDT_ENABLE)
/**
 * @brief CLI command to stop all worker threads.
 *
 * This shell command aborts all active worker threads using
 * k_thread_abort(). The thread IDs are cleared after aborting.
 *
 * Once the worker threads are stopped:
 *  - Health flags will no longer be updated
 *  - all_threads_healthy() will return false
 *  - Watchdog feeding will stop
 *  - The system will reset after the watchdog timeout
 *
 * Usage:
 *   threads_stop
 *
 * @param shell Pointer to the Zephyr shell instance.
 * @param argc  Argument count (unused).
 * @param argv  Argument vector (unused).
 *
 * @return 0 Always returns 0.
 */
static int cmd_threads_stop(const struct shell* shell, size_t argc, char** argv) {
    bool any_thread_stopped = false;
    shell_print(shell, "Stopping all worker threads...");

    if (capture_tid) {
        k_thread_abort(capture_tid);
        capture_tid = NULL;
        any_thread_stopped = true;
    }

    if (process_tid) {
        k_thread_abort(process_tid);
        process_tid = NULL;
        any_thread_stopped = true;
    }

#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
    if (imu_tid) {
        k_thread_abort(imu_tid);
        imu_tid = NULL;
        any_thread_stopped = true;
    }
#endif

    if (!any_thread_stopped) {
        shell_print(shell, "No threads have been initialized.");
    } else {
        shell_print(shell, "Threads stopped successfully.");
    }

    return 0;
}

SHELL_CMD_REGISTER(threads_stop, NULL, "Stop all worker threads", cmd_threads_stop);
#endif

/**
 * @brief Shell command to set KWS (Keyword Spotting) API mode
 *
 * This command allows switching between Sync and Async modes at runtime.
 *
 * Usage:
 *   kws_mode <sync|async>
 *
 * @param shell Shell instance used for printing output
 * @param argc  Argument count
 * @param argv  Argument vector (expects mode as argv[1])
 *
 * @return 0 on success, negative error code on failure
 */
static int cmd_kws_mode(const struct shell* shell, size_t argc, char** argv) {
    if (argc < 2) {
        shell_print(shell, "Usage: kws_mode <sync|async>");
        return -EINVAL;
    }

    if (strcmp(argv[1], "async") == 0) {
#ifdef CONFIG_AKIDATAG_BOARD
        akida_init(DEFAULT_API_SELECTION_ASYNC);
        shell_print(shell, "Switched to ASYNC mode");
#else
        shell_print(shell,
                    "Warning: Async mode is not supported on nRF DK board. "
                    "Falling back to Sync mode.\n");
        return -EINVAL;
#endif

    } else if (strcmp(argv[1], "sync") == 0) {
        akida_init(DEFAULT_API_SELECTION_SYNC);
        shell_print(shell, "Switched to SYNC mode");

    } else {
        shell_print(shell, "Invalid mode. Use sync or async");
        return -EINVAL;
    }

    return 0;
}

/**
 * @brief Shell command to retrieve the current KWS API mode
 *
 * This command prints the currently active Keyword Spotting (KWS) mode
 * based on the global `kws_api_selection` setting.
 *
 * Behavior:
 * - Displays "ASYNC" if async mode is enabled.
 * - Displays "SYNC" if sync mode is enabled.
 *
 * Usage:
 *   kws_mode_get
 *
 * @param shell Shell instance used for output
 * @param argc  Argument count (unused)
 * @param argv  Argument vector (unused)
 *
 * @return Always returns 0
 */
static int cmd_kws_mode_get(const struct shell* shell, size_t argc, char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (kws_api_selection == DEFAULT_API_SELECTION_ASYNC) {
        shell_print(shell, "Current mode: ASYNC");
    } else {
        shell_print(shell, "Current mode: SYNC");
    }

    return 0;
}

/* Command to set mode */
SHELL_CMD_REGISTER(kws_mode, NULL, "Set KWS mode: kws_mode <sync|async>", cmd_kws_mode);

/* Command to get current mode */
SHELL_CMD_REGISTER(kws_mode_get, NULL, "Get current KWS mode", cmd_kws_mode_get);

SHELL_CMD_REGISTER(app, NULL, "App Commands", cmd_app);

SHELL_CMD_REGISTER(full_erase, NULL, "Erase flash: erase <size>", cmd_full_erase);
SHELL_CMD_REGISTER(set, NULL, "Set MCU/AKD1500 as SPI-Master: set <bool> (0:AKD1500 1:MCU)",
                   cmd_set);
SHELL_CMD_REGISTER(spi_freq, NULL, "Set AKD1500 host SPI clock (Hz): spi_freq <hz>", cmd_spi_freq);
SHELL_CMD_REGISTER(infer, NULL, "Start the Inference: infer", cmd_infer);

/* --- AKD1500 high-freq SPI investigation commands (exploration) --- */
SHELL_CMD_REGISTER(akida_rd, NULL, "Read AKD reg: akida_rd <hexaddr>", cmd_akida_rd);
SHELL_CMD_REGISTER(akida_wr, NULL, "Write AKD reg: akida_wr <hexaddr> <hexval>", cmd_akida_wr);
SHELL_CMD_REGISTER(akd_clkinfo, NULL, "Dump AKD1500 SPI_S core-clock/PLL state (@1.4 MHz)",
                   cmd_akd_clkinfo);
SHELL_CMD_REGISTER(akd_pll_on, NULL, "Switch AKD1500 SPI_S core clock onto the 400 MHz PLL",
                   cmd_akd_pll_on);
SHELL_CMD_REGISTER(akd_coreclk, NULL, "Set AKD1500 core clock (Hz): akd_coreclk <hz>",
                   cmd_akd_coreclk);
SHELL_CMD_REGISTER(akd_pll, NULL, "Reprogram AKD1500 PLL output (Hz): akd_pll <600e6..800e6>",
                   cmd_akd_pll);
SHELL_CMD_REGISTER(akd_sysdiv, NULL, "Set AKD1500 core (SYS) divider: akd_sysdiv <n>",
                   cmd_akd_sysdiv);
SHELL_CMD_REGISTER(akd_clkref, NULL, "AKD1500 clock source: akd_clkref <0=PLL|1=25MHz ref>",
                   cmd_akd_clkref);
SHELL_CMD_REGISTER(akd_rdtest, NULL, "Read device-ID N times at current clock: akd_rdtest <count>",
                   cmd_akd_rdtest);
SHELL_CMD_REGISTER(akd_probe, NULL, "Probe reads: akd_probe <hz> <rxdelay 0-7> [count]",
                   cmd_akd_probe);
SHELL_CMD_REGISTER(spi_rxdelay, NULL, "Set SPIM4 rx-delay: spi_rxdelay <0-7>", cmd_spi_rxdelay);
#ifdef CONFIG_AKIDATAG_BOARD
SHELL_CMD_REGISTER(akd_sleep, NULL,
                   "AKD1500 wake reference: akd_sleep <0|1> (0 = take, 1 = "
                   "release; the chip sleeps only when no holder is left)",
                   cmd_akd_sleep);
#endif
