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
Compute a 32-window min/max envelope over a block of DC-blocked int16 samples.
Each output pair (out[2*i], out[2*i+1]) is (min, max) of the samples in window
i. N must be a multiple of n_windows.
*/
static void compute_envelope(const int16_t *x, int N, int16_t *out_pairs,
                             int n_windows) {
  int win_len = N / n_windows;
  for (int w = 0; w < n_windows; w++) {
    int16_t lo = INT16_MAX;
    int16_t hi = INT16_MIN;
    const int16_t *p = &x[w * win_len];
    for (int i = 0; i < win_len; i++) {
      int16_t s = p[i];
      if (s < lo)
        lo = s;
      if (s > hi)
        hi = s;
    }
    out_pairs[2 * w] = lo;
    out_pairs[2 * w + 1] = hi;
  }
}

/*
Applies a DC blocking (high-pass) filter to a block of 16-bit PCM samples and
computes the RMS value of the filtered signal. This function removes DC offset
using a first-order IIR filter implemented in fixed-point (Q15) arithmetic,
making it suitable for embedded DSP / audio pipelines. The filtering is
performed in-place, meaning the input buffer is overwritten with filtered
samples.

When BLE streaming is enabled, also emits a binary PCM waveform frame to the
phone: 32 min/max pairs by default, or 32 decimated samples when the BLE link
has downshifted to fallback mode.
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

  // Compute RMS (used by VAD threshold in audio_processor.c)
  float mean = (float)sum_sq / N;
  *rms = sqrtf(mean);

  if (pdm_stream_flag && is_ble_connected()) {
    /* Expected N=960 @ 16 kHz / 60 ms. Emit 32 windows either as min/max
     * envelope (64 int16 = 128 B payload) or decimated samples (32 int16 =
     * 64 B payload) depending on the BLE link's current fallback state. */
    const int n_windows = 32;
    if (N >= n_windows) {
      if (wave_fallback_active()) {
        int16_t dec[32];
        int stride = N / n_windows;
        for (int i = 0; i < n_windows; i++) {
          dec[i] = x[i * stride];
        }
        send_pcm_wave(dec, n_windows);
      } else {
        int16_t env[64];
        compute_envelope(x, N, env, n_windows);
        send_pcm_wave(env, n_windows * 2);
      }
    }
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

void dmic_reset_dc_state(void) { dc_block_init(&dc_state); }

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

/**
 * @brief DMIC functional test command.
 *
 * This CLI command initializes and starts the DMIC (Digital Microphone) and
 * verifies that audio data blocks are received at the expected interval.
 *
 * The function reads audio blocks using dmic_read() and measures the time
 * difference between consecutive reads using k_uptime_get(). If the interval
 * between reads falls within the expected range (50 ms to 70 ms), it is
 * considered a valid cycle. The test passes after detecting 5 consecutive
 * valid cycles.
 *
 * A global timeout of 5 seconds is used to prevent the loop from running
 * indefinitely if the expected timing condition is not met.
 *
 * Test outcomes:
 * - PASS: 5 consecutive valid cycles detected within the expected timing range.
 * - TIMEOUT: Test duration exceeds the allowed timeout period.
 * - FAIL: DMIC initialization/start failure or read timeout.
 *
 * @param shell Pointer to the Zephyr shell instance.
 * @param argc  Number of command-line arguments.
 * @param argv  Array of command-line arguments.
 *
 * @return 0 on successful execution of the command, negative error code on
 * failure.
 */
static int cmd_test_dmic(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    printk("Invalid command\n");
    return -EINVAL;
  }

  if (dmic_init() < 0) {
    printk("DMIC init failed\n");
    return -1;
  }

  if (dmic_start() < 0) {
    printk("DMIC start failed\n");
    return -1;
  }

  printk("DMIC started\n");

  struct audio_block blk;
  int valid_cycles = 0;

  int64_t prev_time = k_uptime_get();
  int64_t test_start = k_uptime_get();

  while (1) {

    /* Global timeout check (5 seconds) */
    if (k_uptime_get() - test_start > 5000) {
      printk("DMIC TEST TIMEOUT\n");
      break;
    }
    int ret = dmic_read(dmic_dev, 0, &blk.data, &blk.size, READ_TIMEOUT);

    if (ret == 0) {

      int64_t now = k_uptime_get();
      int64_t diff = now - prev_time;
      prev_time = now;

      printk("DMIC data received: %lld ms\n", diff);
      // Check if the interval between consecutive DMIC data blocks is within
      // the expected 50–70 ms range
      if (diff >= 50 && diff <= 70) {
        valid_cycles++;
        printk("Cycle %d OK\n", valid_cycles);
      } else {
        valid_cycles = 0;
      }

      /* IMPORTANT: free buffer immediately */
      k_mem_slab_free(&mem_slab, blk.data);

      if (valid_cycles >= 5) {
        printk("DMIC TEST PASS\n");
        break;
      }
    } else if (ret == -EAGAIN) {
      printk("DMIC timeout\n");
      break;
    }
  }
  stop_dmic();

  return 0;
}

SHELL_CMD_REGISTER(dmic_start, NULL, "dmic_start", cmd_dmic_start);
SHELL_CMD_REGISTER(dmic_stop, NULL, "dmic_stop", cmd_dmic_stop);
SHELL_CMD_REGISTER(test_dmic, NULL, "test_dmic", cmd_test_dmic);
