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
#include "kws/kws_program_info.h"
#include "mnist/mnist_program_info.h"
#include "nrf_spi.h"
#include "sample_input/kws/kws_inputs.h"
#include "sample_input/mnist/mnist_inputs.h"
#include <akd1500/akd1500_spi_driver.h>
#include <cmath>
#include <hardware_device_impl.h>
#include <infra/system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include "acc_gyro.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "audio_processor.h"
#include "ble_services/ble_initialization.h"
#include "ble_services/file_transfer.h"
#include "boot_manager.h"
#include "error.h"
#include "littlefs_storage.h"
#include "pdm_mic.h"

#ifdef __cplusplus
}
#endif
void cli_worker_proc_thread(void *a, void *b, void *c);
/*
FLash offset indices
KWS - 1
MNIST - 0
*/

/** Input batch size, picked an arbitrary value */
#define INPUT_BATCH_SIZE (CONFIG_BATCH_SIZE)
#define GET_SEC_TO_USEC(x) (x * 1000000)

#define SAMPLING_RATE CONFIG_SAMPLING_RATE

#define NUM_CLASSES 36
#define NUM_NEURONS_PER_CLASS 15
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

/** Indexes of novel classes ranges from 33-35 */
#define KWS_EDGE_NOVEL_CLASS_BASE_ID 33
#define KWS_EDGE_MAX_NOVEL_CLASS_ID 35

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

static int verbose_on = 0;
/** Current state of the application */
static uint32_t cur_kws_edge_state = STATE_STOPPED;
/** Current novel class id, selected for learning*/
static uint32_t cur_kws_edge_novel_class = KWS_EDGE_NOVEL_CLASS_BASE_ID;

static bool is_kws_inference_started = false;

static bool kws_threads_suspended = false;

/** learn weights size */
static uint32_t mesh_learn_weights_size = 0;

/** Timestamp of last sample enqueued for learning */
static uint32_t last_learn_ts = 0;

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
static q7_t __aligned(4) spectrogram[SPECTROGRAM_COUNT][SPECTROGRAM_RES];
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

#define VALID_PROGRAM_DATA_MNIST 0xD8130700
#define VALID_PROGRAM_DATA_KWS 0xF4020100 // 0x64d70000
static const uint32_t dims[] = {SPECTROGRAM_COUNT, SPECTROGRAM_RES, 1};

int32_t akida_output[NUM_CLASSES * NUM_NEURONS_PER_CLASS] = {0};
#define VALID_PROGRAM_DATA_KWS 0x64d70000

/*I2C - ACC/GYRO*/
#define I2C_NODE DT_NODELABEL(mysensor)
#define SLEEP_TIME_MS 1000

const unsigned char *inputs[] = {mnist_inputs, kws_inputs};
uint32_t valid_program_data[] = {VALID_PROGRAM_DATA_MNIST,
                                 VALID_PROGRAM_DATA_KWS};
const unsigned char *program_info[] = {mnist_program_info, kws_program_info};
const int64_t program_info_len[] = {mnist_program_info_len,
                                    kws_program_info_len};

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

/* function to read 1st 4 bytes of model data from the flash_offsets[app_index]
 * and validate with the VALID_PROGRAM_DATA, once model is uploaded to the
 * spi-flash via external applications (BLE or Jlink)  */
bool check_program_data(int offset, int len, int app_index_l) {
  uint32_t data;
  spi_flash_read(spi_driver, offset, (uint8_t *)&data, 4);
  if (swap_endian(data) == valid_program_data[app_index_l]) {
    printk("program data @%x: %x is same as the expected one\n", offset,
           valid_program_data[app_index_l]);
    return true;
  }
  printk("program data @%x: %x is not the expected one\n", offset, data);
  return false;
}

// int spi_flash_erase_helper_func(uint32_t offset, uint32_t size);

int64_t time_ms() { return k_uptime_get(); }

void msleep(uint32_t duration) { k_sleep(K_MSEC(duration)); }

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

