#ifndef __MFCC_H__
#define __MFCC_H__
#include <arm_math.h>
#define NUM_FBANK_BINS 40
#define MEL_LOW_FREQ 20
#define MEL_HIGH_FREQ 4000
/**
 * initialize mfcc transform
 *
 * @param features number of features extracted in the output
 * @param len length of the input buffers
 * @param dec_bits decimation
 *
 * @return 0 if success
 */
int mfcc_init(int features, int len, int dec_bits, float _samplerate);

/**
 * Release mfcc allocated memory
 */
void mfcc_deinit();

/**
 * Compute MFCC transform for a given audio buffer
 *
 * @param audio_data the input audio data
 * @param mfcc_out the mfcc output data
 */
void mfcc_compute(const int16_t *audio_data, float *mfcc_out);

#endif
