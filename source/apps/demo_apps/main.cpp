/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <errno.h>
#include <inttypes.h>
#include <soc.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include "akd_spi_flash.h"
#include "akd_spi_flash_handler.h"
#include "akida.h"
#include "akida/hardware_device.h"
#include "io_objects.h"
#include "nrf_spi.h"
#include "sample_input/kws/kws_inputs.h"
#include <akd1500/akd1500_spi_driver.h>
#include <cmath>
#include <hardware_device_impl.h>
#include <infra/system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "audio_processor.h"
#include "ble_services/ble_initialization.h"
#include "ble_services/edge_learning.h"
#include "ble_services/file_transfer.h"
#include "boot_manager.h"
#include "error.h"
#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
#include "imu_h/imu.h"
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
extern int g_min_inference_frames;
extern int speech_active_time_ms;

extern "C" {
int file_transfer_load_meta(int app_idx, model_meta_t *meta_out);
int infer(int app_index_l);
}

void cli_worker_proc_thread(void *a, void *b, void *c);
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
/** Macro to set the API to async mode */
#ifdef CONFIG_SYNC_MODE
#define DEFAULT_API_SELECTION_ASYNC 0
#else
#define DEFAULT_API_SELECTION_ASYNC 1
#endif
#ifdef CONFIG_INFERENCE_SAMPLE_THRESHOLD
#define DEFAULT_INFERENCE_SAMPLE_THRESHOLD CONFIG_INFERENCE_SAMPLE_THRESHOLD
#else
#define DEFAULT_INFERENCE_SAMPLE_THRESHOLD 3
#endif
static struct kws_demo_params {
  int energy_threshold;
  int bin0_threshold;
  int learning_delay;
  int sync_api;
  int infer_threshold;
} params = {DEFAULT_ENERGY_THRESHOLD, DEFAULT_BIN0_THRESHOLD,
            DEFAULT_LEARNING_DELAY, DEFAULT_API_SELECTION_ASYNC,
            DEFAULT_INFERENCE_SAMPLE_THRESHOLD};

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

#if IS_ENABLED(CONFIG_WDT_ENABLE)
/** Handle for the watchdog device  */
static const struct device *wdt;
static int wdt_channel_id;
#endif

/**
 *  Pointer to individual novel class learn weights data.
 */
uint8_t *learn_weights_buff_ptr;

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

static saved_learn_weights *saved_learn_weights_ptr = NULL;

static int32_t save_weights_from_mesh(uint8_t *lbl_wts_ptr, uint32_t size);

/**
 * This pointer points base label weights data.
 */
static uint8_t *base_labels_wts_ptr = NULL;

/** Spectrogram holding the required spectrogram for inference (prior to
  normalization) */
static float __aligned(4) spectrogram[SPECTROGRAM_COUNT][SPECTROGRAM_RES];
/** spectrogram dimensions */
static uint8_t __aligned(4) spectrogram_dims[2] = {SPECTROGRAM_COUNT,
                                                   SPECTROGRAM_RES};

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
  int32_t (*on_mfcc_output)(uint8_t *input, uint32_t *input_shape);

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
static int32_t inference_on_mfcc_output(uint8_t *input, uint32_t *input_shape);

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
static int32_t learning_on_mfcc_output(uint8_t *input, uint32_t *input_shape);

/**
 * @brief  Callback functions in Learning state
 * The state machine in Learning mode, changes the mode depending on passed
 * input. if the input is long press on B1, then changes mode to STATE_INFERENCE
 * else if the input is short press on B1 or B2, then changes mode to
 * STATE_LEARN_SELECT
 * @param input_type - type of user input identified
 */
static void learning_on_user_input(int input_type);

static void switch_learning_delayed(struct k_work *work);

static kws_edge_state_processor kws_edge_state[STATE_COUNT] = {
    [STATE_INFERENCE] = {inference_on_mfcc_output, inference_on_user_input},
    [STATE_LEARN_SELECT] = {NULL, learn_select_on_user_input},
    [STATE_LEARNING] = {learning_on_mfcc_output, learning_on_user_input},
};

static const uint32_t dims[] = {SPECTROGRAM_COUNT, SPECTROGRAM_RES, 1};

const unsigned char *inputs[] = {kws_inputs};

uint32_t g_num_classes = 0;
uint32_t g_num_neurons_per_class = 1;
uint32_t g_num_edge_learn_classes = 0;
uint32_t g_input_size = 0;
// int32_t akida_output[NUM_CLASSES * NUM_NEURONS_PER_CLASS] = {0};
int32_t *akida_output;
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

const struct device *wdt_dev; // Global watchdog device
void kick_watchdog(void) {
  if (wdt_dev) {
    // wdt_feed(wdt_dev, 0); // Feed the watchdog
    printk("Watchdog fed\n");
  }
}

/* helper function to swap the endianness */
uint32_t swap_endian(uint32_t value) {
  return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) |
         ((value << 8) & 0x00FF0000) | ((value << 24) & 0xFF000000);
}

// int spi_flash_erase_helper_func(uint32_t offset, uint32_t size);

extern "C" int64_t time_ms() { return k_uptime_get(); }

extern "C" void msleep(uint32_t duration) { k_sleep(K_MSEC(duration)); }

extern "C" void reset_spectrogram_index(void);

