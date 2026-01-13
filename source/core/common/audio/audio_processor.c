#include "audio_processor.h"
#include "pdm_process.h"
#include "mfcc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "error.h"
#include <zephyr/sys/util.h>
#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include "akd_spi_flash_handler.h"


/** Number of channels in audio capture = 1, as it is stereo data */
#define N_CHANNELS_PER_SAMPLE (CONFIG_CHANNELS_PER_SAMPLE)
/** Number of bytes in each sample */
#define N_BYTES_PER_SAMPLE (2)
/** Number of captures per buffer */
#define N_CAP_PER_BUFF (2)
/** Flag indicating that streaming is ongoing */
static int stream = 0;
/** Flag used to notify that only one spectrogram should be acquired */
static int single_acquisition = 0;

/** Flag used to control verbose output */
static int verbose_on;
/** Flag used to check if the worqueue processing is done */
static uint8_t in_rx_processing;


/* Private data structure to maintain state */
typedef struct {
    int mfcc_len;                     /* Length of MFCC , derived by the application */
    int samplerate;             /* Samplerate at which Codec is configured */
    q7_t* spectrogram_buff;           /* output spectrogram buffer provided by application */
    int8_t spectrogram_len;           /* Spectrogram length */
    int8_t nmfcc;                     /* MFCC feature count */
    inference_cb_t inference_cb;      /* Callback function, invoked on a valid MFCC computation */
             /* local data structure to hold input to MFCC computation */
    int spectrogram_index;            /* Index of the circular spectrogram buffer */
} audio_processor_state_t;

__aligned(32) q7_t g_mfccdata [sizeof(q7_t) * SPECTROGRAM_RES];                   /* local data structure to hold MFCCs computed */
__aligned(32) int16_t g_mfcc_input [N_BYTES_PER_SAMPLE * BLOCK_SAMPLES * N_CHANNELS_PER_SAMPLE * N_CAP_PER_BUFF]; 
__aligned(32) int16_t orig_buf [N_BYTES_PER_SAMPLE * BLOCK_SAMPLES * N_CHANNELS_PER_SAMPLE * N_CAP_PER_BUFF];

static audio_processor_state_t _state;

static audio_processor_state_t* state = &_state;

static uint32_t proc_time = 0, disp_time = 0;

#ifdef CONFIG_AUDIO_CAPTURE_TEST
int is_capture_start =0;
#define BLOCK_SIZE_BYTES MAX_BLOCK_SIZE
#define TOTAL_BUFFER_BYTES CONFIG_SRAM_BUFFER_SIZE
#define TOTAL_BLOCKS (TOTAL_BUFFER_BYTES / BLOCK_SIZE_BYTES)
static uint32_t block_index = 0;
uint32_t offset = 0;
extern uint8_t sram_upload_buffer[];
#endif

/**
 * @brief Push an MFCC transform to the running spectrogram.
 *
 * @param data The MFCC data (expected to be mfcc_len )
 * @param spectrogram_index starting index of circular spectrogram buffer
 */
static int spectrogram_push(q7_t* data, int spectrogram_index) {
    q7_t* spectrogram_buff;
    spectrogram_buff = state->spectrogram_buff + (spectrogram_index * state->nmfcc);
    for (int i = 0; i < state->nmfcc; i++) {
        spectrogram_buff[i] = data[i];
    }

    spectrogram_index++;
    if (spectrogram_index >= state->spectrogram_len) {
        spectrogram_index = 0;
        if (single_acquisition) {
            stream = 0;
        }
    }
    return spectrogram_index;
}

/**
 * @brief This method calls generates the input buffer for
 * MFCC processing
 *
 * It takes care of overlapping the new sampled buffer with
 * the old one to create the required overlap for mfcc transform
 *
 * @param state local data structure maintained by audio processor
 * @param input the input buffer MFCC_SAMPLE_COUNT sized
 * @param mfcc_input the input buffer used for MFCC computation
 */

#define MFCC_HOP_SAMPLES   CONFIG_MFCC_SAMPLE_COUNT     // 20 ms
#define MFCC_WINDOW_SAMPLES   CONFIG_MFCC_SAMPLE_COUNT*2     // 40 ms


#define MFCC_PER_BLOCK     3

#if CONFIG_MFCC_METHOD==1

