#include "audio_processor.h"
#include "akd_spi_flash_handler.h"
#include "error.h"
#include "mfcc.h"
#include "pdm_mic.h"
#if IS_ENABLED(CONFIG_WDT_ENABLE)
#include "watchdog_h/watchdog.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(audio_processor, LOG_LEVEL_DBG);

#define RMS_THRESHOLD 550
int rms_threshold = RMS_THRESHOLD;
#define SPEECH_IDLE 0
#define SPEECH_ACTIVE 1
int speech_active_time_ms = 1300;
static atomic_t g_inference_period = 3;
static int g_frames_since_reset = 0;
/** Number of channels in audio capture = 1, as it is stereo data */
#define N_CHANNELS_PER_SAMPLE (CONFIG_CHANNELS_PER_SAMPLE)
/** Number of bytes in each sample */
#define N_BYTES_PER_SAMPLE (2)
/** Number of captures per buffer */
#define N_CAP_PER_BUFF (2)

/** Flag used to control verbose output */
extern int verbose_on;

/* Private data structure to maintain state */
typedef struct {
    int mfcc_len;                /* Length of MFCC , derived by the application */
    int samplerate;              /* Samplerate at which Codec is configured */
    float* spectrogram_buff;     /* output spectrogram buffer provided by application */
    int8_t spectrogram_len;      /* Spectrogram length */
    int8_t nmfcc;                /* MFCC feature count */
    inference_cb_t inference_cb; /* Callback function, invoked on a valid MFCC computation */
                                 /* local data structure to hold input to MFCC computation */
    int spectrogram_index;       /* Index of the circular spectrogram buffer */
} audio_processor_state_t;

__aligned(32) float g_mfccdata[SPECTROGRAM_RES]; /* local data structure to hold
                                                    MFCCs computed */
/* Sized for the largest supported block (MAX_BLOCK_SAMPLES) so a runtime
 * `app blkms` change never overruns these buffers. g_mfcc_input additionally
 * holds one hop of overlap from the previous block. */
__aligned(32) int16_t
    g_mfcc_input[N_BYTES_PER_SAMPLE * MAX_BLOCK_SAMPLES * N_CHANNELS_PER_SAMPLE * N_CAP_PER_BUFF];
__aligned(32) int16_t
    orig_buf[N_BYTES_PER_SAMPLE * MAX_BLOCK_SAMPLES * N_CHANNELS_PER_SAMPLE * N_CAP_PER_BUFF];

static audio_processor_state_t _state;

static audio_processor_state_t* state = &_state;

#ifdef CONFIG_AUDIO_CAPTURE_TEST
int is_capture_start = 0;
#define BLOCK_SIZE_BYTES MAX_BLOCK_SIZE
#define TOTAL_BUFFER_BYTES CONFIG_SRAM_BUFFER_SIZE
#define TOTAL_BLOCKS (TOTAL_BUFFER_BYTES / BLOCK_SIZE_BYTES)
static uint32_t block_index = 0;
uint32_t offset = 0;
/* Raw PCM is captured here and dumped over UART afterwards. Only compiled for
 * this debug option, which is off in every shipped build. */
static __noinit uint8_t raw_capture_buffer[TOTAL_BUFFER_BYTES];
#endif
extern uint8_t is_kws_debounce_complete(void);
extern uint32_t kws_debounce_time;
extern void set_feature_buff_full(void);
extern uint8_t is_feature_buff_full(void);
extern void reset_stale_inference_data(void);
extern int64_t time_ms();

static uint32_t ap_counter = 0;

void reset_spectrogram_index(void) {
    state->spectrogram_index = 0;
    g_frames_since_reset = 0;
    ap_counter = 0;
}

/**
 * @brief Push an MFCC transform to the running spectrogram.
 *
 * @param data The MFCC data (expected to be mfcc_len )
 * @param spectrogram_index starting index of circular spectrogram buffer
 */
