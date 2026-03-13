/*
 * SPI Camera Control
 * Zephyr RTOS
 *
 * This module provides functions to interface with an SPI-based camera.
 * Features include:
 *   - SPI initialization and register access
 *   - Continuous frame capture
 *   - FIFO management
 *   - Base64 encoding of captured frames
 *   - Shell commands for camera start/stop
 */

#include "camera/spi_camera.h"
#include "ble_services/file_transfer.h"
#include "error.h"

#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>

/* ==================== RESOLUTION TABLE ====================
 * Only 2 resolutions supported — RAM does not allow more.
 *   96x96   RGB888 =  27,648 B
 *   128x128 RGB888 =  49,152 B <- maximum supported
 *
 * WARNING: 320x240, 320x320, and higher resolutions
 *          exceed the available RAM and are NOT supported.
 * =========================================================== */
typedef struct {
  const char *name;
  uint16_t width;
  uint16_t height;
  uint8_t reg_val;
} cam_res_t;

static const cam_res_t cam_res_table[3] = {
    {"96x96", 96, 96,
     CAM_SET_CAPTURE_MODE | CAM_IMAGE_MODE_96X96_LEGACY}, /* 1 */
    {"128x128", 128, 128,
     CAM_SET_CAPTURE_MODE | CAM_IMAGE_MODE_128X128_LEGACY}, /* 2 */
};

/* -1 = not set. Must call camera_set_pixel before camera_start. */
static int8_t cur_res_idx = -1;
static uint8_t camera_satrt_flg = false;

#define CUR_WIDTH cam_res_table[cur_res_idx].width
#define CUR_HEIGHT cam_res_table[cur_res_idx].height
#define CUR_REG_VAL cam_res_table[cur_res_idx].reg_val
#define CUR_PIXELS ((uint32_t)CUR_WIDTH * CUR_HEIGHT)
#define CUR_RGB565_BYTES (CUR_PIXELS * 2U)
#define CUR_RGB888_BYTES (CUR_PIXELS * 3U)
#define RED_BIT_MASK 0X1F
#define GREEN_BIT_MASK 0X3F
#define BLUE_BIT_MASK 0X1F
#define THREE_BIT 3
#define TWO_BIT 2
#define FOUR_BIT 4

/* ==================== SPI Device & Configuration ==================== */

/* SPI device used to communicate with the camera */

const struct device *spi3_dev = DEVICE_DT_GET(DT_NODELABEL(spi3));

/* SPI configuration for the camera */

static struct spi_config spi_cfg_camera = {
    .frequency = 8000000,                            // SPI frequency: 8 MHz
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB, // 8-bit MSB first
    .slave = 0,
    .cs =
        {
            .gpio =
                SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(camera)), // Camera CS pin
            .delay = 0,
        },
};

/* ==================== SPI HELPERS ==================== */
/**
 * @brief Write a value to a camera register over SPI
 *
 * @param addr Camera register address
 * @param val Value to write
 */

static void camera_write_reg(uint8_t addr, uint8_t val) {
  uint8_t tx[2] = {addr | 0x80, val};
  struct spi_buf buf = {.buf = tx, .len = 2};
  struct spi_buf_set set = {.buffers = &buf, .count = 1};
  spi_write(spi3_dev, &spi_cfg_camera, &set);
}

/**
 * @brief Read a value from a camera register over SPI
 *
 * @param addr Camera register address
 * @return uint8_t Value read from register
 */

static uint8_t camera_read_reg(uint8_t addr) {
  uint8_t tx[3] = {addr & 0x7F, 0, 0};
  uint8_t rx[3] = {0};
  struct spi_buf txb = {.buf = tx, .len = 3};
  struct spi_buf rxb = {.buf = rx, .len = 3};
  struct spi_buf_set txs = {.buffers = &txb, .count = 1};
  struct spi_buf_set rxs = {.buffers = &rxb, .count = 1};
  spi_transceive(spi3_dev, &spi_cfg_camera, &txs, &rxs);
  return rx[2];
}