static void mfcc_process_input(const q15_t* input, int16_t* mfcc_input) {

    for (int i = 0; i < state->mfcc_len; i++) {
        mfcc_input[i] = mfcc_input[state->mfcc_len + i];
        mfcc_input[state->mfcc_len + i] = input[i];
    }

    for (int f = 0; f < MFCC_PER_BLOCK; f++) {

        mfcc_compute(&g_mfcc_input[f*MFCC_HOP_SAMPLES], g_mfccdata);
        state->spectrogram_index =
            spectrogram_push(g_mfccdata,
                              state->spectrogram_index);
    }
}


#elif CONFIG_MFCC_METHOD==2

// This persists in memory between PDM interrupts
static int16_t history_samples[MFCC_HOP_SAMPLES] = {0}; 
static int16_t processing_frame[MFCC_WINDOW_SAMPLES] = {0}; 


static void mfcc_process_input(const q15_t* input, int16_t* mfcc_input) {

    for (int i = 0; i < state->mfcc_len; i++) {
        mfcc_input[i] = mfcc_input[state->mfcc_len + i];
        mfcc_input[state->mfcc_len + i] = input[i];
    }
	

	for (int f = 0; f < MFCC_PER_BLOCK; f++) {
		if (f == 0) {
			// Frame 1: [320 samples from previous 60ms] + [Samples 0-320 of new 60ms]
			memcpy(processing_frame, history_samples, MFCC_HOP_SAMPLES * sizeof(int16_t));
			memcpy(&processing_frame[MFCC_HOP_SAMPLES], &mfcc_input[0], MFCC_HOP_SAMPLES * sizeof(int16_t));
		} 
		else if (f == 1) {
			// Frame 2: [Samples 0-320] + [Samples 320-640]
			memcpy(processing_frame, &mfcc_input[0], MFCC_HOP_SAMPLES * sizeof(int16_t));
			memcpy(&processing_frame[MFCC_HOP_SAMPLES], &mfcc_input[MFCC_HOP_SAMPLES], MFCC_HOP_SAMPLES * sizeof(int16_t));
		} 
		else {
			// Frame 3: [Samples 320-640] + [Samples 640-960]
			memcpy(processing_frame, &mfcc_input[MFCC_HOP_SAMPLES], MFCC_HOP_SAMPLES * sizeof(int16_t));
			memcpy(&processing_frame[MFCC_HOP_SAMPLES], &mfcc_input[2 * MFCC_HOP_SAMPLES], MFCC_HOP_SAMPLES * sizeof(int16_t));
		}	
		mfcc_compute(processing_frame, g_mfccdata);

		state->spectrogram_index = spectrogram_push(g_mfccdata, state->spectrogram_index);
    }	
	memcpy(history_samples, &mfcc_input[2 * MFCC_HOP_SAMPLES], MFCC_HOP_SAMPLES * sizeof(int16_t));
	

}

#else

static void mfcc_process_input(const q15_t* input,  int16_t* mfcc_input) {
    
    // Create a local buffer to stitch the overlap
    // [Old 320 samples] + [New 960 samples] = 1280 samples total
    static int16_t stream_buffer[MFCC_HOP_SAMPLES + 960]; 
    
    // 1. Move the last 320 samples of previous run to the start
    // (Already done at the end of the previous call)
    
    // 2. Copy NEW 960 samples into the rest of the buffer
    memcpy(&stream_buffer[MFCC_HOP_SAMPLES], input, 960 * sizeof(int16_t));

    // 3. Compute 3 MFCCs
    // Frame 0: 0-640 (Contains 320 old, 320 new)
    // Frame 1: 320-960
    // Frame 2: 640-1280
    for (int f = 0; f < MFCC_PER_BLOCK; f++) {
        mfcc_compute(&stream_buffer[f * MFCC_HOP_SAMPLES], g_mfccdata);
        state->spectrogram_index = spectrogram_push(g_mfccdata, state->spectrogram_index);
    }

    // 4. Save the LAST 320 samples of the CURRENT input for the NEXT call
    memcpy(&stream_buffer[0], &stream_buffer[960], MFCC_HOP_SAMPLES * sizeof(int16_t));
}

#endif


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

__aligned(32) static q15_t input[MAX_MFCC_LEN];
	uint32_t size = 0;
	
    int min = 128000;
    int max = -128000;

    /* Select only one channel */
    for (int i = 0; i <  state->mfcc_len; i++) {
        if (min > orig_buf[N_CHANNELS_PER_SAMPLE * i])
            min = orig_buf[N_CHANNELS_PER_SAMPLE * i];
        if (max < orig_buf[N_CHANNELS_PER_SAMPLE * i])
            max = orig_buf[N_CHANNELS_PER_SAMPLE * i];
        input[i] = orig_buf[N_CHANNELS_PER_SAMPLE * i];
    }

    /* Indicate if signal is too large */
    if(min < -32000 || max > 32000){
        printk("CLIP!!!! min %d max %d\r\n", min, max);
    }

    mfcc_process_input(input, g_mfcc_input);

    state->inference_cb(state->spectrogram_index);

    in_rx_processing = false;
	return SUCCESS;
}

