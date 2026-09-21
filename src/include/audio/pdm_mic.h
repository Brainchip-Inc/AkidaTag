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

/* Set the PDM microphone gain at runtime, in GAINL/GAINR register steps.
 * Stops capture, reconfigures the DMIC and leaves capture stopped (the
 * caller restarts), the same contract as audio_set_block_ms(). */
int audio_set_mic_gain(uint8_t gain);
/* Gain the DMIC is currently configured with. */
uint8_t audio_get_mic_gain(void);

/* ================= CONFIG ================= */
#define SAMPLE_RATE CONFIG_SAMPLING_RATE
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define READ_TIMEOUT 120

/* PDM microphone gain, in nRF5340 GAINL/GAINR register steps of 0.5 dB, where
 * 0x00 is -20 dB, 0x28 is 0 dB and 0x50 is +20 dB.
 *
 * 0x48 is +16 dB, which lines this board's full-scale point up with the Nicla
 * Vision keyword demo that runs the same model: both then clip at 114 dB SPL.
 * The 16 dB is 10 dB of microphone, this board's IM69D130 being -36 dBFS
 * against the Nicla's MP34DT06J at -26 dBFS, plus the 6 dB of gain that demo
 * applies and this one did not. Measured back to back against one played
 * keyword set: 0 of 10 detected at 0x28 and 8 of 10 at 0x48, nothing clipping,
 * and the acoustic level scaling by 15.6 dB against the 16.0 dB predicted.
 *
 * The noise floor does not follow, because at roughly 237 counts it is fixed
 * board noise rather than sound, so it stays well clear of the speech gate in
 * kws_config.c and that gate needs no matching change.
 */
#define AUDIO_MIC_GAIN_MIN 0x00
#define AUDIO_MIC_GAIN_MAX 0x50
#define AUDIO_MIC_GAIN_DEFAULT 0x48

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