/**
 * @brief Wait until camera sensor is idle
 *
 * Polls the camera's status register until the sensor is idle
 * or timeout occurs.
 * Time Calculation:
     Iterations: 100
     Delay per iteration: 1ms (k_msleep(1))
     Maximum timeout: 100 iterations × 1ms = 100ms total
 */

static void wait_i2c_idle(void) {
  for (int i = 0; i < I2C_IDLE_TIMEOUT_MS; i++) {
    if ((camera_read_reg(CAM_REG_SENSOR_STATE) & 0x03) == CAM_SENSOR_STATE_IDLE)
      return;
    k_msleep(1);
  }
}

/**
 * @brief Get the length of data in the camera FIFO
 *
 * @return uint32_t Number of bytes currently in FIFO
 */

static uint32_t fifo_length(void) {
  return (camera_read_reg(FIFO_SIZE3) << 16) |
         (camera_read_reg(FIFO_SIZE2) << 8) | camera_read_reg(FIFO_SIZE1);
}

/**
 * @brief Read data from the camera FIFO
 *
 * @param buf Pointer to buffer to store data
 * @param len Number of bytes to read
 */

static void fifo_read(uint8_t *buf, uint32_t len) {
  uint8_t cmd = BURST_FIFO_READ;
  uint8_t dummy = 0;
  struct spi_buf txb[] = {{.buf = &cmd, .len = 1},
                          {.buf = &dummy, .len = 1},
                          {.buf = NULL, .len = len}};
  struct spi_buf rxb[] = {{.buf = NULL, .len = 1},
                          {.buf = NULL, .len = 1},
                          {.buf = buf, .len = len}};
  struct spi_buf_set tx = {.buffers = txb, .count = 3};
  struct spi_buf_set rx = {.buffers = rxb, .count = 3};
  spi_transceive(spi3_dev, &spi_cfg_camera, &tx, &rx);
}

/* ==================== Camera Initialization & Capture ==================== */

/**
 * @brief Initialize the camera hardware
 *
 * Performs SPI communication tests, resets the camera, reads ID,
 * configures ISP settings (brightness, contrast, saturation, etc.),
 * and sets manual exposure and gain.
 *
 * @return int 0 if successful, negative on error
 */

int camera_init(void) {
  if (!device_is_ready(spi3_dev)) {
    printk("ERROR: SPI not ready\n");
    return -ENODEV;
  }
  k_msleep(100);

  camera_write_reg(ARDUCHIP_TEST1, 0x55);
  if (camera_read_reg(ARDUCHIP_TEST1) != 0x55) {
    printk("ERROR: SPI test failed\n");
    return -1;
  }

  /* Full reset */
  camera_write_reg(CAM_REG_SENSOR_RESET, CAM_SENSOR_RESET_ALL);
  wait_i2c_idle();
  k_msleep(100);

  uint8_t id = camera_read_reg(CAM_REG_SENSOR_ID);
  printk("Camera ID: 0x%02X\n", id);

  /* ISP tuning */
  camera_write_reg(CAM_REG_BRIGHTNESS, ISP_BRIGHTNESS);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_CONTRAST, ISP_CONTRAST);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_SATURATION, ISP_SATURATION);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_EV, ISP_EV);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_SHARPNESS, ISP_SHARPNESS);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_WHITE_BALANCE, ISP_WHITE_BALANCE);
  wait_i2c_idle();

  /* Manual exposure/gain */
  camera_write_reg(CAM_REG_EXPOSURE_GAIN_WB_CONTROL, 0x01);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_MANUAL_EXPOSURE_BIT_15_8,
                   (MANUAL_EXPOSURE >> 8) & 0xFF);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_MANUAL_EXPOSURE_BIT_7_0, MANUAL_EXPOSURE & 0xFF);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_MANUAL_GAIN_BIT_9_8, (MANUAL_GAIN >> 8) & 0xFF);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_MANUAL_GAIN_BIT_7_0, MANUAL_GAIN & 0xFF);
  wait_i2c_idle();

  return (id == 0 || id == 0xFF) ? -1 : 0;
}

