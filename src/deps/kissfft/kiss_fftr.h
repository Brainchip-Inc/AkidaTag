/*
 * Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * (see kiss_fft.h for full license text)
 */

#ifndef KISS_FFTR_H
#define KISS_FFTR_H

#include "kiss_fft.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Real optimized version can save about 45% cpu time vs. the complex version
 * (with optimization flags).
 *
 * nfft must be even.
 *
 * If you don't want to allocate a cfg buffer and free it later, use
 * mem and lenmem mechanism.
 */
struct kiss_fftr_state {
  kiss_fft_cfg substate;
  kiss_fft_cpx *tmpbuf;
  kiss_fft_cpx *super_twiddles;
};

typedef struct kiss_fftr_state *kiss_fftr_cfg;

kiss_fftr_cfg kiss_fftr_alloc(int nfft, int inverse_fft, void *mem,
                              size_t *lenmem);

/*
 * nfft/2+1 complex frequency bins are returned in freqdata.
 * input timedata has nfft scalar points.
 */
void kiss_fftr(kiss_fftr_cfg cfg, const kiss_fft_scalar *timedata,
               kiss_fft_cpx *freqdata);

/*
 * input freqdata has nfft/2+1 complex points.
 * output timedata has nfft scalar points.
 */
void kiss_fftri(kiss_fftr_cfg cfg, const kiss_fft_cpx *freqdata,
                kiss_fft_scalar *timedata);

#define kiss_fftr_free(p) free(p)

#ifdef __cplusplus
}
#endif

#endif /* KISS_FFTR_H */