int audio_processor_init(int samplerate) {
    /** Initialize codec */
    _state.samplerate = samplerate;
	//pdm_init();
    return SUCCESS;
}

int audio_processor_start(bool single, q7_t* spectrogram_buff, uint8_t* spectrogram_dims,
                          int mfcc_len, inference_cb_t cb) {

    single_acquisition = single;
    stream = 1;
    if (mfcc_len > MAX_MFCC_LEN) {
		printk(" invalid parameter \n\r ");
        return EFAILURE;
    }
    _state.mfcc_len = 960;
	_state.spectrogram_index = 0;

    _state.spectrogram_len = spectrogram_dims[0];
    _state.nmfcc = spectrogram_dims[1];
    _state.inference_cb = cb;
    _state.spectrogram_buff = spectrogram_buff;

    int ret = mfcc_init(_state.nmfcc, mfcc_len * 2, 1, (float)_state.samplerate);
	if (ret == EFAILURE)
	{
		printk("mfcc_init failure\n\r ");
		return EFAILURE;
	}


    in_rx_processing = false;


	return SUCCESS;
}

int audio_processor_stop() {
    bool is_processing = true;
    uint32_t flags = 0;
    stream = 0;

    //free(_state.mfccdata);
    //free(_state.mfcc_input);
    mfcc_deinit();

    return SUCCESS;
}


void audio_processor_set_verbose(int verbose_level) {
    verbose_on = verbose_level;
}

#ifdef CONFIG_AUDIO_CAPTURE_TEST
static inline void uart_send_pcm(const int16_t *pcm, size_t samples)
{

    for (size_t i = 0; i < samples; i++) {
		printk("%d,", pcm[i]);
		if( i%20==0)
			k_sleep(K_MSEC(10));
    }
}

static void capture_raw_samples(size_t samples){
	
	if(is_capture_start)
	{
		offset = block_index * BLOCK_SIZE_BYTES;

		memcpy(&sram_upload_buffer[offset], (uint8_t*)orig_buf, samples);

		block_index++;

		if (block_index >= TOTAL_BLOCKS) {
			block_index = 0;   /* wrap or stop capture */
			is_capture_start = 0;
			printk("\n\rcap stopped\n\r");
		}
	}
}
#endif

void audio_process_thread(void *a, void *b, void *c)
{
    struct audio_block blk;
    printk ("audio_process_thread:\n\r");
    uint32_t audio_process_thread_cntr = 0;
	size_t samples;
    while (1) {

        k_msgq_get(&audio_msgq, &blk, K_FOREVER);

		samples = blk.size / sizeof(int16_t);
#ifdef CONFIG_AUDIO_CAPTURE_TEST
		capture_raw_samples(blk.size);
#else	
        pdm_process(orig_buf, samples);  
		audio_processor();
#endif
		//prcess_led();
	    audio_process_thread_cntr++;
		//printk ("ap %d\n", audio_process_thread_cntr);
    }
}



#ifdef CONFIG_AUDIO_CAPTURE_TEST
int cmd_cap_start(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    printk("invalid command ");
    return -EINVAL;
  }
    printk("cap started ");
  is_capture_start = 1;
  return 0;
}

int cmd_cap_stop(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    printk("invalid command ");
    return -EINVAL;
  }
  printk("cap stopped ");
  is_capture_start = 0;
  return 0;
}


int cmd_dump_uart(const struct shell *shell, size_t argc, char **argv) {
  if (argc > 1) {
    printk("invalid command ");
    return -EINVAL;
  }
  uart_send_pcm((int16_t*)sram_upload_buffer, TOTAL_BUFFER_BYTES/2);
  printk("\n\rdump completed\n\r ");
  return 0;
}


SHELL_CMD_REGISTER(cap_start, NULL, "cap_start", cmd_cap_start);
SHELL_CMD_REGISTER(cap_stop, NULL, "cap_stop", cmd_cap_stop);
SHELL_CMD_REGISTER(dump_uart, NULL, "dump_uart", cmd_dump_uart);
#endif