/**
 * @brief Capture a single RGB frame from the camera
 *
 * Sets RGB format, resolution, clears FIFO, triggers capture,
 * waits for capture to complete, reads FIFO data.
 *
 * @param buf Buffer to store captured frame
 * @param max_len Maximum length of buffer
 * @return int Number of bytes read or negative on error
 */

static int capture_rgb(uint8_t *buf, uint32_t max_len) {
  /* Wait for capture */
  bool done = false;
  for (int i = 0; i < CAMERA_CAPTURE_TIMEOUT_MS; i++) {
    if (camera_read_reg(ARDUCHIP_TRIG) & CAP_DONE_MASK) {
      done = true;
      break;
    }
    k_msleep(1);
  }

  if (!done) {
    printk("ERROR: Capture timeout\n");
    return -1;
  }

  uint32_t len = fifo_length();
  printk("FIFO: %u bytes (expected: %u)\n", len, CUR_RGB565_BYTES);

  if (len == 0 || len > max_len) {
    printk("ERROR: Invalid FIFO length\n");
    /* Clear and start once to recover from a corrupted FIFO state */
    camera_write_reg(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK);
    k_msleep(1);
    camera_write_reg(ARDUCHIP_FIFO, FIFO_START_MASK);
    return -1;
  }

  fifo_read(buf, len);
  camera_write_reg(ARDUCHIP_FIFO, FIFO_START_MASK);
  return (int)len;
}

/* ==================== RGB565 to RGB888 Conversion ==================== */
/**
 * @brief Convert an image buffer from RGB565 format to RGB888 format
 *
 * This function converts pixel data from 16-bit RGB565 format
 * (5 bits Red, 6 bits Green, 5 bits Blue) into 24-bit RGB888 format
 * (8 bits per channel). The input pixels are expected in big-endian
 * byte order (MSB first). Color components are expanded to 8-bit
 * precision using bit replication for improved accuracy.
 *
 * The conversion is performed in-place (rgb565 and rgb888 point
 * to the same buffer). To avoid overwriting unread source pixels,
 * the loop iterates backwards from the last pixel to the first.
 * This is safe because RGB888 output (3 bytes/pixel) is always written
 * at a higher address than the RGB565 source (2 bytes/pixel) for the
 * same pixel index.
 *
 * Loop is unrolled by a factor of 4 to reduce loop overhead.
 * Both supported resolutions (96x96 = 9,216 and 128x128 = 16,384
 * pixels) are perfectly divisible by 4.
 * @param rgb565       Pointer to input buffer containing RGB565 data
 * @param rgb888       Pointer to output buffer for RGB888 data
 * @param pixel_count  Number of pixels to convert
 * @return int 0 if successful, -1 on error
 */