static int spectrogram_push(float* data, int spectrogram_index) {
    float* spectrogram_buff;
    spectrogram_buff = state->spectrogram_buff + (spectrogram_index * state->nmfcc);
    for (int i = 0; i < state->nmfcc; i++) {
        spectrogram_buff[i] = data[i];
    }

    spectrogram_index++;
    g_frames_since_reset++;
    if (spectrogram_index >= state->spectrogram_len) {
        set_feature_buff_full();
        spectrogram_index = 0;
    }
    return spectrogram_index;
}

/**
 * @brief This method calls generates the input buffer for
 * MFCC processing
 *
 * Processes streaming audio samples and computes MFCC features using
 * overlapping frames. The function maintains a rolling buffer so that each call
 * produces multiple MFCC frames while preserving overlap between consecutive
 * calls.
 *
 * @param state local data structure maintained by audio processor
 * @param input the input buffer MFCC_SAMPLE_COUNT sized
 * @param mfcc_input the input buffer used for MFCC computation
 */

#define MFCC_HOP_SAMPLES CONFIG_MFCC_SAMPLE_COUNT         // 20 ms
#define MFCC_WINDOW_SAMPLES CONFIG_MFCC_SAMPLE_COUNT * 2  // 40 ms (2 hops)

/* Number of MFCC frames produced from one DMA block. The block size is chosen
 * at runtime (audio_set_block_ms) and is always a multiple of the hop, so this
 * is exact: e.g. 1 frame @20 ms, 2 @40 ms, 3 @60 ms, 4 @80 ms. */
static inline int mfcc_frames_per_block(void) {
    return (int)(audio_get_block_samples() / MFCC_HOP_SAMPLES);
}

/* Turn one DMA block of `block_samples` PCM samples into MFCC frames.
 *
 * mfcc_input layout: [0 .. hop) holds the last hop retained from the previous
 * block; [hop .. hop+block_samples) holds this block. Each MFCC window is 2
 * hops wide and steps by one hop, so a block of N hops yields N frames and the
 * final window ends exactly at the buffer end. The last hop of this block is
 * then retained as the overlap for the next call. Works for any block size
 * that is a whole number of hops. */
static void mfcc_process_input(const int16_t* input, int16_t* mfcc_input, int block_samples) {
    const int frames = block_samples / MFCC_HOP_SAMPLES;

    memcpy(&mfcc_input[MFCC_HOP_SAMPLES], input, block_samples * sizeof(int16_t));

    for (int f = 0; f < frames; f++) {
        mfcc_compute(&mfcc_input[f * MFCC_HOP_SAMPLES], g_mfccdata);
        state->spectrogram_index = spectrogram_push(g_mfccdata, state->spectrogram_index);
    }

    memcpy(&mfcc_input[0], &mfcc_input[block_samples], MFCC_HOP_SAMPLES * sizeof(int16_t));
}

/**
 * @brief work function
 *
 * This function is called each time an i2s end of transfer interrupt happens.
 * It calculates basic statistics on the acquired buffer, and call the mfcc
 * processing function
 *
 * @param w the work item
 */

int audio_processor(void) {
    __aligned(32) static int16_t input[MAX_BLOCK_SAMPLES];

    const int block_samples = (int)audio_get_block_samples();
    int min = 128000;
    int max = -128000;

    /* Select only one channel */
    for (int i = 0; i < block_samples; i++) {
        if (min > orig_buf[N_CHANNELS_PER_SAMPLE * i])
            min = orig_buf[N_CHANNELS_PER_SAMPLE * i];
        if (max < orig_buf[N_CHANNELS_PER_SAMPLE * i])
            max = orig_buf[N_CHANNELS_PER_SAMPLE * i];
        input[i] = orig_buf[N_CHANNELS_PER_SAMPLE * i];
    }

    /* Indicate if signal is too large */
    if (min < -32000 || max > 32000) {
        LOG_WRN("CLIP!!!! min %d max %d", min, max);
    }

    mfcc_process_input(input, g_mfcc_input, block_samples);

    if (verbose_on) {
        LOG_INF("mfcc: %d frames computed, buffer %d/%d", mfcc_frames_per_block(),
                state->spectrogram_index, state->spectrogram_len);
    }

    ap_counter++;
    if ((ap_counter % g_inference_period) == 0) {
        if (verbose_on) {
            LOG_INF("inference: starting (spec_idx=%d)", state->spectrogram_index);
        }
        state->inference_cb(state->spectrogram_index);
    }

    return SUCCESS;
}

