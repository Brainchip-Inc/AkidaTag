/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <soc.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include "akd_spi_flash.h"
#include "akd_spi_flash_handler.h"
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
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
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

#define SAMPLING_RATE CONFIG_SAMPLING_RATE

#define NUM_CLASSES 33
#define NUM_NEURONS_PER_CLASS 1
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

#define LEARN_WEIGHTS_FILE_NAME "/kwswts.bin"

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

static uint8_t cur_kws_edge_state = 0;
static bool id_dmic_audio_proc_started = false;

/** Spectrogram holding the required spectrogram for inference (prior to
  normalization) */
static q7_t __aligned(4) spectrogram[SPECTROGRAM_COUNT][SPECTROGRAM_RES];
/** spectrogram dimensions */
static uint8_t __aligned(4) spectrogram_dims[2] = {SPECTROGRAM_COUNT,
                                                   SPECTROGRAM_RES};

static int32_t get_inferred_class(int32_t *result, int num_classes,
                                  int num_neurons);

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

static kws_edge_state_processor kws_edge_state[STATE_COUNT] = {
    [STATE_INFERENCE] = {inference_on_mfcc_output, inference_on_user_input},
    [STATE_LEARN_SELECT] = {NULL, learn_select_on_user_input},
    [STATE_LEARNING] = {learning_on_mfcc_output, learning_on_user_input},
};

#define VALID_PROGRAM_DATA_MNIST 0xD8130700
#define VALID_PROGRAM_DATA_KWS 0x64d70000
static const uint32_t dims[] = {SPECTROGRAM_COUNT, SPECTROGRAM_RES, 1};

int32_t akida_output[NUM_CLASSES * NUM_NEURONS_PER_CLASS] = {0};

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

int32_t get_inferred_class(int32_t *result, int num_classes, int num_neurons) {
  int32_t max_val = 0, max_index = -1, n_activations = 0;
  n_activations = num_classes * num_neurons;
  for (int i = 0; i < n_activations; i++) {
    if (result[i] > max_val) {
      max_val = result[i];
      max_index = i;
    }
  }
  printk("\nClass : %d\n", max_index);
  printk("Word : %s\n", kws_tags[max_index]);
  return (max_index / num_neurons);
}

int post_processing(auto out, const int32_t *bytes_out, int app_index_l) {
  if (app_index_l <= 1) {
    int32_t max_val = bytes_out[0];
    int max_index = -1;
    for (int i = 0; i < (int)out->size(); i++) {
      if (bytes_out[i] > max_val) {
        max_val = bytes_out[i];
        max_index = i;
      }
    }
    return max_index;
  } else {
    return EFAILURE;
  }
}

int akida_forward(uint8_t *input, uint32_t *input_dims, uint8_t *output,
                  int output_size) {

  akida::TensorConstPtr in = akida::Dense::create_view(
      reinterpret_cast<const char *>(input), akida::TensorType::uint8,
      {input_dims[0], input_dims[1], input_dims[2]},
      akida::Dense::Layout::RowMajor);

  /** Execute inference */
  auto ret = akd_device.forward({in});

  if (ret.size()) {
    /** Get output buffer */
    auto out = akida::Tensor::ensure_dense(std::move(ret[0]));
    if (out && out->size() * sizeof(int) == (size_t)output_size) {
      const unsigned char *bytes_out = (unsigned char *)out->buffer()->data();
      memcpy(output, bytes_out, output_size);
      return SUCCESS;
    }
  }
  return -EFAILURE;
}

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
      // printk ("do_inference 123 \n");
      kws_edge_state[cur_kws_edge_state].on_mfcc_output((uint8_t *)akida_input,
                                                        (uint32_t *)dims);
      if (verbose_on) {
        printk("Spectrogram params max=%d energy=%d spectrogram_index=%d", max,
               energy, spectrogram_index);
      }
    }
  }
  /*else{
          //printk(" less than energy_threshold \n");
  }*/
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
  id_dmic_audio_proc_started = true;
  return 0;
}

int main(void) {
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
    akida_program_info((uint8_t *)program_info[1], program_info_len[1],
                       flash_offsets[1]);
    akd_device.set_batch_size(1, true);
    kws_model_present = true;
  }

  if (kws_model_present) {
    start_dmic_audio_proc();
  }

  cli_worker_tid = k_thread_create(
      &cli_worker_thread, cli_worker_stack, CONFIG_SHELL_STACK_SIZE,
      cli_worker_proc_thread, NULL, NULL, NULL, CLI_WORKER_PRIORITY, K_USER,
      K_FOREVER // START SUSPENDED
  );
  k_thread_start(cli_worker_tid);

  return 0;
  for (;;) {
    // prcess_led();
  }
}

