#ifndef __PDM_MIC_H__
#define __PDM_MIC_H__
#include <stdint.h>
#include <zephyr/kernel.h>
int dmic_process(uint16_t* passed_buffer, uint32_t passed_size, float* p_rms_val);
int dmic_start(void);
int dmic_init(void);
void dmic_capture_thread(void* a, void* b, void* c);
void stop_dmic(void);
/* Re-zero the DC-blocking IIR state. Intended to be called after stop_dmic()
 * and before dmic_start() when restarting the pipeline mid-run so the first
 * post-restart block doesn't carry a step transient. */
void dmic_reset_dc_state(void);

/* Change the audio DMA block size at runtime (in ms). Must be a multiple of
 * AUDIO_BLOCK_STEP_MS and no larger than AUDIO_MAX_BLOCK_MS. Stops capture,
 * reconfigures the DMA, and leaves capture stopped (the caller restarts). */
int audio_set_block_ms(uint32_t ms);
/* Active audio block size, in ms and in PCM samples. */
uint32_t audio_get_block_ms(void);
uint32_t audio_get_block_samples(void);

/* ================= CONFIG ================= */
#define SAMPLE_RATE CONFIG_SAMPLING_RATE
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define READ_TIMEOUT 120

/* Audio DMA block granularity.
 *
 * The block size is selectable at runtime (audio_set_block_ms() / the `app
 * blkms` shell command), defaulting to CONFIG_AUDIO_BLOCK_MS. It must be a
 * multiple of AUDIO_BLOCK_STEP_MS (one MFCC hop = CONFIG_MFCC_SAMPLE_COUNT
 * samples = 20 ms at 16 kHz) so the downstream MFCC framing stays aligned, and
 * no larger than AUDIO_MAX_BLOCK_MS. The DMA mem-slab is statically sized for
 * the maximum, so a runtime change can never overrun the allocation. */
#define AUDIO_BLOCK_STEP_MS (CONFIG_MFCC_SAMPLE_COUNT * 1000 / SAMPLE_RATE) /* 20 ms */
#define AUDIO_BLOCK_MS_DEFAULT CONFIG_AUDIO_BLOCK_MS
#define AUDIO_MAX_BLOCK_MS CONFIG_AUDIO_MAX_BLOCK_MS

#define AUDIO_MS_TO_SAMPLES(_ms) ((SAMPLE_RATE) * (_ms) / 1000)
#define AUDIO_MS_TO_BYTES(_ms) (BYTES_PER_SAMPLE * AUDIO_MS_TO_SAMPLES(_ms))

/* Largest block the pipeline is sized for; all static allocations use this. */
#define MAX_BLOCK_SAMPLES AUDIO_MS_TO_SAMPLES(AUDIO_MAX_BLOCK_MS)
#define MAX_BLOCK_SIZE AUDIO_MS_TO_BYTES(AUDIO_MAX_BLOCK_MS)

/* Number of DMA buffers in the slab. Sized so that even at the smallest block
 * (AUDIO_BLOCK_STEP_MS) we retain ~AUDIO_TARGET_BUFFER_MS of buffered audio,
 * guarding against slab starvation at the faster capture cadence. */
#define AUDIO_TARGET_BUFFER_MS 240
#define BLOCK_COUNT (AUDIO_TARGET_BUFFER_MS / AUDIO_BLOCK_STEP_MS)

#define CAPTURE_STACK_SIZE 4096
#define CAPTURE_PRIORITY 3 /* highest */

struct audio_block {
    void* data;  /* PCM buffer pointer */
    size_t size; /* valid bytes in buffer */
};

/* Message queue declaration */
extern struct k_msgq audio_msgq;
extern struct k_mem_slab mem_slab;

#endif  //__PDM_MIC_H__