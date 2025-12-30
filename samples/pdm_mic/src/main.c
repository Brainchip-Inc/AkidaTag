/*
 * DMIC Continuous RMS Monitor (NO DELAY, DC REMOVED)
 * nRF54L15 | NCS v3.0.2
 */

#include <zephyr/kernel.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#define SAMPLE_RATE       16000
#define SAMPLE_BIT_WIDTH  16
#define BYTES_PER_SAMPLE  sizeof(int16_t)
#define READ_TIMEOUT      120   /* <— IMPORTANT */

#define BLOCK_SIZE(_rate, _ch) \
	(BYTES_PER_SAMPLE * (_rate / 10) * _ch)

#define MAX_BLOCK_SIZE  BLOCK_SIZE(SAMPLE_RATE, 1)
#define BLOCK_COUNT     6

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

/* ================= RMS with DC Removal ================= */

static int16_t calculate_rms_dc_removed(int16_t *samples, uint32_t count)
{
	int64_t mean = 0;
	uint64_t sum = 0;

	/* Calculate DC offset */
	for (uint32_t i = 0; i < count; i++) {
		mean += samples[i];
	}
	mean /= count;

	/* RMS after DC removal */
	for (uint32_t i = 0; i < count; i++) {
		int32_t s = samples[i] - mean;
		sum += (uint64_t)(s * s);
	}

	return (int16_t)sqrtf((float)(sum / count));
}

/* ================= MAIN ================= */

int main(void)
{
	const struct device *dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
	int ret;

	if (!device_is_ready(dmic_dev)) {
		printk("DMIC not ready\n");
		return 0;
	}

	struct pcm_stream_cfg stream = {
		.pcm_width = SAMPLE_BIT_WIDTH,
		.mem_slab  = &mem_slab,
	};

	struct dmic_cfg cfg = {
		.io = {
			.min_pdm_clk_freq = 1000000,
			.max_pdm_clk_freq = 3500000,
			.min_pdm_clk_dc   = 40,
			.max_pdm_clk_dc   = 60,
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

	ret = dmic_configure(dmic_dev, &cfg);
	if (ret < 0) {
		printk("dmic_configure failed: %d\n", ret);
		return 0;
	}

	dmic_trigger(dmic_dev, DMIC_TRIGGER_START);

	printk("Continuous RMS (100ms):\n");

	while (1) {
		void *buffer;
		uint32_t size;

		ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
		if (ret < 0) {
			printk("read err %d\n", ret);
			continue;
		}

		int16_t rms =
			calculate_rms_dc_removed((int16_t *)buffer,
			                         size / BYTES_PER_SAMPLE);

		/* True continuous output */
		printk("RMS = %d\n", rms);

		k_mem_slab_free(&mem_slab, buffer);
	}
}
