#include "pdm_mic.h"

extern "C" {
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/sys/printk.h>
}

#include <cstdint>
#include <cmath>

/* ================= CONFIG ================= */

#define SAMPLE_RATE       16000
#define SAMPLE_BIT_WIDTH  16
#define BYTES_PER_SAMPLE  sizeof(int16_t)
#define READ_TIMEOUT      120

#define BLOCK_SIZE(_rate, _ch) \
	(BYTES_PER_SAMPLE * (_rate / 10) * (_ch))

#define MAX_BLOCK_SIZE  BLOCK_SIZE(SAMPLE_RATE, 1)
#define BLOCK_COUNT     6

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);

/* ================= STATIC STATE ================= */

static const struct device *dmic_dev = nullptr;

/* ================= RMS (DC REMOVED) ================= */

static int16_t calculate_rms_dc_removed(int16_t *samples, uint32_t count)
{
	int64_t mean = 0;
	uint64_t sum = 0;

	for (uint32_t i = 0; i < count; i++) {
		mean += samples[i];
	}
	mean /= static_cast<int64_t>(count);

	for (uint32_t i = 0; i < count; i++) {
		int32_t s = samples[i] - static_cast<int32_t>(mean);
		sum += static_cast<uint64_t>(s * s);
	}

	float rms = std::sqrt(static_cast<float>(sum) /
	                      static_cast<float>(count));
	return static_cast<int16_t>(rms);
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

	stream.pcm_width = SAMPLE_BIT_WIDTH;
	stream.mem_slab  = &mem_slab;
	stream.pcm_rate  = SAMPLE_RATE;
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

	/* Channel config (ORDER SAFE) */
	cfg.channel.req_num_chan    = 1;
	cfg.channel.req_num_streams = 1;
	cfg.channel.req_chan_map_lo =
		dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
	cfg.channel.req_chan_map_hi = 0;

	return dmic_configure(dmic_dev, &cfg);
}


int dmic_rms_start(void)
{
	if (dmic_dev == nullptr) {
		return -ENODEV;
	}
	return dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
}

int dmic_rms_read(int16_t *rms_out)
{
	if (dmic_dev == nullptr || rms_out == nullptr) {
		return -EINVAL;
	}

	void *buffer = nullptr;
	uint32_t size = 0;

	int ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
	if (ret < 0) {
		return ret;
	}

	*rms_out = calculate_rms_dc_removed(
		static_cast<int16_t *>(buffer),
		size / BYTES_PER_SAMPLE
	);

	k_mem_slab_free(&mem_slab, buffer);
	return 0;
}