int audio_processor_init(int samplerate) {
    /** Initialize codec */
    _state.samplerate = samplerate;
    // pdm_init();
    return SUCCESS;
}

int audio_processor_start(bool single, float* spectrogram_buff, uint8_t* spectrogram_dims,
                          int mfcc_hop_len, inference_cb_t cb) {
    if (mfcc_hop_len > MAX_MFCC_LEN) {
        LOG_ERR(" invalid parameter  ");
        return EFAILURE;
    }
    /* MFCC hop length in samples (e.g. 320 = 20 ms). The number of samples the
     * processor consumes per call now follows the runtime block size
     * (audio_get_block_samples()), so this field is informational only. */
    _state.mfcc_len = mfcc_hop_len;
    _state.spectrogram_index = 0;

    _state.spectrogram_len = spectrogram_dims[0];
    _state.nmfcc = spectrogram_dims[1];
    _state.inference_cb = cb;
    _state.spectrogram_buff = spectrogram_buff;

    int ret = mfcc_init(_state.nmfcc, mfcc_hop_len * 2, (float)_state.samplerate);
    if (ret == EFAILURE) {
        LOG_ERR("mfcc_init failure ");
        return EFAILURE;
    }

    return SUCCESS;
}

/* MFCC frames produced between consecutive inference callbacks. Inference fires
 * every g_inference_period blocks, and each block yields mfcc_frames_per_block()
 * frames, so this stays exact for any block size (used by the learning path to
 * copy the most-recent frames from the spectrogram). */
int get_audio_frames_cb(void) {
    return mfcc_frames_per_block() * g_inference_period;
}

int audio_processor_stop() {
    mfcc_deinit();

    return SUCCESS;
}

#ifdef CONFIG_AUDIO_CAPTURE_TEST
static inline void uart_send_pcm(const int16_t* pcm, size_t samples) {
    for (size_t i = 0; i < samples; i++) {
        LOG_PRINTK("%d,", pcm[i]);
        if (i % 20 == 0)
            k_sleep(K_MSEC(10));
    }
}

static void capture_raw_samples(size_t samples) {
    if (is_capture_start) {
        offset = block_index * BLOCK_SIZE_BYTES;

        memcpy(&raw_capture_buffer[offset], (uint8_t*)orig_buf, samples);

        block_index++;

        if (block_index >= TOTAL_BLOCKS) {
            block_index = 0; /* wrap or stop capture */
            is_capture_start = 0;
            LOG_INF("cap stopped");
        }
    }
}
#endif

/* This thread is responsible to process the received DMIC sample and then
 * trigger the inference  */
