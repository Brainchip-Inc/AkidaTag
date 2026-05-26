#ifndef __AUDIO_PROCESSOR_H__
#define __AUDIO_PROCESSOR_H__
#include <stdbool.h>
#include <stdint.h>

#define MAX_MFCC_LEN 1024
/**
 * Callback function invoked on a valid MFCC frame
 *
 * @param spectrogram_index index of circular spectrogram buffer.
 */
typedef void (*inference_cb_t)(int spectrogram_index);

int audio_processor(void);

/**
 * @brief Initialize audio processor
 *
 * @param conf Data structure to hold configuration parameters
 */
int audio_processor_init(int samplerate);

/**
 * @brief Start i2s streaming process
 *
 * @param is_stream true if audio stream is needed, else false
 * @param spectrogram_buff output spectrogram buffer
 * @param spectrogram_dims spectrogram buffer dimensions
 * @param mfcc_len  length of mfcc, which effects audio buffer length
 * @param cb callback function pointer invoked on a valid mfcc
 */
int audio_processor_start(bool is_stream, float *spectrogram_buff,
                          uint8_t *spectrogram_dims, int mfcc_len,
                          inference_cb_t cb);

/**
 * @brief Stop audio stream, Releases allocated memory
 *
 */
int audio_processor_stop();

int get_audio_frames_cb();

void audio_process_thread(void *a, void *b, void *c);

#ifdef CONFIG_SPARK_BOARD
/* Bench mode: drive one inference-callback tick without touching audio
 * samples or MFCC state. Respects the same g_inference_period gating used
 * by the normal audio_processor() path so the inference cadence matches
 * real operation. Called from audio_process_thread when
 * inference_bench_active() is true. */
void audio_processor_bench_tick(void);
#endif

#define PROCESS_STACK_SIZE 4096
#define PROCESS_PRIORITY 7
#define SPECTROGRAM_COUNT 49
#define SPECTROGRAM_RES 10

// Configurable audio processor parameters
extern int rms_threshold;
extern int speech_active_time_ms;

#endif