void do_inference(int spectrogram_index) {
  __aligned(32) static uint8_t akida_input[SPECTROGRAM_COUNT][SPECTROGRAM_RES];
  q7_t min = 127;
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
  }

  /* It has been experimentally determined that when we talk on the mic,
   * the energy goes above 666, and the first bin of the MFCC spectrogram
   * is above -39. This allows not to send data to akida if we already
   * know that there is nothing to detect.
   */
  // printk ("min %d, max %d  energy %d, spectrogram_index %d\n" , min, max,
  // energy, spectrogram[(25 + spectrogram_index) % SPECTROGRAM_COUNT][0]);
  if ((energy > params.energy_threshold) &&
      (spectrogram[(25 + spectrogram_index) % SPECTROGRAM_COUNT][0] >
       params.bin0_threshold))

  {

    // printk ("min %d, max %d  energy %d, spectrogram_index %d\n" , min, max,
    // energy, spectrogram_index);
    if (kws_edge_state[cur_kws_edge_state].on_mfcc_output) {
      /* Generate model input where whole spectrogram is normalized
         between 0 and 255 */
      for (int i = 0; i < SPECTROGRAM_COUNT; i++) {
        int idx = (i + spectrogram_index) % SPECTROGRAM_COUNT;
        for (int j = 0; j < SPECTROGRAM_RES; j++) {
          akida_input[i][j] =
              (uint8_t)((int)255 * ((int)spectrogram[idx][j] - (int)min) /
                        ((int)max - (int)min));
        }
      }

      // memcpy ((uint8_t*) akida_input, &kws_inputs[0], 490);
      // printk ("do_inference cur_kws_edge_state %d \n", cur_kws_edge_state);
      kws_edge_state[cur_kws_edge_state].on_mfcc_output((uint8_t *)akida_input,
                                                        (uint32_t *)dims);
      if (verbose_on) {
        printk("Spectrogram params max=%d energy=%d spectrogram_index=%d", max,
               energy, spectrogram_index);
      }
    }

    else {
      // printk ("on_mfcc_output is NULL on cur_kws_edge_state = %d\n",
      // cur_kws_edge_state);
    }

  } else if (last_learn_ts && STATE_LEARNING == cur_kws_edge_state) {
    uint32_t cur_ts = k_cycle_get_32();
    uint32_t last_learn_duration = (uint32_t)(cur_ts - last_learn_ts);
    uint64_t duration_us = k_cyc_to_us_floor64(last_learn_duration);
    if (duration_us >= GET_SEC_TO_USEC(5)) {
      switch_mode(STATE_LEARN_SELECT);
      printk("learning -> learn_select\n\r");
      akida_learn_mode(true);
      if (SUCCESS ==
          save_weights_from_mesh(
              learn_weights_buff_ptr,
              saved_learn_weights_ptr->learn_weights_data.learn_weights_size)) {
        saved_learn_weights_ptr->learn_weights_data.label_learnt_val |=
            1 << (cur_kws_edge_novel_class - KWS_EDGE_NOVEL_CLASS_BASE_ID);
        printk("Save Weights from MESH->MEM \n\r");
      } else {
        printk(
            "Sync:akida_save_learn_weights function has failed for label %d ",
            cur_kws_edge_novel_class);
      }
      akida_learn_mode(false);
      uint64_t duration_us = k_cyc_to_us_floor64(k_cycle_get_32() - cur_ts);
      if (verbose_on) {
        printk("mesh_mem = %" PRIu64 " us\n", duration_us);
      }
    }
  }
}

K_THREAD_STACK_DEFINE(capture_stack, CAPTURE_STACK_SIZE);
K_THREAD_STACK_DEFINE(process_stack, PROCESS_STACK_SIZE);

K_THREAD_STACK_DEFINE(cli_worker_stack, CONFIG_SHELL_STACK_SIZE);

struct k_thread capture_thread;
struct k_thread process_thread;
struct k_thread cli_worker_thread;

k_tid_t capture_tid;
k_tid_t process_tid;
k_tid_t cli_worker_tid;
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
      audio_processor_start(false, (q7_t *)spectrogram, spectrogram_dims,
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
  memset(spectrogram, -127, SPECTROGRAM_COUNT * SPECTROGRAM_RES);
  cur_kws_edge_state = STATE_LEARNING;
  printk("learn_select -> learning");
  last_learn_ts = k_cycle_get_32();
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

      akida_learn_mode(false);

      cur_kws_edge_state = mode;
    } else if (STATE_LEARNING == mode) {

      akida_learn_mode(true);
      k_work_schedule(&switch_delayed_work, K_SECONDS(1));
      //  switch_learning_delayed();
    }
    break;
  case STATE_LEARNING:

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