void audio_process_thread(void* a, void* b, void* c) {
#if IS_ENABLED(CONFIG_WDT_ENABLE)
    wdt_enable_thread(AUDIO_PROCESS);
#endif
    struct audio_block blk;
    LOG_INF("audio_process_thread:");
    uint32_t audio_process_thread_cntr = 0;
    size_t samples;
    float rms_val = 0.0f;
    int speech_state = SPEECH_IDLE;
    uint64_t speech_start_time = 0;
    bool was_in_debounce = false;
    while (1) {
#if IS_ENABLED(CONFIG_WDT_ENABLE)
        /* Mark thread as healthy */
        atomic_set(&thread_health[AUDIO_PROCESS], 1);
#endif
        /* Wait for message with timeout to prevent thread from blocking forever,
         * allowing periodic WDT feeding even when no data is received */
        if (k_msgq_get(&audio_msgq, &blk, K_MSEC(READ_TIMEOUT)) == 0) {
            samples = blk.size / sizeof(int16_t);

#ifdef CONFIG_AUDIO_CAPTURE_TEST
            capture_raw_samples(blk.size);
#else

            if (!is_kws_debounce_complete()) {
                /* Debounce cooldown active: skip all processing */
                speech_state = SPEECH_IDLE;
                if (verbose_on && !was_in_debounce) {
                    LOG_INF("debounce: %ums cooldown active", kws_debounce_time);
                }
                was_in_debounce = true;
            }

            /* remove DC offset and compute RMS based on compute_rms, flag */
            else {
                if (was_in_debounce && verbose_on) {
                    LOG_INF("debounce: cooldown complete");
                }
                was_in_debounce = false;
                if (SUCCESS == dmic_process(orig_buf, samples, &rms_val)) {
                    /* Level of every block, gated or not. The idle line below only
                     * fires under the threshold, so this is the one way to see what
                     * speech actually reaches, which is what the mic gain is set
                     * against. */
                    if (verbose_on >= 3) {
                        LOG_INF("block: rms=%.0f", (double)rms_val);
                    }
                    /* ok to lose fraction part resolution, comparing with int value only
                     */
                    if (((int)rms_val >= rms_threshold)) {
                        if (verbose_on && speech_state == SPEECH_IDLE) {
                            LOG_INF("speech: ACTIVE (rms=%.0f >= %d)", (double)rms_val,
                                    rms_threshold);
                        }
                        speech_state = SPEECH_ACTIVE;
                        speech_start_time = time_ms();

                    } else if (speech_state == SPEECH_IDLE) {
                        if (verbose_on >= 2) {
                            LOG_INF("speech: idle (rms=%.0f)", (double)rms_val);
                        }
                        /* do not process as state is idle */
                        continue;
                    } else if ((time_ms() - speech_start_time) > speech_active_time_ms) {
                        /* If the speech state is active and control reaches this point, it
                         * means that the rms_val has remained below the threshold for
                         * speech_active_time_ms. This indicates that no valid speech
                         * command was detected. Therefore, the system transitions back to
                         * the IDLE state and clears any stale inference data */
                        if (verbose_on) {
                            LOG_INF("speech: IDLE (rms=%.0f, timeout %dms)", (double)rms_val,
                                    speech_active_time_ms);
                        }
                        speech_state = SPEECH_IDLE;
                        reset_stale_inference_data();
                        continue;
                    }
                    audio_processor();
                }
            }

            audio_process_thread_cntr++;
#endif
        }
    }
}

int cmd_inf_period(const struct shell* shell, size_t argc, char** argv) {
    if (argc > 1) {
        char* endptr;
        errno = 0;

        uint32_t val1 = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || errno == ERANGE || val1 > 3 || val1 < 1) {
            shell_error(shell, "Invalid input: ");
            return -EINVAL;
        }
        g_inference_period = val1;
        LOG_INF("g_inference_period %d ", val1);
    } else {
        LOG_ERR("incorrect command ");
    }
    return 0;
}

#ifdef CONFIG_AUDIO_CAPTURE_TEST
int cmd_cap_start(const struct shell* shell, size_t argc, char** argv) {
    if (argc > 1) {
        LOG_ERR("invalid command ");
        return -EINVAL;
    }
    LOG_INF("cap started ");
    is_capture_start = 1;
    return 0;
}

int cmd_cap_stop(const struct shell* shell, size_t argc, char** argv) {
    if (argc > 1) {
        LOG_ERR("invalid command ");
        return -EINVAL;
    }
    LOG_INF("cap stopped ");
    is_capture_start = 0;
    return 0;
}

int cmd_dump_uart(const struct shell* shell, size_t argc, char** argv) {
    if (argc > 1) {
        LOG_ERR("invalid command ");
        return -EINVAL;
    }
    uart_send_pcm((int16_t*)raw_capture_buffer, TOTAL_BUFFER_BYTES / 2);
    LOG_INF("dump completed ");
    return 0;
}

SHELL_CMD_REGISTER(cap_start, NULL, "cap_start", cmd_cap_start);
SHELL_CMD_REGISTER(cap_stop, NULL, "cap_stop", cmd_cap_stop);
SHELL_CMD_REGISTER(dump_uart, NULL, "dump_uart", cmd_dump_uart);
#endif

SHELL_CMD_REGISTER(inf_period, NULL, "inf_period", cmd_inf_period);
