/*
 * DMIC Continuous RMS Monitor (NO DELAY, DC REMOVED)
 * nRF53xx | NCS v3.1.1
 */
#include "pdm_mic.h"
#include "error.h"
#if IS_ENABLED(CONFIG_WDT_ENABLE)
#include "watchdog_h/watchdog.h"
#endif
#include "led/led_init.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys_clock.h>

#include "ble_services/ble_initialization.h"
atomic_t is_dmic_start;

K_MEM_SLAB_DEFINE(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 32);

/* Queue holds audio_block descriptors */
K_MSGQ_DEFINE(audio_msgq, sizeof(struct audio_block), BLOCK_COUNT, 32);

const struct device *dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

typedef struct {
  int32_t prev_x;
  int32_t prev_y;
} dc_block_t;

dc_block_t dc_state;

void dc_block_init(dc_block_t *s) {
  s->prev_x = 0;
  s->prev_y = 0;
}
/*
Applies a DC blocking (high-pass) filter to a block of 16-bit PCM samples and
computes the RMS value of the filtered signal. This function removes DC offset
using a first-order IIR filter implemented in fixed-point (Q15) arithmetic,
making it suitable for embedded DSP / audio pipelines. The filtering is
performed in-place, meaning the input buffer is overwritten with filtered
samples.
*/
static int dc_block_process(dc_block_t *s, int16_t *x, int N, float *rms) {
  const int32_t alpha = 32700; // ~0.998 in Q15
  int64_t sum_sq = 0;          // for RMS

  if (N <= 0) {
    printk("no of samples passed is incorrect %d\n", N);
    return -EFAILURE;
  }
  for (int i = 0; i < N; i++) {
    int32_t y = x[i] - s->prev_x + ((alpha * s->prev_y) >> 15);

    s->prev_x = x[i];
    s->prev_y = y;

    // Saturate safely
    if (y > 32767)
      y = 32767;
    if (y < -32768)
      y = -32768;

    x[i] = (int16_t)y;

    // RMS accumulation
    sum_sq += (int32_t)x[i] * x[i];
  }

  // Compute RMS
  float mean = (float)sum_sq / N;
  *rms = sqrtf(mean);
  if (pdm_stream_flag) {
    uint32_t send_rms = (uint32_t)*rms;
    send_pdm_data(send_rms);
  }
  return SUCCESS;
}

int dmic_start(void) {
  if (dmic_dev == NULL) {
    return -ENODEV;
  }
  return dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
}

int dmic_init(void) {
  if (!device_is_ready(dmic_dev)) {
    printk("DMIC not ready\n");
    return -ENODEV;
  }

  /* -------- Stream configuration -------- */
  static struct pcm_stream_cfg stream;
  memset(&stream, 0, sizeof(stream));

  stream.pcm_width = SAMPLE_BIT_WIDTH;
  stream.mem_slab = &mem_slab;
  stream.pcm_rate = SAMPLE_RATE;
  stream.block_size = BLOCK_SIZE(SAMPLE_RATE, 1);

  /* -------- DMIC configuration -------- */
  static struct dmic_cfg cfg;
  memset(&cfg, 0, sizeof(cfg));

  /* IO config */
  cfg.io.min_pdm_clk_freq = 1000000;
  cfg.io.max_pdm_clk_freq = 1200000;
  cfg.io.min_pdm_clk_dc = 48;
  cfg.io.max_pdm_clk_dc = 52;

  /* Stream config */
  cfg.streams = &stream;

  /* Channel config */
  cfg.channel.req_num_chan = 1;
  cfg.channel.req_num_streams = 1;
  cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
  cfg.channel.req_chan_map_hi = 0;

  int ret = dmic_configure(dmic_dev, &cfg);
  if (ret < 0) {
    printk("dmic_configure failed: %d\n", ret);
    return -EFAILURE;
  }
  dc_block_init(&dc_state);
  if (dmic_start() < 0) {
    printk("DMIC start failed\n");
    return -EFAILURE;
  }

  return SUCCESS;
}

int dmic_process(uint16_t *pcm, uint32_t passed_size, float *p_rms_val) {
  float rms_val = 0.0f;
  int ret_val = -EFAILURE;
  if (SUCCESS == dc_block_process(&dc_state, pcm, passed_size, &rms_val)) {
    *p_rms_val = rms_val;
    ret_val = SUCCESS;
  }

  return ret_val;
}

extern int16_t orig_buf[];
void dmic_capture_thread(void *a, void *b, void *c) {
#if IS_ENABLED(CONFIG_WDT_ENABLE)
  wdt_enable_thread(DMIC_CAPTURE);
#endif
  struct audio_block blk;
  printk("dmic_capture_thread: \n\r");
  uint32_t dmic_capture_thread_cntr = 0;

  while (1) {
    if (dmic_read(dmic_dev, 0, &blk.data, &blk.size, READ_TIMEOUT) == 0) {

      /* Push buffer pointer to processing thread */
      if (k_msgq_put(&audio_msgq, &blk, K_NO_WAIT) != 0) {
        /* Queue full → drop buffer safely */
        printk("audio_msgq is full");
      }

      memcpy((void *)orig_buf, blk.data, blk.size);
      k_mem_slab_free(&mem_slab, blk.data);
      /*
       * Signal the LED indication thread every 2 cycles of the DMIC capture
       * loop. The DMIC thread runs every ~60 ms, so triggering on every second
       * cycle generates a ~120 ms event used by the LED thread for timing.
       */
      if (dmic_capture_thread_cntr % 2 == 0) {
        k_sem_give(&led_sem);
      }
      dmic_capture_thread_cntr++;
      // printk ("dmic %d\n", dmic_capture_thread_cntr);
    }
/* Feed WDT regardless of dmic_read() result to avoid trigger when DMIC is
 * stopped */
#if IS_ENABLED(CONFIG_WDT_ENABLE)
    /* Mark thread as healthy */
    atomic_set(&thread_health[DMIC_CAPTURE], 1);
#endif
  }
}

void stop_dmic(void) {
  struct audio_block blk;
  dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);

  /* Drain pending buffers */
  while (dmic_read(dmic_dev, 0, &blk.data, &blk.size, READ_TIMEOUT) == 0) {
    k_mem_slab_free(&mem_slab, blk.data);
  }
  printk("dmic_stop done ");
}

static int cmd_dmic_stop(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    printk("invalid command ");
    return -EINVAL;
  }
  atomic_set(&is_dmic_start, 0);
  stop_dmic();
  return 0;
}

static int cmd_dmic_start(const struct shell *shell, size_t argc, char **argv) {

  if (argc > 1) {
    printk("invalid command ");
    return -EINVAL;
  }

  if (dmic_init() < 0) {
    printk("DMIC init failed\n");
    return -1;
  }
  /* -------- MIC PDM Start -------- */
  if (dmic_start() < 0) {
    printk("DMIC start failed\n");
    return -1;
  }
  atomic_set(&is_dmic_start, 1);
  printk("dmic_start done ");
  return 0;
}

SHELL_CMD_REGISTER(dmic_start, NULL, "dmic_start", cmd_dmic_start);
SHELL_CMD_REGISTER(dmic_stop, NULL, "dmic_stop", cmd_dmic_stop);