int convert_rgb565_to_rgb888(const uint8_t *rgb565, uint8_t *rgb888,
                             uint32_t pixel_count) {
  if (pixel_count == 0) {
    return -1;
  }

  for (uint32_t i = pixel_count; i >= 4; i -= 4) {
    /* Pixel i-1 (highest, processed first for in-place safety) */
    uint16_t p3 =
        ((uint16_t)rgb565[(i - 1) * 2] << 8) | rgb565[(i - 1) * 2 + 1];
    uint8_t r5 = (p3 >> 11) & RED_BIT_MASK;
    uint8_t g6 = (p3 >> 5) & GREEN_BIT_MASK;
    uint8_t b5 = p3 & BLUE_BIT_MASK;
    rgb888[(i - 1) * 3] = (r5 << THREE_BIT) | (r5 >> TWO_BIT);
    rgb888[(i - 1) * 3 + 1] = (g6 << TWO_BIT) | (g6 >> FOUR_BIT);
    rgb888[(i - 1) * 3 + 2] = (b5 << THREE_BIT) | (b5 >> TWO_BIT);

    /* Pixel i-2 */
    uint16_t p2 =
        ((uint16_t)rgb565[(i - 2) * 2] << 8) | rgb565[(i - 2) * 2 + 1];
    r5 = (p2 >> 11) & RED_BIT_MASK;
    g6 = (p2 >> 5) & GREEN_BIT_MASK;
    b5 = p2 & BLUE_BIT_MASK;
    rgb888[(i - 2) * 3] = (r5 << THREE_BIT) | (r5 >> TWO_BIT);
    rgb888[(i - 2) * 3 + 1] = (g6 << TWO_BIT) | (g6 >> FOUR_BIT);
    rgb888[(i - 2) * 3 + 2] = (b5 << THREE_BIT) | (b5 >> TWO_BIT);

    /* Pixel i-3 */
    uint16_t p1 =
        ((uint16_t)rgb565[(i - 3) * 2] << 8) | rgb565[(i - 3) * 2 + 1];
    r5 = (p1 >> 11) & RED_BIT_MASK;
    g6 = (p1 >> 5) & GREEN_BIT_MASK;
    b5 = p1 & BLUE_BIT_MASK;
    rgb888[(i - 3) * 3] = (r5 << THREE_BIT) | (r5 >> TWO_BIT);
    rgb888[(i - 3) * 3 + 1] = (g6 << TWO_BIT) | (g6 >> FOUR_BIT);
    rgb888[(i - 3) * 3 + 2] = (b5 << THREE_BIT) | (b5 >> TWO_BIT);

    /* Pixel i-4 (lowest in this group) */
    uint16_t p0 =
        ((uint16_t)rgb565[(i - 4) * 2] << 8) | rgb565[(i - 4) * 2 + 1];
    r5 = (p0 >> 11) & RED_BIT_MASK;
    g6 = (p0 >> 5) & GREEN_BIT_MASK;
    b5 = p0 & BLUE_BIT_MASK;
    rgb888[(i - 4) * 3] = (r5 << THREE_BIT) | (r5 >> TWO_BIT);
    rgb888[(i - 4) * 3 + 1] = (g6 << TWO_BIT) | (g6 >> FOUR_BIT);
    rgb888[(i - 4) * 3 + 2] = (b5 << THREE_BIT) | (b5 >> TWO_BIT);
  }
  return 0;
}
/* ==================== Base64 Encoding ==================== */

/**
 * @brief Encode a buffer to base64 and print over console
 *
 * @param buf Pointer to buffer
 * @param len Length of buffer
 */

static const char b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void send_base64_rgb888(const uint8_t *buf, uint32_t len) {
  printk("\n--- RGB888_START_ ---\n");
  /* Base64 encode RGB888 data */
  for (uint32_t i = 0; i < len; i += 3) {
    uint32_t n = buf[i] << 16;
    if (i + 1 < len)
      n |= buf[i + 1] << 8;
    if (i + 2 < len)
      n |= buf[i + 2];

    printk("%c%c%c%c", b64[(n >> 18) & 63], b64[(n >> 12) & 63],
           (i + 1 < len) ? b64[(n >> 6) & 63] : '=',
           (i + 2 < len) ? b64[n & 63] : '=');
  }

  printk("\n--- RGB888_END_ ---\n");
}

/* ==================== Camera Control API ==================== */

/**
 * @brief Start camera capture
 *
 * Clears FIFO and sets camera to start mode
 */

