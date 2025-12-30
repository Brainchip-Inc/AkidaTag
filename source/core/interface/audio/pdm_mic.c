#include "pdm_mic.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/sys/printk.h>

#include <stdint.h>
#include <math.h>
#include <string.h>

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

/* ================= STATIC STATE ================= */

static const struct device *dmic_dev = NULL;

/* ================= RMS (DC REMOVED) ================= */

static int16_t calculate_rms_dc_removed(int16_t *samples, uint32_t count)
{
	int64_t mean = 0;
	uint64_t sum = 0;

	/* Calculate DC offset */
	for (uint32_t i = 0; i < count; i++) {
		mean += samples[i];
	}
	mean /= (int64_t)count;

	/* RMS after DC removal */
	for (uint32_t i = 0; i < count; i++) {
		int32_t s = (int32_t)samples[i] - (int32_t)mean;
		sum += (uint64_t)(s * s);
	}

	float rms = sqrtf((float)sum / (float)count);
	return (int16_t)rms;
}

/* ================= PUBLIC API ================= */

int dmic_rms_init(void)
{
	dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

	if (!device_is_ready(dmic_dev)) {
		printk("DMIC not ready\n");
		return -ENODEV;
	}

	/* -------- Stream configuration -------- */
	static struct pcm_stream_cfg stream;
	memset(&stream, 0, sizeof(stream));

	stream.pcm_width  = SAMPLE_BIT_WIDTH;
	stream.mem_slab   = &mem_slab;
	stream.pcm_rate   = SAMPLE_RATE;
	stream.block_size = BLOCK_SIZE(SAMPLE_RATE, 1);

	/* -------- DMIC configuration -------- */
	static struct dmic_cfg cfg;
	memset(&cfg, 0, sizeof(cfg));

	/* IO config */
	cfg.io.min_pdm_clk_freq = 1000000;
	cfg.io.max_pdm_clk_freq = 3500000;
	cfg.io.min_pdm_clk_dc   = 40;
	cfg.io.max_pdm_clk_dc   = 60;

	/* Stream config */
	cfg.streams = &stream;

	/* Channel config */
	cfg.channel.req_num_chan    = 1;
	cfg.channel.req_num_streams = 1;
	cfg.channel.req_chan_map_lo =
		dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
	cfg.channel.req_chan_map_hi = 0;

	return dmic_configure(dmic_dev, &cfg);
}

int dmic_rms_start(void)
{
	if (dmic_dev == NULL) {
		return -ENODEV;
	}

	return dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
}

int dmic_rms_read(int16_t *rms_out)
{
	if (dmic_dev == NULL || rms_out == NULL) {
		return -EINVAL;
	}

	void *buffer = NULL;
	uint32_t size = 0;

	int ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
	if (ret < 0) {
		return ret;
	}

	*rms_out = calculate_rms_dc_removed(
		(int16_t *)buffer,
		size / BYTES_PER_SAMPLE
	);

	k_mem_slab_free(&mem_slab, buffer);
	return 0;
}
void dmic_capture_thread(void *a, void *b, void *c)
{
	int16_t rms;

    while (1) {
        if (dmic_rms_read(&rms) == 0) {
			printk("RMS = %d\n", rms);
		}
    }
}