static int initiate_kws_inference() {
  k_work_init_delayable(&switch_delayed_work, switch_learning_delayed);
  mesh_learn_weights_size = akida_learn_mem_size();

  printk("mesh_learn_weights_size = %" PRIu32 "\n", mesh_learn_weights_size);

  /* allocating memory for structure (this will hold crc, size etc ) + learn
   * weights data together to place them in contiguous locations */
  saved_learn_weights_ptr = (saved_learn_weights *)malloc(
      sizeof(saved_learn_weights) + mesh_learn_weights_size);

  base_labels_wts_ptr = (uint8_t *)malloc(mesh_learn_weights_size);

  if ((saved_learn_weights_ptr == NULL) || (base_labels_wts_ptr == NULL)) {
    printk("dynamic memory allocation failed for weights data and hence "
           "application is not "
           "running ");
    return -EFAILURE;
  }
  /* initialize the learn_weights_mem structure */
  reset_saved_weights();
  // init_learn_weights_mem(mesh_learn_weights_size);

  read_learn_weights_from_flash();

  cur_kws_edge_state = STATE_INFERENCE;

  start_dmic_audio_proc();
  return SUCCESS;
}

int main(void) {

  /* printk("App Core Version: %s\n", CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION); */
  /* Image IDs defined by MCUboot */
  print_image_version(FLASH_AREA_ID(image_0), "App Core");

  /*print_image_version(FLASH_AREA_ID(image_1), "Net Core");*/

  uart_init();
  printk("Akida TAG Application\n");
  confirm_image_if_needed();
  init_setting_sub_system();
  file_transfer_init();
  ble_init();
  akida_spiflash_init();

  const struct device *qspi = DEVICE_DT_GET(DT_NODELABEL(mx25r64));

  if (!device_is_ready(qspi)) {
    printk("QSPI not ready\n");
  } else {
    printk("QSPI device ready: %s \n", qspi->name);
  }
  int err = storage_init();
  if (err != 0) {
    printk("LittleFS mount failed %d", err);
  } else {
    printk("LittleFS mount succeeded %d", err);
  }

  init_boot_count();

  akida_config_spi(1);
  int ret = check_program_data(flash_offsets[1], 4, 1);
  akida_config_spi(0);

  if (ret == false) {
    printk("model data, not present in SPI Flash, upload the model\n");
    kws_model_present = false;
  } else {
    printk("model is already present \n");
    // program the model info part to AKD1500

    printk("Programming the model\n");
    akida_program_flash((uint8_t *)program_info[1], program_info_len[1],
                        flash_offsets[1]);
    akd_device.set_batch_size(1, true);
    kws_model_present = true;
  }

  if (kws_model_present) {
    initiate_kws_inference();
    is_kws_inference_started = true;
  }

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

  static const struct i2c_dt_spec dev_i2c =
		I2C_DT_SPEC_GET(I2C_NODE);

	struct ism330_data data{};

	if (acc_gyro_init(&dev_i2c) < 0) {
		printk("ISM330 init failed\n");
		return;
	}

  while (1) {

    acc_gyro_read_all(&dev_i2c, &data);

		printk("ACC X:%d Y:%d Z:%d | GYR X:%d Y:%d Z:%d\n",
		       data.accel[0], data.accel[1], data.accel[2],
		       data.gyro[0],  data.gyro[1],  data.gyro[2]);
        
    prcess_led();
  }
}

