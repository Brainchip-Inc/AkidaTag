#ifndef __PDM_PROCESS_H__
#define __PDM_PROCESS_H__
#include <stdint.h>
#include <zephyr/kernel.h>
//int pdm_read(uint32_t *buffer, uint32_t * size);
int pdm_process(uint16_t *passed_buffer, uint32_t passed_size);
int pdm_init(void);
void dmic_capture_thread(void *a, void *b, void *c);
void stop_dmic (void);


#define SAMPLE_RATE       CONFIG_SAMPLING_RATE
#define SAMPLE_BIT_WIDTH  16
#define BYTES_PER_SAMPLE  sizeof(int16_t)
#define READ_TIMEOUT      120

#define BLOCK_MS          60   /* 60 ms window */

/* Number of PCM samples per block */
#define BLOCK_SAMPLES \
    (SAMPLE_RATE * BLOCK_MS / 1000)   /* 16000 * 60 / 1000 = 960 samples */

/* Bytes per block */
#define BLOCK_SIZE(_rate, _ch) \
    (BYTES_PER_SAMPLE * BLOCK_SAMPLES * _ch)

/* Single-channel audio */
#define MAX_BLOCK_SIZE    BLOCK_SIZE(SAMPLE_RATE, 1)   /* 960 * 2 = 1920 bytes */

/* Number of buffers in the slab */
#define BLOCK_COUNT       6

#define CAPTURE_STACK_SIZE 4096
#define CAPTURE_PRIORITY   3   /* highest */

struct audio_block {
    void   *data;   /* PCM buffer pointer */
    size_t  size;   /* valid bytes in buffer */
};

/* Message queue declaration */
extern struct k_msgq audio_msgq;
extern struct k_mem_slab mem_slab;


#endif //__PDM_PROCESS_H__