/*
 * DMIC Continuous RMS Monitor (NO DELAY, DC REMOVED)
 * nRF53xx | NCS v3.1.1
 */
#include "pdm_process.h"
#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/sys/printk.h>
#include <math.h>
#include <string.h>
#include "error.h"
#include <zephyr/sys/util.h>
#include <zephyr/shell/shell.h>

#include <zephyr/sys_clock.h>

#include <stdint.h>


K_MEM_SLAB_DEFINE(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 32);


/* Queue holds audio_block descriptors */
K_MSGQ_DEFINE(audio_msgq,
              sizeof(struct audio_block),
              BLOCK_COUNT,
              32);

const struct device *dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));


static int16_t calculate_rms_dc_removed(int16_t *samples, uint32_t count)
{
	int64_t mean = 0;
	uint64_t sum = 0;
	int16_t val;

	/* Calculate DC offset */
	for (uint32_t i = 0; i < count; i++) {
		mean += samples[i];
	}
	mean /= count;

	/* RMS calculation (non-destructive) */
	for (uint32_t i = 0; i < count; i++) {
		val = samples[i] - mean;
		sum += (uint64_t)(val * val);
	}

	return (int16_t)sqrtf((float)(sum / count));
}

typedef struct {
    int32_t prev_x;
    int32_t prev_y;
} dc_block_t;

dc_block_t dc_state;


void dc_block_init(dc_block_t *s) {
    s->prev_x = 0;
    s->prev_y = 0;
}

void dc_block_process(dc_block_t *s, int16_t *x, int N) {
    const int32_t alpha = 32700; // ~0.998 in Q15

    for (int i = 0; i < N; i++) {
        int32_t y = x[i]
                  - s->prev_x
                  + ((alpha * s->prev_y) >> 15);

        s->prev_x = x[i];
        s->prev_y = y;

        // Saturate safely
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;

        x[i] = (int16_t)y;
    }
}

int pdm_init(void)
{
	if (!device_is_ready(dmic_dev)) {
		printk("DMIC not ready\n");
		return EFAILURE;
	}

	struct pcm_stream_cfg stream = {
		.pcm_width = SAMPLE_BIT_WIDTH,
		.mem_slab  = &mem_slab,
	};

	struct dmic_cfg cfg = {
		.io = {
			.min_pdm_clk_freq = 1000000,
			.max_pdm_clk_freq = 1200000,
			.min_pdm_clk_dc   = 48,       // 40–60% works
			.max_pdm_clk_dc   = 52,
		},
		.streams = &stream,
		.channel = {
			.req_num_streams = 1,
			.req_num_chan    = 1,
			.req_chan_map_lo =
				dmic_build_channel_map(0, 0, PDM_CHAN_LEFT),
		},
	};

	cfg.streams[0].pcm_rate  = SAMPLE_RATE;
	cfg.streams[0].block_size =
		BLOCK_SIZE(SAMPLE_RATE, 1);

	int ret = dmic_configure(dmic_dev, &cfg);
	if (ret < 0) {
		printk("dmic_configure failed: %d\n", ret);
		return EFAILURE;
	}
    dc_block_init(&dc_state);
	k_sleep(K_MSEC(20));
	dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
	return SUCCESS;

}

int pdm_process(uint16_t *pcm, uint32_t passed_size)
{
   dc_block_process(&dc_state, pcm, passed_size);

	return SUCCESS;
}

extern int16_t orig_buf[];
void dmic_capture_thread(void *a, void *b, void *c)
{
	struct audio_block blk;
    printk ("dmic_capture_thread: \n\r");
	uint32_t dmic_capture_thread_cntr = 0;
	
    while (1) {
        if (dmic_read(dmic_dev, 0,  &blk.data, &blk.size, READ_TIMEOUT) == 0) {
     
            /* Push buffer pointer to processing thread */
            if (k_msgq_put(&audio_msgq,  &blk, K_NO_WAIT) != 0) {
                /* Queue full → drop buffer safely */
				printk ("audio_msgq is full");

            }
			memcpy ((void *)orig_buf, blk.data, blk.size);
			k_mem_slab_free(&mem_slab,  blk.data);			
			dmic_capture_thread_cntr++;
			//printk ("dmic %d\n", dmic_capture_thread_cntr);			
        }
    }
}


void stop_dmic (void)
{
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
  stop_dmic();
  return 0;

}
SHELL_CMD_REGISTER(dmic_stop, NULL, "dmic_stop", cmd_dmic_stop);
