/*
 * Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *     * Neither the author nor the names of any contributors may be used to
 *       endorse or promote products derived from this software without specific
 *       prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef KISS_FFT_H
#define KISS_FFT_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef kiss_fft_scalar
#define kiss_fft_scalar float
#endif

typedef struct {
  kiss_fft_scalar r;
  kiss_fft_scalar i;
} kiss_fft_cpx;

#define MAXFACTORS 32
struct kiss_fft_state {
  int nfft;
  int inverse;
  int factors[2 * MAXFACTORS];
  kiss_fft_cpx twiddles[1]; /* flexible array */
};

typedef struct kiss_fft_state *kiss_fft_cfg;

/**
 * kiss_fft_alloc
 *
 * Initialize a FFT (or IFFT) algorithm's cfg/state buffer.
 *
 * typical usage: kiss_fft_cfg mycfg=kiss_fft_alloc(1024,0,NULL,NULL);
 *
 * The return value from fft_alloc is a cfg buffer used internally
 * by the fft routine or NULL.
 *
 * If lenmem is NULL, then kiss_fft_alloc will allocate a cfg buffer using
 * malloc. The returned value should be free()d when done to avoid memory leaks.
 *
 * The state can be placed in a user supplied buffer 'mem':
 * If lenmem is not NULL and mem is not NULL and *lenmem is large enough,
 *    then the function places the cfg in mem and the size used in *lenmem
 *    and returns mem.
 *
 * If lenmem is not NULL and ( mem is NULL or *lenmem is not large enough),
 *    then the function returns NULL and places the minimum cfg
 *    buffer size in *lenmem.
 */
kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void *mem,
                            size_t *lenmem);

/**
 * kiss_fft(cfg,in_out_buf)
 *
 * Perform an FFT on a complex input buffer.
 * for a forward FFT,
 * fin should be  f[0] , f[1] , ... ,f[nfft-1]
 * fout will be   F[0] , F[1] , ... ,F[nfft-1]
 * Note that each element is complex and can be accessed like
 *    f[k].r and f[k].i
 */
void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx *fin, kiss_fft_cpx *fout);

/**
 * A more generic version of the above function. It reads its input from
 * every Nth sample.
 */
void kiss_fft_stride(kiss_fft_cfg cfg, const kiss_fft_cpx *fin,
                     kiss_fft_cpx *fout, int fin_stride);

/** If kiss_fft_alloc allocated a buffer, it is one contiguous
 *  buffer and can be simply free()d when no longer needed */
#define kiss_fft_free(p) free(p)

/**
 * Returns the smallest integer k, such that k>=n and k has only "fast" factors
 * (2,3,5)
 */
int kiss_fft_next_fast_size(int n);

/* the guts header contains all the multiplication and addition macros that
 * are defined for fixed or floating point complex numbers. It also declares
 * the kf_ internal functions.
 */

#define S_MUL(a, b) ((a) * (b))
#define C_MUL(m, a, b)                                                         \
  do {                                                                         \
    (m).r = (a).r * (b).r - (a).i * (b).i;                                     \
    (m).i = (a).r * (b).i + (a).i * (b).r;                                     \
  } while (0)
#define C_MULBYSCALAR(c, s)                                                    \
  do {                                                                         \
    (c).r *= (s);                                                              \
    (c).i *= (s);                                                              \
  } while (0)
#define C_FIXDIV(c, div) /* NOOP for float */
#define C_ADD(res, a, b)                                                       \
  do {                                                                         \
    (res).r = (a).r + (b).r;                                                   \
    (res).i = (a).i + (b).i;                                                   \
  } while (0)
#define C_SUB(res, a, b)                                                       \
  do {                                                                         \
    (res).r = (a).r - (b).r;                                                   \
    (res).i = (a).i - (b).i;                                                   \
  } while (0)
#define HALF_OF(x) ((x)*.5f)
#define kf_cexp(x, phase)                                                      \
  do {                                                                         \
    (x).r = cosf(phase);                                                       \
    (x).i = sinf(phase);                                                       \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* KISS_FFT_H */
