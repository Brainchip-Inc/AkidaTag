#ifndef PDM_MIC_H_
#define PDM_MIC_H_


#ifdef __cplusplus
extern "C" {
#endif
    
#include <stdint.h>
    /* ================= CONFIG ================= */

#define SAMPLE_RATE        CONFIG_SAMPLING_RATE
#define SAMPLE_BIT_WIDTH   16
#define BYTES_PER_SAMPLE   sizeof(int16_t)
#define READ_TIMEOUT       120  /* ms */

/* 100 ms audio block */
#define BLOCK_SIZE(_rate, _ch) \
	(BYTES_PER_SAMPLE * ((_rate) / 10) * (_ch))

#define MAX_BLOCK_SIZE     BLOCK_SIZE(SAMPLE_RATE, 1)
#define BLOCK_COUNT        6

#define CAPTURE_STACK_SIZE 4096
#define CAPTURE_PRIORITY   3   /* highest */

/* ================= PUBLIC API ================= */

int dmic_rms_init(void);
int dmic_rms_start(void);
int dmic_rms_read(int16_t *rms_out);
void dmic_capture_thread(void *a, void *b, void *c);
#ifdef __cplusplus
}
#endif
#endif /* PDM_MIC_H_ */