void cli_worker_proc_thread(void *a, void *b, void *c) {
  printk("CLI Worker: \n\r");

  while (1) {
    prcess_led();
  }
}

static int32_t inference_on_mfcc_output(uint8_t *input, uint32_t *input_shape) {
  int ret = 0;
  static int last_found = 0, same_count = 0;
  // printk("inference_on_mfcc_output\n");
  // if (params.sync_api == 0)
  {

    if (0 == akida_forward(input, input_shape, (uint8_t *)akida_output,
                           sizeof(akida_output))) {
      int found =
          get_inferred_class(akida_output, NUM_CLASSES, NUM_NEURONS_PER_CLASS);

      if (found == -1) {
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
        }
      }
    } else {
      printk("akida_forward failure\n");
    }
  }
  return ret;
}

static void inference_on_user_input(int input_type) {
  printk("inference_on_user_input\n");
}

static void learn_select_on_user_input(int input_type) {
  printk("learn_select_on_user_input\n");
}

static int32_t learning_on_mfcc_output(uint8_t *input, uint32_t *input_shape) {
  printk("learning_on_mfcc_output\n");
  return 0;
}

static void learning_on_user_input(int input_type) {
  printk("learning_on_user_input\n");
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
    akida_program_info((uint8_t *)program_info[app_index_l],
                       program_info_len[app_index_l],
                       flash_offsets[app_index_l]);
    akd_device.set_batch_size(1, true);
    app_index = app_index_l;
  }

  stop_dmic(); // stop dmic to perform static frame inference
  akd_device.toggle_clock_counter(true);

  uint32_t inf_complete = 0;
  uint32_t s_dma_cycls = 0;
  uint64_t s_tick = 0;
  uint64_t e_tick = 0;
  uint32_t inf_time = 0;
  uint32_t e_dma_cycls = 0;
  uint32_t delta_cycle = 0;

  auto shape = mnist_inputs_shape;

  if (app_index_l == 1)
    shape = kws_inputs_shape;

  akida::TensorConstPtr in = akida::Dense::create_view(
      reinterpret_cast<const char *>(inputs[app_index_l]),
      akida::TensorType::uint8, {shape}, akida::Dense::Layout::RowMajor);

  s_dma_cycls = akd_device.read_clock_counter();
  s_tick = time_ms();
  auto inference_op = akd_device.forward({in});
  e_tick = time_ms();
  e_dma_cycls = akd_device.read_clock_counter();
  delta_cycle = e_dma_cycls - s_dma_cycls;
  inf_time = e_tick - s_tick;
  printk("\n\rinference time= %u dma cycles, time = %u ms\n\r", delta_cycle,
         inf_time);
  /** Get output buffer */
  auto out = akida::Tensor::ensure_dense(std::move(inference_op[0]));
  const int32_t *bytes_out = (int32_t *)out->buffer()->data();
  for (int i = 0; i < (int)out->size(); i++) {
    printk("Output-%d = %d\n", i, bytes_out[i]);
    inf_complete = 0xAA;
  }
  int class_id = post_processing(out, bytes_out, app_index_l);

  if (class_id == -1) {
    return -1;
  }
  if (app_index_l == 0) { // mnist
    printk("Predicted Digit : %d\n", class_id);
  } else if (app_index_l == 1) { // kws
    printk("\nClass : %d\n", class_id);
    printk("Word : %s\n", kws_tags[class_id]);
    kws_model_present = true;
    if (!id_dmic_audio_proc_started)
      start_dmic_audio_proc();
  }

  if (inf_complete != 0xAA) {
    printk("Inference did not happen\n");
  } else {
    printk("APP Inference Completed\n");
  }
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

SHELL_CMD_REGISTER(full_erase, NULL, "Erase flash: erase <size>",
                   cmd_full_erase);
SHELL_CMD_REGISTER(
    set, NULL, "Set MCU/AKD1500 as SPI-Master: set <bool> (0:AKD1500 1:MCU)",
    cmd_set);
SHELL_CMD_REGISTER(infer, NULL, "Start the Inference: infer", cmd_infer);