static int32_t inference_on_mfcc_output(uint8_t *input, uint32_t *input_shape) {
  int ret = 0;
  static int last_found = -1, same_count = 0;

  if (params.sync_api == 0) {

    uint32_t cur_ts = k_cycle_get_32();

    if (SUCCESS == akida_forward(input, input_shape, (uint8_t *)akida_output,
                                 sizeof(akida_output))) {

      uint64_t inf_time = k_cyc_to_us_floor64((k_cycle_get_32() - cur_ts));
      if (verbose_on)
        printk("inf_time = %" PRIu64 " us\n", inf_time);

      int found =
          get_inferred_class(akida_output, NUM_CLASSES, NUM_NEURONS_PER_CLASS);

      if (found == -1) {
        printk("get_inferred_class failure\n");
        return -1;
      }
      if (last_found != found) {
        last_found = found;
        same_count = 1;
      } else {
        same_count++;
        if (same_count >= params.infer_threshold) {
          current_class = found;
          printk("\nClass : %d\n", found);
          printk("Word : %s\n", kws_tags[found]);
          same_count = 0;
        }
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

  last_learn_ts = k_cycle_get_32();
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
int infer(int app_index_l) {
  if (app_index_l > 1) {
    printk("Illegal model index %d\n", app_index_l);
    return -1;
  }

  akida_config_spi(1);
  int ret = check_program_data(flash_offsets[app_index_l], 4, app_index_l);
  akida_config_spi(0);

  if (ret == false) {
    printk("model data, not present in SPI Flash, upload the model\n");
    return -1;
  } else {
    printk("model is already present \n");
    // program the model info part to AKD1500

    printk("Programming the model\n");
    akida_program_flash((uint8_t *)program_info[app_index_l],
                        program_info_len[app_index_l],
                        flash_offsets[app_index_l]);
    akd_device.set_batch_size(1, true);
    app_index = app_index_l;
  }

  akd_device.toggle_clock_counter(true);

  uint32_t inf_complete = 0;
  uint32_t s_dma_cycls = 0;
  uint64_t s_tick = 0;
  uint64_t e_tick = 0;
  uint32_t inf_time = 0;
  uint32_t e_dma_cycls = 0;
  uint32_t delta_cycle = 0;
  int num_classes = 10;
  int num_neurons_per_class = 1;

  auto shape = mnist_inputs_shape;
  int output_size = 10 * 4;
  if (app_index_l == 1) {
    shape = kws_inputs_shape;
    num_classes = NUM_CLASSES;
    num_neurons_per_class = NUM_NEURONS_PER_CLASS;
    output_size = sizeof(akida_output);
  }

  int class_id = -1;
  uint32_t inp_shap[] = {shape[0], shape[1], shape[2]};
  s_dma_cycls = akd_device.read_clock_counter();
  s_tick = time_ms();
  ret = akida_forward((uint8_t *)inputs[app_index_l], inp_shap,
                      (uint8_t *)akida_output, output_size);
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
  if (app_index_l == 0) { // mnist
    printk("Predicted Digit : %d\n", class_id);

    k_thread_suspend(capture_tid);
    k_thread_suspend(process_tid);
    kws_threads_suspended = true;
  } else if (app_index_l == 1) { // kws
    printk("\nClass : %d\n", class_id);
    printk("Word : %s\n", kws_tags[class_id]);
    kws_model_present = true;
    if (!is_kws_inference_started) {
      initiate_kws_inference();
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
    app_index = 1;
    printk("inference kws requested, app index %d", app_index);
  } else if (!strcmp(string, "mnist")) {
    app_index = 0;
    printk("inference mnist requested, app index %d", app_index);
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

  if (spi_flash_erase_helper_func(0x1000, FLASH_MAX_16_MB_SIZE - 0x1000)) {
    return 1;
  }

  return 0;
}

/* shell cli function to invoke erase function */
static int cmd_kws_el(const struct shell *shell, size_t argc, char **argv) {
  printk("cmd exec argc %d\n", argc);
  if (argc > 1) {
    if (argc > 2 && !strcmp(argv[1], "verbose")) {
      verbose_on = atoi(argv[2]);
      audio_processor_set_verbose(verbose_on);
    } else if (!strcmp(argv[1], "stop")) {
      cur_kws_edge_state = STATE_STOPPED;
      audio_processor_stop();
    } else if (!strcmp(argv[1], "evt")) {
      if (argc > 2) {
        printk(" cur_kws_edge_state %d\n", cur_kws_edge_state);
        kws_edge_state[cur_kws_edge_state].on_user_input(atoi(argv[2]));
      }
    } else {
      printk("incorrect command \n\r");
    }
  }

  return 0;
}

SHELL_CMD_REGISTER(kws_el, NULL, "KWS Edge Learn Support", cmd_kws_el);

SHELL_CMD_REGISTER(full_erase, NULL, "Erase flash: erase <size>",
                   cmd_full_erase);
SHELL_CMD_REGISTER(
    set, NULL, "Set MCU/AKD1500 as SPI-Master: set <bool> (0:AKD1500 1:MCU)",
    cmd_set);
SHELL_CMD_REGISTER(infer, NULL, "Start the Inference: infer", cmd_infer);