int camera_start(void) {
  if (spi3_dev == NULL) {
    return -ENODEV;
  }

  /* Set RGB format */
  camera_write_reg(CAM_REG_FORMAT, CAM_IMAGE_PIX_FMT_RGB);
  wait_i2c_idle();
  camera_write_reg(CAM_REG_CAPTURE_RESOLUTION, CUR_REG_VAL);
  wait_i2c_idle();

  /* Verify */
  uint8_t res_check = camera_read_reg(CAM_REG_CAPTURE_RESOLUTION);
  printk("Resolution register: 0x%02X (should be 0x%02X)\n", res_check,
         CUR_REG_VAL);

  /* Clear and start */
  camera_write_reg(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK);
  k_msleep(150);
  camera_write_reg(ARDUCHIP_FIFO, FIFO_START_MASK);

  /* --- Wait for buffer to be FREE ---
   *
   * Wait until the shared buffer becomes FREE before using it.
   *
   * Behavior:
   *  - If the buffer is already free → returns immediately.
   *  - If a model update is currently using the buffer
   *    → this call blocks until the update completes and releases it.
   *
   * Once the FREE event is received:
   *  1. Clear the FREE flag.
   *  2. Mark the buffer as BUSY to take ownership.
   *
   */
  k_event_wait(&sram_buf_event, BUF_EVENT_FREE, false, K_FOREVER);
  k_event_clear(&sram_buf_event, BUF_EVENT_FREE);
  k_event_post(&sram_buf_event, BUF_EVENT_BUSY);
  /* Warm-up */
  printk("Warming up (3 frames)...\n");
  for (int i = 0; i < 3; i++) {
    int len = capture_rgb(sram_upload_buffer, MAX_RGB888_SIZE);
    if (len > 0) {
      printk(" Warm-up %d: %d bytes\n", i + 1, len);
    } else {
      printk(" Warm-up %d: FAILED\n", i + 1);
    }
    k_msleep(200);
  }
  printk("Warm-up complete!\n\n");
  /* --- Release SRAM upload buffer ---
   *
   * Camera processing has finished using sram_upload_buffer.
   *
   * Steps:
   *  1. Clear the BUSY flag to indicate this module no longer owns the buffer.
   *  2. Post the FREE event to notify any waiting component
   *  that the buffer is now available.
   */
  k_event_clear(&sram_buf_event, BUF_EVENT_BUSY);
  k_event_post(&sram_buf_event, BUF_EVENT_FREE);

  camera_write_reg(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK);
  k_msleep(1);
  camera_write_reg(ARDUCHIP_FIFO, FIFO_START_MASK);
}

/**
 * @brief Stop camera capture
 */

void camera_stop(void) {
  /* CLEAR FIFO AND FULL RESET*/
  camera_write_reg(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK);
  camera_write_reg(CAM_REG_SENSOR_RESET, CAM_SENSOR_RESET_ALL);
  printk("camera stopped\n");
}

/**
 * @brief Continuous capture thread
 *
 * Performs a warm-up capture and then continuously
 * captures frames, sending them as base64 to console.
 */