void panic(const char *format, ...) {
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
float mfcc_fs = 123.56967163085938f;

// KWS class definitions
#define KWS_SILENCE_CLASS 10
#define KWS_UNKNOWN_CLASS 11

// Sliding window score smoothing parameters
#define SCORE_WINDOW_SIZE 5
#define SCORE_THRESHOLD 0.6f
int score_window_size = SCORE_WINDOW_SIZE;
float score_threshold = SCORE_THRESHOLD;

// Metrics mode: show confidence and timing details on keyword detection
int metrics_on = 0;

static int check_model_compatibility(uint8_t is_el_model_l,
                                     model_meta_t kws_meta) {

  if (is_el_model_l) {
    if (kws_meta.is_edge_learned) {
      printk("\n\r model is edge learn capable\n\r");
    } else {
      printk("E: model in-compatability, FS and model to be updated "
             "correctly\n\r");
      return -1;
    }
  } else {
    if (kws_meta.is_edge_learned == 0) {
      printk("\n\r model is not edge learn capable\n\r");
    } else {
      printk("E: model in-compatability, FS and model to be updated "
             "correctly\n\r");
      return -1;
    }
  }
  return SUCCESS;
}

static void learn_to_ls(void) {
  if (last_learn_ts && STATE_LEARNING == cur_kws_edge_state) {
    uint64_t cur_ts = time_ms();
    switch_mode(STATE_LEARN_SELECT);
    printk("learning -> learn_select\n\r");
    akida_learn_mode(true);
    if (SUCCESS ==
        save_weights_from_mesh(
            learn_weights_buff_ptr,
            saved_learn_weights_ptr->learn_weights_data.learn_weights_size)) {
      saved_learn_weights_ptr->learn_weights_data.label_learnt_val |=
          1 << (cur_kws_edge_novel_class - KWS_EDGE_NOVEL_CLASS_BASE_ID);
      learning_completed();
      printk("Save Weights from MESH->MEM \n\r");
    } else {
      printk("Sync:akida_save_learn_weights function has failed for label %d ",
             cur_kws_edge_novel_class);
    }
    akida_learn_mode(false);
    uint32_t duration_ms = (uint32_t)(time_ms() - cur_ts);
    if (verbose_on) {
      printk("mesh_mem = % lu ms\n", duration_ms);
    }
  }
}

void do_inference(int spectrogram_index) {
  __aligned(32) static uint8_t akida_input[SPECTROGRAM_COUNT][SPECTROGRAM_RES];
  /*q7_t min = 127;
  q7_t max = -128;
  int32_t energy = 0;
  int32_t energies[SPECTROGRAM_COUNT + 1];

  // printk ("do_inference \n");

  for (int i = 0; i < SPECTROGRAM_COUNT; i++) {
    int idx = (i + spectrogram_index) % SPECTROGRAM_COUNT;
    energies[i] = 0;
    for (int j = 0; j < SPECTROGRAM_RES; j++) {
      if (spectrogram[idx][j] > max) {
        max = spectrogram[idx][j];
      }
      if (spectrogram[idx][j] < min) {
        min = spectrogram[idx][j];
      }
      if (j > 0)
        energies[i] += spectrogram[idx][j] > 0 ? spectrogram[idx][j]
                                               : -spectrogram[idx][j];
    }
    energy += energies[i];
  }*/

  /* It has been experimentally determined that when we talk on the mic,
   * the energy goes above 666, and the first bin of the MFCC spectrogram
   * is above -39. This allows not to send data to akida if we already
   * know that there is nothing to detect.
   */
  // printk ("min %d, max %d  energy %d, spectrogram_index %d\n" , min, max,
  // energy, spectrogram[(25 + spectrogram_index) % SPECTROGRAM_COUNT][0]);
  /*if ((energy > params.energy_threshold) &&
      (spectrogram[(25 + spectrogram_index) % SPECTROGRAM_COUNT][0] >
       params.bin0_threshold))*/

  // printk ("min %d, max %d  energy %d, spectrogram_index %d\n" , min, max,
  // energy, spectrogram_index);
  if (kws_edge_state[cur_kws_edge_state].on_mfcc_output) {
    /* Generate model input where whole spectrogram is normalized
       between 0 and 255 */
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

    // memcpy ((uint8_t*) akida_input, &kws_inputs[0], 490);
    // printk ("do_inference cur_kws_edge_state %d \n", cur_kws_edge_state);
    kws_edge_state[cur_kws_edge_state].on_mfcc_output((uint8_t *)akida_input,
                                                      (uint32_t *)dims);
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

const struct device *uart;

void uart_init(void) {
  uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
  if (!device_is_ready(uart)) {
    printk("UART not ready\n");
  } else {
    printk("UART is ready\n");
  }
}

static int start_dmic_audio_proc(void) {
  dmic_init();
  audio_processor_init(SAMPLING_RATE);

  int is_audio_started =
      audio_processor_start(false, (float *)spectrogram, spectrogram_dims,
                            MFCC_SAMPLE_COUNT, do_inference);
  if (is_audio_started == EFAILURE) {
    printk("audio_processor not started\n");
    return EFAILURE;
  }

  capture_tid = k_thread_create(&capture_thread, capture_stack,
                                CAPTURE_STACK_SIZE, dmic_capture_thread, NULL,
                                NULL, NULL, CAPTURE_PRIORITY, K_USER,
                                K_FOREVER // START SUSPENDED
  );

  process_tid = k_thread_create(&process_thread, process_stack,
                                PROCESS_STACK_SIZE, audio_process_thread, NULL,
                                NULL, NULL, PROCESS_PRIORITY, K_USER,
                                K_FOREVER // START SUSPENDED
  );

  k_thread_start(capture_tid);
  k_thread_start(process_tid);

  return 0;
}
#if IS_ENABLED(CONFIG_CAMERA_ENABLE_THREAD)
static int initialize_spi_camera_interface(void) {

  camera_thread_id = k_thread_create(&camera_thread, camera_stack,
                                     CAMERA_STACK_SIZE, camera_capture_thread,
                                     NULL, NULL, NULL, CAMERA_PRIORITY, K_USER,
                                     K_FOREVER // START SUSPENDED
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
  saved_learn_weights_ptr->total_saved_learn_weights_size =
      sizeof(saved_learn_weights);

  /* store the last layer size information */
  saved_learn_weights_ptr->learn_weights_data.learn_weights_size =
      layer_mem_size;

  /* initialize learn_weights_buff_ptr to the next location after the
   * structure saved_learn_weights size */
  learn_weights_buff_ptr =
      (uint8_t *)((uint8_t *)saved_learn_weights_ptr +
                  saved_learn_weights_ptr->total_saved_learn_weights_size);

  saved_learn_weights_ptr->learn_weights_data.label_learnt_val = 0;
  for (int i = 0;
       i < saved_learn_weights_ptr->learn_weights_data.learn_weights_size;
       i++) {
    learn_weights_buff_ptr[i] = 0;
  }
  saved_learn_weights_ptr->total_saved_learn_weights_size +=
      saved_learn_weights_ptr->learn_weights_data.learn_weights_size;
}

static struct k_work_delayable switch_delayed_work;
static void switch_learning_delayed(struct k_work *work) {
  ARG_UNUSED(work);

  if (cur_kws_edge_state == STATE_LEARN_SELECT) {
    cur_kws_edge_state = STATE_LEARNING;
    printk("learn_select -> learning");
    last_learn_ts = time_ms();
    k_work_reschedule(&switch_delayed_work, K_SECONDS(5));
  } else if (cur_kws_edge_state == STATE_LEARNING) {
    learn_to_ls();
  }
}

static void switch_mode(int mode) {
  switch (cur_kws_edge_state) {
  case STATE_INFERENCE:
    if (STATE_LEARN_SELECT == mode) {

      akida_learn_mode(true);

      cur_kws_edge_state = mode;
    }
    break;
  case STATE_LEARN_SELECT:
    if (STATE_INFERENCE == mode) {
      k_work_cancel_delayable(&switch_delayed_work);
      akida_learn_mode(false);

      cur_kws_edge_state = mode;
    } else if (STATE_LEARNING == mode) {

      akida_learn_mode(true);
      k_work_reschedule(&switch_delayed_work, K_SECONDS(1));
    }
    break;
  case STATE_LEARNING:
    k_work_cancel_delayable(&switch_delayed_work);
    akida_learn_mode(false);
    // printk(" STATE_LEARNING mode %d, cur_kws_edge_state %d \n\r", mode,
    // cur_kws_edge_state);
    if (STATE_LEARN_SELECT == mode) {
      cur_kws_edge_state = mode;
    } else if (STATE_INFERENCE == mode) {

      akida_learn_mode(false);

      cur_kws_edge_state = mode;
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
      akida_update_learn_weights(
          (uint32_t *)learn_weights_buff_ptr,
          saved_learn_weights_ptr->learn_weights_data.learn_weights_size);
  if (saved_learn_weights_ptr->learn_weights_data.learn_weights_size !=
      (int32_t)akida_learn_mem_size()) {
    printk(" there is an issue for the learned class, as weights are not "
           "stored properly "
           "in Akida Neuron Fabric");
    /* as there is an error, initialize the memory again */
    init_learn_weights_mem(mesh_learn_weights_size);
  }
}

static void read_learn_weights_from_flash(void) {

  struct fs_file_t file;
  fs_file_t_init(&file);

  int ret = fs_open(&file, LEARN_WEIGHTS_FILE_NAME, FS_O_READ);

  if (ret == 0) {
    ret = fs_read(&file, (uint8_t *)saved_learn_weights_ptr,
                  saved_learn_weights_ptr->total_saved_learn_weights_size);

    fs_close(&file);

    /* if same number of bytes are read from flash, then check CRC else user to
     * do re-learning*/
    if (ret == (int)saved_learn_weights_ptr->total_saved_learn_weights_size) {
      uint32_t crc32 = crc32_ieee(
          (uint8_t *)saved_learn_weights_ptr + 4,
          (saved_learn_weights_ptr->total_saved_learn_weights_size - 4));
      /* if CRC is failed then user to do re-learning*/
      if (crc32 != saved_learn_weights_ptr->crc) {
        printk("CRC check failed, %d bytes read from flash and there is an "
               "error in reading "
               "learning data, user need to perform learning again \r\n",
               ret);
        reset_saved_weights();
      } else {
        printk("%d bytes are read from flash (learn weights) to "
               "saved_learn_weights_ptr "
               "location \r\n",
               ret);
        if (saved_learn_weights_ptr->learn_weights_data.label_learnt_val) {
          update_weights_to_mesh();
        }
      }
    } else {
      printk("incorrect number of bytes read from flash, user need to re-learn "
             "\r\n");
      reset_saved_weights();
    }
  } else {
    printk("read_learn_weights_from_flash: file open failed \n");
  }
}

static int initiate_kws_inference(uint8_t is_el_model_l) {

  if (is_el_model_l) {
    k_work_init_delayable(&switch_delayed_work, switch_learning_delayed);
    mesh_learn_weights_size = akida_learn_mem_size();

    printk("mesh_learn_weights_size = %" PRIu32 "\n", mesh_learn_weights_size);

    // Free any prior allocation to avoid memory leak on re-init
    if (saved_learn_weights_ptr) {
      delete[] reinterpret_cast<uint8_t *>(saved_learn_weights_ptr);
      saved_learn_weights_ptr = NULL;
      learn_weights_buff_ptr = NULL;
    }
    if (base_labels_wts_ptr) {
      delete[] base_labels_wts_ptr;
      base_labels_wts_ptr = NULL;
    }

    // allocating memory for structure (this will hold crc, size etc ) + learn
    // weights data together to place them in contiguous locations
    saved_learn_weights_ptr = reinterpret_cast<saved_learn_weights *>(
        new uint8_t[sizeof(saved_learn_weights) + mesh_learn_weights_size]);

    base_labels_wts_ptr = new uint8_t[mesh_learn_weights_size];

    if ((saved_learn_weights_ptr == NULL) || (base_labels_wts_ptr == NULL)) {
      printk("dynamic memory allocation failed for weights data and hence "
             "application is not "
             "running ");
      return -EFAILURE;
    }
    // initialize the learn_weights_mem structure
    reset_saved_weights();

    read_learn_weights_from_flash();
  }

  cur_kws_edge_state = STATE_INFERENCE;
  /* adding additional 1200ms to last_trigger_time_ms to increase the debouce
   * time at during the initialization to suppress any noise from dmic */
  last_trigger_time_ms = time_ms() + 1200ULL;
  start_dmic_audio_proc();
  return SUCCESS;
}
#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
static int start_imu_proc(void) {
  imu_tid =
      k_thread_create(&imu_thread, imu_stack, IMU_STACK_SIZE, imu_data_thread,
                      NULL, NULL, NULL, IMU_PRIORITY, K_USER,
                      K_FOREVER // START SUSPENDED
      );

  k_thread_start(imu_tid);
  return 0;
}
#endif
void check_reset_reason(void) {
  uint32_t cause = 0;

  if (hwinfo_get_reset_cause(&cause) != 0) {
    printk("Failed to read reset cause\n");
    return;
  }

  printk("Reset cause: 0x%08x\n", cause);

  if (cause & RESET_LOW_POWER_WAKE) {
    printk("Wakeup from System OFF\n");
  }
  if (cause & RESET_PIN) {
    printk("Reset from RESET pin\n");
  }
  if (cause & RESET_WATCHDOG) {
    printk("Reset from Watchdog\n");
  }
  if (cause & RESET_SOFTWARE) {
    printk("Reset from software reset\n");
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
  led_tid =
      k_thread_create(&led_thread, led_stack, LED_STACK_SIZE, led_ind_thread,
                      NULL, NULL, NULL, LED_PRIORITY, K_USER,
                      K_FOREVER // START SUSPENDED
      );

  k_thread_start(led_tid);
  return 0;
}

static void update_model_params(model_meta_t kws_meta) {
  g_input_size = kws_meta.input_shape[0] * kws_meta.input_shape[1] *
                 kws_meta.input_shape[2];
  g_num_classes = kws_meta.output_shape[0] * kws_meta.output_shape[1] *
                  kws_meta.output_shape[2];

  printk("kws_meta.num_edge_classes %x \n\r", kws_meta.num_edge_classes);
  g_num_neurons_per_class = (kws_meta.num_edge_classes & 0xFFFF0000) >> 16;
  if (g_num_neurons_per_class == 0) {
    g_num_neurons_per_class = 1;
  }

  g_num_classes = g_num_classes / g_num_neurons_per_class;
  g_num_edge_learn_classes = (kws_meta.num_edge_classes & 0xFFFF);

  printk("kws_meta.input_shape[0] %d, kws_meta.input_shape[1] %d, "
         "kws_meta.input_shape[2] %d\n\r",
         kws_meta.input_shape[0], kws_meta.input_shape[1],
         kws_meta.input_shape[2]);

  printk("kws_meta.output_shape[0] %d, kws_meta.output_shape[1] %d, "
         "kws_meta.output_shape[2] %d\n\r",
         kws_meta.output_shape[0], kws_meta.output_shape[1],
         kws_meta.output_shape[2]);

  printk("g_input_size %d, g_num_classes %d, g_num_neurons_per_class %d, "
         "g_num_edge_learn_classes %d \n\r",
         g_input_size, g_num_classes, g_num_neurons_per_class,
         g_num_edge_learn_classes);

  delete[] akida_output;
  akida_output = new int32_t[g_num_classes * g_num_neurons_per_class];
  akd_op_size = sizeof(int32_t) * g_num_classes * g_num_neurons_per_class;
}

int main(void) {

  check_reset_reason();
  /* printk("App Core Version: %s\n", CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION); */
  /* Image IDs defined by MCUboot */
  print_image_version(FLASH_AREA_ID(image_0), "App Core");

  /*print_image_version(FLASH_AREA_ID(image_1), "Net Core");*/

  uart_init();
  start_led_ind();
  led_set_state(LED_STATE_NORMAL_APP);
  printk("Akida TAG Application\n");
  confirm_image_if_needed();
  init_setting_sub_system();
  shared_buf_init();
  file_transfer_init();
  ble_init();
  akida_spiflash_init();

  /* Get the SPI NOR flash device defined in the device tree (node label:
   * ext_flash) and verify that the driver has initialized successfully before
   * using it.
   */
  const struct device *spi_flash = DEVICE_DT_GET(DT_NODELABEL(ext_flash));

  if (!device_is_ready(spi_flash)) {
    printk("SPI flash not ready\n");
  } else {
    printk("SPI flash device ready: %s\n", spi_flash->name);
  }
  int err = storage_init();
  if (err != 0) {
    printk("LittleFS mount failed %d", err);
  } else {
    printk("LittleFS mount succeeded %d", err);
  }

  init_boot_count();

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
    printk("E: Metadata header unavailable (err %d)\n", hdr_ret);
    return -1;
  }
  uint32_t kws_flash_addr = kws_meta.flash_address;

  /* Step 3: validate model name from the header (model_meta_t.model_name) */
  if (file_transfer_check_model_name(0, kws_meta.model_name) != 0) {
    printk("E: Model name mismatch: stored='%s', expected for slot 1='kws'\n",
           kws_meta.model_name);
    kws_model_present = false;
    return -1;
  }
  printk("Model name: stored='%s', \n", kws_meta.model_name);
  /* Step 3: Copy to model_name */
  /* Step 2&4: load data meta and validate flash contents */
  int dm_ret = file_transfer_load_data_meta(0, &kws_data_meta);
  if (dm_ret == 0) {
    /* Step 4: full SPI flash CRC validation */
    akida_config_spi(1);
    int val_ret =
        file_transfer_validate_flash_data(kws_flash_addr, &kws_data_meta);
    akida_config_spi(0);
    if (val_ret != 0) {
      printk("E: Model data validation FAILED will not program Akida\n");
      kws_model_present = false;
      return -1;
    }
  } else {
    printk("E: model_data file is not present and returning\n");
    return -1;
  }

  /* Step 5: reload full meta + program_info into sram_upload_buffer.
   * This is necessary because file_transfer_validate_flash_data() may have
   * overwritten sram_upload_buffer during the CRC read loop. */
  int meta_ret = file_transfer_load_meta(0, &kws_meta);
  if (meta_ret != 0) {
    printk("E: Metadata reload failed (err %d)\n", meta_ret);
    return -1;
  }

  /* Step 6: program Akida */
  printk("Model data found at 0x%08X\n", kws_flash_addr);
  akd_device.toggle_clock_counter(true);
  printk("Programming model info into AKD1500\n");

  uint32_t s_dma_cycls = akd_device.read_clock_counter();
  uint64_t start_time = time_ms();

  akida_program_flash(sram_upload_buffer, (int)kws_meta.info_data_len,
                      kws_meta.flash_address, &is_el_model);

  uint32_t prog_time = (uint32_t)(time_ms() - start_time);
  uint32_t delta_cycle = akd_device.read_clock_counter() - s_dma_cycls;
  uint32_t dma_time = delta_cycle / AKIDA_FREQUENCY_MHZ;
  printk("\nModel program: %u dma cycles, %u us dma, %u ms cpu\n", delta_cycle,
         dma_time, prog_time);

  akd_device.set_batch_size(1, true);
  kws_model_present = true;
  update_model_params(kws_meta);

  if (check_model_compatibility(is_el_model, kws_meta) != SUCCESS) {
    return -1;
  }

  initiate_kws_inference(is_el_model);
  is_kws_inference_started = true;

  printk("data to check : model_size %d, class %d \n",
         kws_meta.info_data_len + kws_data_meta.data_length, g_num_classes);
#if IS_ENABLED(CONFIG_IMU_ENABLE_THREAD)
  start_imu_proc();
#endif

#if IS_ENABLED(CONFIG_WDT_ENABLE)
  watchdog_init(&wdt, &wdt_channel_id);
#endif

#if IS_ENABLED(CONFIG_CAMERA_ENABLE_THREAD)
  initialize_spi_camera_interface();
#endif
  // ... inside a function like main() or a separate initialization function
  printk("Current CPU frequency: %u MHz\n", SystemCoreClock / 1000000);
  // You can also inspect the NRF_CLOCK_S->HFCLKCTRL register value
  printk("NRF_CLOCK_S->HFCLKCTRL: %d\n", NRF_CLOCK_S->HFCLKCTRL);

  cli_worker_tid = k_thread_create(
      &cli_worker_thread, cli_worker_stack, CONFIG_SHELL_STACK_SIZE,
      cli_worker_proc_thread, NULL, NULL, NULL, CLI_WORKER_PRIORITY, K_USER,
      K_FOREVER // START SUSPENDED
  );
  k_thread_start(cli_worker_tid);

  return 0;
}

void cli_worker_proc_thread(void *a, void *b, void *c) {
  printk("CLI Worker: \n\r");

  while (1) {
#if IS_ENABLED(CONFIG_WDT_ENABLE)
    if (all_threads_healthy()) {
      wdt_feed(wdt, wdt_channel_id);
    }
#endif
    prcess_led();
  }
}

#define DEBOUNCE_COOLDOWN_MS                                                   \
  300 // Cooldown period after a trigger (changed from 1000ms)

uint32_t kws_debounce_time = DEBOUNCE_COOLDOWN_MS;
bool feature_buff_full = false;

extern "C" void reset_stale_inference_data(void) {
  reset_kws_spectrogram();
  if (verbose_on) {
    printk("clearing the stale inference data\n\r");
  }
  return;
}

extern "C" uint8_t is_kws_debounce_complete(void) {
  uint8_t is_debounce = 0;
  /* DEBOUNCING (Preventing multiple rapid triggers)*/
  if ((time_ms()) > last_trigger_time_ms + kws_debounce_time)
    is_debounce = 1;

  return is_debounce;
}

extern "C" void set_feature_buff_full(void) { feature_buff_full = 1; }

extern "C" uint8_t is_feature_buff_full(void) { return feature_buff_full; }

static void reset_kws_spectrogram(void) {
  memset(spectrogram, 0, sizeof(spectrogram));
  // feature_buff_full = false;
  reset_spectrogram_index();
}
static int32_t inference_on_mfcc_output(uint8_t *input, uint32_t *input_shape) {
  int ret = 0;
  static int class_history[SCORE_WINDOW_SIZE] = {-1, -1, -1, -1, -1};
  static int history_idx = 0;

  if (params.sync_api == 0) {

    uint64_t start_time = time_ms();
    uint32_t s_dma_cycls = akd_device.read_clock_counter();

    if (SUCCESS == akida_forward(input, input_shape, (uint8_t *)akida_output,
                                 akd_op_size)) {
      uint32_t inf_time = time_ms() - start_time;
      uint32_t delta_cycle = akd_device.read_clock_counter() - s_dma_cycls;
      uint32_t dma_time = delta_cycle / AKIDA_FREQUENCY_MHZ;
      if (verbose_on) {
        printk("\n\rinference time= %u dma cycles, dma time = %d us, cpu time "
               "= %u ms\n\r",
               delta_cycle, dma_time, inf_time);
      }

      int32_t max_val = 0;
      int32_t akida_output_l[g_num_classes] = {0};
      for (int num_cls = 0; num_cls < g_num_classes; num_cls++) {
        max_val = 0;
        for (int i = 0; i < g_num_neurons_per_class; i++) {
          /* identify the biggest value within the g_num_neurons_per_class and
          use the max value for that class */
          int32_t current_val =
              akida_output[(num_cls * g_num_neurons_per_class) + i];
          if (current_val > max_val) {
            max_val = current_val;
          }
        }

        /* copy the max value per class */
        akida_output_l[num_cls] = max_val;
      }

      int found = get_inferred_class(akida_output_l, g_num_classes, 1);

      if (found == -1) {
        if (verbose_on) {
          printk("no class detected (zero activations)\n\r");
        }
        return 0;
      }

      if (found == KWS_SILENCE_CLASS || found == KWS_UNKNOWN_CLASS) {
        if (verbose_on) {
          printk("suppressed: %s\n\r",
                 (found < kws_new_tags_count) ? kws_new_tags[found] : "?");
        }
        return 0;
      }

      // Sliding window score smoothing
      class_history[history_idx % score_window_size] = found;
      history_idx++;

      // Compute smoothing score for this class
      int match_count = 0;
      for (int i = 0; i < score_window_size; i++) {
        if (class_history[i] == found)
          match_count++;
      }
      float score = (float)match_count / score_window_size;

      if (verbose_on) {
        printk("class=%d (%s) score=%.2f\n\r", found,
               (found < kws_new_tags_count) ? kws_new_tags[found] : "?", score);
      }

      if (score >= score_threshold) {
        current_class = found;
        // Compute softmax confidence over class-level spike sums

        float class_sums[g_num_classes] = {0};

        for (int c = 0; c < g_num_classes; c++) {
          for (int n = 0; n < g_num_neurons_per_class; n++) {
            class_sums[c] +=
                (float)akida_output[c * g_num_neurons_per_class + n];
          }
        }
        float max_sum = class_sums[0];
        for (int c = 1; c < g_num_classes; c++) {
          if (class_sums[c] > max_sum) {
            max_sum = class_sums[c];
          }
        }
        float exp_sum = 0.0f;
        for (int c = 0; c < g_num_classes; c++) {
          exp_sum += expf(class_sums[c] - max_sum);
        }
        float confidence = (exp_sum > 0.0f)
                               ? expf(class_sums[found] - max_sum) / exp_sum
                               : 0.0f;

        printk("\nKeyword Detected: %s\n\r",
               (found < kws_new_tags_count) ? kws_new_tags[found] : "?");
        if (metrics_on) {
          printk("  confidence=%.1f%% vote=%.2f cpu=%ums dma=%uus\n\r",
                 confidence * 100.0f, score, inf_time, dma_time);
        }
        if (is_ble_connected()) {
          send_event(CMD_DEPLOY_START, kws_new_tags[found],
                     confidence * 100.0f);
        }

        // Clear history so next word starts fresh
        memset(class_history, -1, sizeof(class_history));
        history_idx = 0;
        reset_kws_spectrogram();
        last_trigger_time_ms = time_ms();
      }
    } else {
      printk("akida_forward failure\n");
    }
  }
  return ret;
}

static void inference_on_user_input(int input_type) {
  switch (input_type) {
  case USER_INPUT_LP(0):
    switch_mode(STATE_LEARN_SELECT);
    printk("inference -> learn_select");
    break;
  default:
    printk(" wrong input");
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
      crc32_ieee((uint8_t *)saved_learn_weights_ptr + 4,
                 (saved_learn_weights_ptr->total_saved_learn_weights_size - 4));
  if (verbose_on) {
    printk(" the computed CRC = %x", saved_learn_weights_ptr->crc);
  }
  /* save the learned weights into flash */

  struct fs_file_t file;
  fs_file_t_init(&file);

  int rc = fs_open(&file, LEARN_WEIGHTS_FILE_NAME, FS_O_CREATE | FS_O_WRITE);
  if (rc == 0) {
    fs_write(&file, (uint8_t *)saved_learn_weights_ptr,
             saved_learn_weights_ptr->total_saved_learn_weights_size);
    fs_close(&file);

    printk("%d learned weight bytes are programmed to flash at "
           "LEARN_WEIGHTS_FILE_NAME",
           saved_learn_weights_ptr->total_saved_learn_weights_size);
  } else {
    printk("save_weights_to_flash: fail open failed \n");
  }
  uint32_t flash_ts = (uint32_t)(k_cycle_get_32() - cur_ts);
  uint64_t duration_us = k_cyc_to_us_floor64(flash_ts);
  if (verbose_on) {
    printk("Duration = %" PRIu64 " us\n", duration_us);
  }
}

/**
 * @brief Resets the learn weights memory
 *
 * This function update the saved_learn_weights with base_labels_wts_ptr and
 * corrupts the learn weights file in flash
 * @param lbl_wts - holds the base class weights
 */

static void reset_learned_weights(uint8_t *lbl_wts) {
  /* reset learn_weights_size and label_learnt_val and CRC to 0 */
  /* once the contents are reset, then stay in the same state, so that user can
   * do the learning */
  saved_learn_weights_ptr->learn_weights_data.label_learnt_val = 0;
  for (int i = 0;
       i < saved_learn_weights_ptr->learn_weights_data.learn_weights_size;
       i++) {
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
    fs_write(&file, (uint8_t *)saved_learn_weights_ptr, 4);
    fs_close(&file);
  }
  /* reset weights in model also */
  update_weights_to_mesh();
  printk("learn weights content in Flash have been reset");
}

static void learn_select_on_user_input(int input_type) {
  printk("learn_select_on_user_input\n");
  switch (input_type) {
  case USER_INPUT_LP(0):
    /*  change the state to inference */
    switch_mode(STATE_INFERENCE);
    printk("learn_select -> inference");
    /* save the weights to flash only when labels are learnt and state chages to
     * STATE_INFERENCE */
    if (saved_learn_weights_ptr->learn_weights_data.label_learnt_val) {
      printk("Weights saved from MEM->FLASH");
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
    printk("class ID selected is %d\n", cur_kws_edge_novel_class);
    break;
  case USER_INPUT_LP(1):
    reset_learned_weights(base_labels_wts_ptr);
    break;
  default:
    printk("input_type default\n");
    break;
  }
}

/**
 * @brief Function to save learned weights from mesh into location pointed by
 * passed argument.
 * @param lbl_wts_ptr  - Weights to be saved.
 * @param size - number of bytes to be saved.
 */
static int32_t save_weights_from_mesh(uint8_t *lbl_wts_ptr, uint32_t size) {
  int32_t ret_val = -EFAILURE;
  /* save the learned weights from last layer and update them after programming
   * the model again */
  saved_learn_weights_ptr->learn_weights_data.learn_weights_size =
      akida_save_learn_weights((uint32_t *)lbl_wts_ptr, size);

  if (saved_learn_weights_ptr->learn_weights_data.learn_weights_size ==
      (int32_t)akida_learn_mem_size()) {
    ret_val = SUCCESS;
  }
  return ret_val;
}

static int32_t learning_on_mfcc_output(uint8_t *input, uint32_t *input_shape) {

  int32_t label_id = cur_kws_edge_novel_class;

  int ret = 0;
  if (params.sync_api == 0) {
    akida_fit(input, input_shape, &label_id);
    printk("Sync:learning done for class@ %d", cur_kws_edge_novel_class);

  } else {

    do {
      ret = akida_enqueue(input, input_shape, &label_id);
    } while (ret);
  }

  last_learn_ts = time_ms();
  return ret;
}

static void learning_on_user_input(int input_type) {
  uint32_t cur_ts;
  uint32_t mesh_mem;
  uint64_t duration_us;
  switch (input_type) {
  case USER_INPUT_LP(0):
    switch_mode(STATE_INFERENCE);
    printk("learning->inference");
    akida_learn_mode(true);

    /* save the weights to flash only when labels are learnt and state changes
     * to STATE_INFERENCE */
    if (SUCCESS ==
        save_weights_from_mesh(
            learn_weights_buff_ptr,
            saved_learn_weights_ptr->learn_weights_data.learn_weights_size)) {
      saved_learn_weights_ptr->learn_weights_data.label_learnt_val |=
          1 << (cur_kws_edge_novel_class - KWS_EDGE_NOVEL_CLASS_BASE_ID);
      save_weights_to_flash();
      printk("Save Weights from MEM->FLASH");
    } else {
      printk("Sync:akida_save_learn_weights function has failed for label %d ",
             cur_kws_edge_novel_class);
    }

    akida_learn_mode(false);

    break;
  case USER_INPUT_SP(0):
  case USER_INPUT_SP(1):
    cur_ts = k_cycle_get_32();

    switch_mode(STATE_LEARN_SELECT);
    printk("learning-> learnselect");

    akida_learn_mode(true);

    if (SUCCESS ==
        save_weights_from_mesh(
            learn_weights_buff_ptr,
            saved_learn_weights_ptr->learn_weights_data.learn_weights_size)) {
      saved_learn_weights_ptr->learn_weights_data.label_learnt_val |=
          1 << (cur_kws_edge_novel_class - KWS_EDGE_NOVEL_CLASS_BASE_ID);
      printk("Save Weights from MESH->MEM\n\r");
    } else {
      printk("Sync:akida_save_learn_weights function has failed for label %d ",
             cur_kws_edge_novel_class);
    }

    akida_learn_mode(false);
    mesh_mem = (k_cycle_get_32() - cur_ts);
    duration_us = k_cyc_to_us_floor64(mesh_mem);
    printk("mesh_mem = %" PRIu64 " us\n", duration_us);

    break;
  default:
    break;
  }
}

/* function to run the inference */
extern "C" int infer(int app_index_l) {
  if (app_index_l > 0) {
    printk("Illegal model index %d\n", app_index_l);
    return -1;
  }

  /* Step 1: read header only to get flash_address without touching
   * sram_upload_buffer */
  model_meta_t infer_meta;
  int hdr_ret = file_transfer_read_meta_hdr_only(app_index_l, &infer_meta);
  if (hdr_ret != 0) {
    printk("E: Metadata header unavailable (err %d)\n", hdr_ret);
    return -1;
  }
  uint32_t use_flash_addr = infer_meta.flash_address;

  /* Step 3: validate model name from the header (model_meta_t.model_name) */
  if (file_transfer_check_model_name(app_index_l, infer_meta.model_name) != 0) {
    printk("E: Model name mismatch for slot %d: '%s'\n", app_index_l,
           infer_meta.model_name);
    return -1;
  }

  /* Step 2&4: load data meta and validate flash contents */
  model_data_meta_t infer_data_meta;
  int dm_ret = file_transfer_load_data_meta(app_index_l, &infer_data_meta);
  if (dm_ret == 0) {
    /* Step 4: full SPI flash CRC validation */
    akida_config_spi(1);
    int val_ret =
        file_transfer_validate_flash_data(use_flash_addr, &infer_data_meta);
    akida_config_spi(0);
    if (val_ret != 0) {
      printk("E: Flash data validation FAILED for slot %d\n", app_index_l);
      return -1;
    }
  } else {
    /* Legacy fallback: 4-byte check only */
    printk("E: No data meta file (err %d) \n", dm_ret);
    return -1;
  }

  /* Step 5: reload full meta + program_info into sram_upload_buffer.
   * file_transfer_validate_flash_data() may have overwritten it. */
  int meta_ret = file_transfer_load_meta(app_index_l, &infer_meta);
  if (meta_ret != 0) {
    printk("Metadata reload failed (err %d)\n", meta_ret);
    return -1;
  }
  update_model_params(infer_meta);

  /* Step 6: program Akida */
  akida_program_flash(sram_upload_buffer, (int)infer_meta.info_data_len,
                      infer_meta.flash_address, &is_el_model);

  akd_device.set_batch_size(1, true);
  app_index = app_index_l;

  if (check_model_compatibility(is_el_model, infer_meta) != SUCCESS) {
    return -1;
  }

  akd_device.toggle_clock_counter(true);

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
  s_dma_cycls = akd_device.read_clock_counter();
  s_tick = time_ms();
  int ret = akida_forward((uint8_t *)inputs[app_index_l], inp_shap,
                          (uint8_t *)akida_output, akd_op_size);
  e_tick = time_ms();
  e_dma_cycls = akd_device.read_clock_counter();
  delta_cycle = e_dma_cycls - s_dma_cycls;
  inf_time = e_tick - s_tick;
  printk("\n\rinference time= %u dma cycles, time = %u ms\n\r", delta_cycle,
         inf_time);
  if (ret == SUCCESS) {
    class_id =
        get_inferred_class(akida_output, num_classes, num_neurons_per_class);
  } else {
    printk("\n\r inference failed \n\r");
    return -1;
  }
  if (app_index_l == 0) { // kws
    printk("\nClass : %d\n", class_id);
    printk("Word : %s\n", (class_id >= 0 && class_id < kws_new_tags_count)
                              ? kws_new_tags[class_id]
                              : "?");
    kws_model_present = true;
    if (!is_kws_inference_started) {
      initiate_kws_inference(is_el_model);
    } else if (kws_threads_suspended) {
      k_thread_resume(capture_tid);
      k_thread_resume(process_tid);
      kws_model_present = false;
    }
  }

  printk("APP Inference Completed\n");

  return 0;
}

/* shell cli function to invoke infer function */
static int cmd_infer(const struct shell *shell, size_t argc, char **argv) {
  if (argc != 2) {
    printk("Usage: infer <string>");
    return -EINVAL;
  }

  char *string = argv[1];
  if (!strcmp(string, "kws")) {
    app_index = 0;
    printk("inference kws requested, app index %d", app_index);
  } else {
    printk("Illegal model inference request");
    return -EINVAL;
  }

  return infer(app_index);
}

/* shell cli function to set external host MCU/AKD1500 as SPI master */
static int cmd_set(const struct shell *shell, size_t argc, char **argv) {
  if (argc != 2) {
    printk("Usage: set <bool>");
    return -EINVAL;
  }
  size_t value = strtoul(argv[1], NULL, 0);

  printk("Value = %d", value);
  if (value != 0 && value != 1) {
    printk("Invalid <bool> value %d", value);
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

/* shell cli function to invoke erase function */
static int cmd_full_erase(const struct shell *shell, size_t argc, char **argv) {
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
static int cmd_kws_el(const struct shell *shell, size_t argc, char **argv) {
  printk("cmd exec argc %d\n", argc);
  if (argc > 1) {
    if (argc > 2 && !strcmp(argv[1], "verbose")) {
      verbose_on = atoi(argv[2]);
      printk("verbose_on = %d\n\r", verbose_on);
    } else if (!strcmp(argv[1], "stop")) {
      cur_kws_edge_state = STATE_STOPPED;
      audio_processor_stop();
    } else if (!strcmp(argv[1], "evt")) {
      if (argc > 2) {
        printk(" cur_kws_edge_state %d\n", cur_kws_edge_state);
        if (is_el_model == 0) {
          printk(" illegal request, this is not an edge learning model\n");
          return 0;
        }

        if (cur_kws_edge_state == STATE_STOPPED) {
          printk(
              " cur_kws_edge_state is STATE_STOPPED user input not possible\n");
          return 0;
        }
        kws_edge_state[cur_kws_edge_state].on_user_input(atoi(argv[2]));
      }
    } else if (argc > 2 && !strcmp(argv[1], "rms")) {
      rms_threshold = atoi(argv[2]);
      printk("rms_threshold = %d\n\r", rms_threshold);
    } else if (argc > 2 && !strcmp(argv[1], "debounce")) {
      kws_debounce_time = atoi(argv[2]);
      printk("kws_debounce_time = %u ms\n\r", kws_debounce_time);
    } else if (argc > 2 && !strcmp(argv[1], "window")) {
      score_window_size = atoi(argv[2]);
      if (score_window_size < 1)
        score_window_size = 1;
      if (score_window_size > SCORE_WINDOW_SIZE)
        score_window_size = SCORE_WINDOW_SIZE;
      printk("score_window_size = %d\n\r", score_window_size);
    } else if (argc > 2 && !strcmp(argv[1], "score")) {
      score_threshold = atof(argv[2]);
      if (score_threshold < 0.0f)
        score_threshold = 0.0f;
      if (score_threshold > 1.0f)
        score_threshold = 1.0f;
      printk("score_threshold = %.2f\n\r", score_threshold);
    } else if (argc > 2 && !strcmp(argv[1], "min_frames")) {
      g_min_inference_frames = atoi(argv[2]);
      if (g_min_inference_frames < 1)
        g_min_inference_frames = 1;
      printk("g_min_inference_frames = %d\n\r", g_min_inference_frames);
    } else if (argc > 2 && !strcmp(argv[1], "speech")) {
      speech_active_time_ms = atoi(argv[2]);
      printk("speech_active_time_ms = %d ms\n\r", speech_active_time_ms);
    } else if (argc > 2 && !strcmp(argv[1], "metrics")) {
      metrics_on = atoi(argv[2]);
      printk("metrics_on = %d\n\r", metrics_on);
    } else if (!strcmp(argv[1], "show")) {
      printk("\n\r=== KWS Parameters ===\n\r");
      printk("verbose_on = %d\n\r", verbose_on);
      printk("rms_threshold = %d\n\r", rms_threshold);
      printk("kws_debounce_time = %u ms\n\r", kws_debounce_time);
      printk("score_window_size = %d\n\r", score_window_size);
      printk("score_threshold = %.2f\n\r", score_threshold);
      printk("g_min_inference_frames = %d\n\r", g_min_inference_frames);
      printk("speech_active_time_ms = %d ms\n\r", speech_active_time_ms);
      printk("metrics_on = %d\n\r", metrics_on);
      printk("====================\n\r");
    } else {
      printk("KWS Commands:\n\r");
      printk("  kws_el verbose <0|1>\n\r");
      printk("  kws_el rms <threshold>\n\r");
      printk("  kws_el debounce <ms>\n\r");
      printk("  kws_el window <n>\n\r");
      printk("  kws_el score <0.0-1.0>\n\r");
      printk("  kws_el min_frames <n>\n\r");
      printk("  kws_el speech <ms>\n\r");
      printk("  kws_el metrics <0|1>\n\r");
      printk("  kws_el show\n\r");
      printk("  kws_el stop\n\r");
      printk("  kws_el evt <n>\n\r");
    }
  }

  return 0;
}
void edge_learning_cmd_process(uint8_t value) {
  printk("cur_kws_edge_state %d\n", cur_kws_edge_state);

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
static int cmd_threads_stop(const struct shell *shell, size_t argc,
                            char **argv) {
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

SHELL_CMD_REGISTER(threads_stop, NULL, "Stop all worker threads",
                   cmd_threads_stop);
#endif

SHELL_CMD_REGISTER(kws_el, NULL, "KWS Edge Learn Support", cmd_kws_el);

SHELL_CMD_REGISTER(full_erase, NULL, "Erase flash: erase <size>",
                   cmd_full_erase);
SHELL_CMD_REGISTER(
    set, NULL, "Set MCU/AKD1500 as SPI-Master: set <bool> (0:AKD1500 1:MCU)",
    cmd_set);
SHELL_CMD_REGISTER(infer, NULL, "Start the Inference: infer", cmd_infer);