void camera_capture_thread(void *a, void *b, void *c) {

  if (camera_init() != 0) {
    printk("camera init failed\n");
    return;
  }

  /* Main loop */
  while (1) {
    if (camera_satrt_flg) {

      printk("--- Sequence start ---\n");

      /* --- Wait for buffer to be FREE ---
       *
       * Wait until the shared buffer becomes FREE before using it.
       *
       * Behavior:
       *  - If the buffer is already free → returns immediately.
       *  - If a model update is currently using the buffer
       *    → this call blocks until the update completes and releases it.
       *
       * Once the FREE event is received:
       *  1. Clear the FREE flag.
       *  2. Mark the buffer as BUSY to take ownership.
       *
       */
      k_event_wait(&sram_buf_event, BUF_EVENT_FREE, false, K_FOREVER);
      k_event_clear(&sram_buf_event, BUF_EVENT_FREE);
      k_event_post(&sram_buf_event, BUF_EVENT_BUSY);

      int len = capture_rgb(sram_upload_buffer, MAX_RGB888_SIZE);
      if (len > 0) {
        if (convert_rgb565_to_rgb888(sram_upload_buffer, sram_upload_buffer,
                                     CUR_PIXELS) < 0) {
          printk("RGB565 to RGB888 conversion failed");
          continue;
        }
        /*
         * For actual Akida integration this function is NOT required.
         * It was added temporarily to send images to Python for testing.
         */
        send_base64_rgb888(sram_upload_buffer, CUR_RGB888_BYTES);
      } else {
        printk("Image FAILED\n");
      }

      /* --- Release SRAM upload buffer ---
       *
       * Camera processing has finished using sram_upload_buffer.
       *
       * Steps:
       *  1. Clear the BUSY flag to indicate this module no longer owns the
       * buffer.
       *  2. Post the FREE event to notify any waiting component
       *  that the buffer is now available.
       */
      k_event_clear(&sram_buf_event, BUF_EVENT_BUSY);
      k_event_post(&sram_buf_event, BUF_EVENT_FREE);

      printk("--- Sequence complete ---\n\n");

    } else {
      k_msleep(10);
    }
  }
}

/* ==================== Shell Commands ==================== */

/**
 * @brief Shell command to start camera
 */

static int cmd_camera_start(const struct shell *shell, size_t argc,
                            char **argv) {
  if (argc > 1) {
    printk("invalid command\n");
    return -EINVAL;
  }

  if (cur_res_idx < 0) {
    shell_error(shell, "Resolution not set. Run camera_set_pixel 1/2 first.");
    return -EINVAL;
  }

  if (camera_start() < 0) {
    return -1;
  }
  camera_satrt_flg = true;
  printk("camera started\n");
  return 0;
}

/**
 * @brief Shell command to stop camera
 */

static int cmd_camera_stop(const struct shell *shell, size_t argc,
                           char **argv) {
  if (argc > 1) {
    printk("invalid command\n");
    return -EINVAL;
  }
  camera_stop();
  camera_satrt_flg = false;
  return 0;
}

/**
 * @brief Set pixel resolution before starting camera
 *
 * Must be called before camera_start.
 *
 *   uart:~$ camera_set_pixel 1   ->  96x96
 *   uart:~$ camera_set_pixel 2   -> 128x128
 *
 * WARNING: 320x240, 320x320 and higher resolutions are not supported
 * (insufficient RAM)
 */
static int cmd_camera_set_pixel(const struct shell *shell, size_t argc,
                                char **argv) {
  if (argc != 2) {
    shell_print(shell, "Usage: camera_set_pixel <1|2>");
    shell_print(shell, "  1 ->  96x96");
    shell_print(shell, "  2 -> 128x128");
    shell_print(shell, "WARNING: 320x240, 320x320 and higher resolutions are "
                       "not supported (insufficient RAM)");
    return -EINVAL;
  }

  char choice = argv[1][0];
  if (choice < '1' || choice > '2' || argv[1][1] != '\0') {
    shell_error(shell, "Invalid: '%s'. Use 1 or 2.", argv[1]);
    shell_print(shell, "WARNING: 320x240, 320x320 and higher resolutions are "
                       "not supported (insufficient RAM)");
    return -EINVAL;
  }

  cur_res_idx = (int8_t)(choice - '1'); /* '1'->0, '2'->1 */
  shell_print(shell, "Pixel set: %s (%dx%d). Now run camera_start.",
              cam_res_table[cur_res_idx].name, CUR_WIDTH, CUR_HEIGHT);
  return 0;
}

/* Register shell commands */

SHELL_CMD_REGISTER(camera_start, NULL, "camera_start", cmd_camera_start);
SHELL_CMD_REGISTER(camera_stop, NULL, "camera_stop", cmd_camera_stop);
SHELL_CMD_REGISTER(camera_set_pixel, NULL, "Set resolution: 1=96x96 2=128x128 ",
                   cmd_camera_set_pixel);
