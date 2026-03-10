/*
 * Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * (see kiss_fft.h for full license text)
 */

#include "kiss_fftr.h"

/*struct kiss_fftr_state {
    kiss_fft_cfg substate;
    kiss_fft_cpx *tmpbuf;
    kiss_fft_cpx *super_twiddles;
};*/

kiss_fftr_cfg kiss_fftr_alloc(int nfft, int inverse_fft, void *mem,
                              size_t *lenmem) {
  int i;
  kiss_fftr_cfg st = NULL;
  size_t subsize, memneeded;

  if (nfft & 1) {
    fprintf(stderr, "Real FFT optimization must be even.\n");
    return NULL;
  }
  nfft >>= 1;

  kiss_fft_alloc(nfft, inverse_fft, NULL, &subsize);
  memneeded = sizeof(struct kiss_fftr_state) + subsize +
              sizeof(kiss_fft_cpx) * (nfft * 3 / 2);

  if (lenmem == NULL) {
    st = (kiss_fftr_cfg)malloc(memneeded);
  } else {
    if (mem != NULL && *lenmem >= memneeded)
      st = (kiss_fftr_cfg)mem;
    *lenmem = memneeded;
  }
  if (!st)
    return NULL;

  st->substate = (kiss_fft_cfg)(st + 1);
  st->tmpbuf = (kiss_fft_cpx *)((char *)(st + 1) + subsize);
  st->super_twiddles = st->tmpbuf + nfft;

  kiss_fft_alloc(nfft, inverse_fft, st->substate, &subsize);

  for (i = 0; i < nfft / 2; ++i) {
    double phase = -3.14159265358979323846 * ((double)(i + 1) / nfft + .5);
    if (inverse_fft)
      phase *= -1;
    kf_cexp(st->super_twiddles[i], (float)phase);
  }
  return st;
}

void kiss_fftr(kiss_fftr_cfg st, const kiss_fft_scalar *timedata,
               kiss_fft_cpx *freqdata) {
  /* input buffer timedata is stored row-wise */
  int k, ncfft;
  kiss_fft_cpx fpnk, fpk, f1k, f2k, tw, tdc;

  if (!st)
    return;

  ncfft = st->substate->nfft;

  /* perform the parallel fft of two real signals packed in real,imag */
  kiss_fft(st->substate, (const kiss_fft_cpx *)timedata, st->tmpbuf);

  /* The real part of the DC element of the frequency spectrum in st->tmpbuf
   * contains the sum of the even-numbered elements of the input time sequence
   * The move part of the DC element the negative sum of the odd-numbered
   * elements */
  tdc.r = st->tmpbuf[0].r;
  tdc.i = st->tmpbuf[0].i;
  C_FIXDIV(tdc, 2);
  freqdata[0].r = tdc.r + tdc.i;
  freqdata[ncfft].r = tdc.r - tdc.i;
  freqdata[ncfft].i = freqdata[0].i = 0;

  for (k = 1; k <= ncfft / 2; ++k) {
    fpk = st->tmpbuf[k];
    fpnk.r = st->tmpbuf[ncfft - k].r;
    fpnk.i = -st->tmpbuf[ncfft - k].i;
    C_FIXDIV(fpk, 2);
    C_FIXDIV(fpnk, 2);

    C_ADD(f1k, fpk, fpnk);
    C_SUB(f2k, fpk, fpnk);
    C_MUL(tw, f2k, st->super_twiddles[k - 1]);

    freqdata[k].r = HALF_OF(f1k.r + tw.r);
    freqdata[k].i = HALF_OF(f1k.i + tw.i);
    freqdata[ncfft - k].r = HALF_OF(f1k.r - tw.r);
    freqdata[ncfft - k].i = HALF_OF(tw.i - f1k.i);
  }
}

void kiss_fftri(kiss_fftr_cfg st, const kiss_fft_cpx *freqdata,
                kiss_fft_scalar *timedata) {
  int k, ncfft;

  if (!st)
    return;

  ncfft = st->substate->nfft;

  st->tmpbuf[0].r = freqdata[0].r + freqdata[ncfft].r;
  st->tmpbuf[0].i = freqdata[0].r - freqdata[ncfft].r;
  C_FIXDIV(st->tmpbuf[0], 2);

  for (k = 1; k <= ncfft / 2; ++k) {
    kiss_fft_cpx fk, fnkc, fek, fok, tmp;
    fk = freqdata[k];
    fnkc.r = freqdata[ncfft - k].r;
    fnkc.i = -freqdata[ncfft - k].i;
    C_FIXDIV(fk, 2);
    C_FIXDIV(fnkc, 2);

    C_ADD(fek, fk, fnkc);
    C_SUB(tmp, fk, fnkc);
    C_MUL(fok, tmp, st->super_twiddles[k - 1]);
    C_ADD(st->tmpbuf[k], fek, fok);
    C_SUB(st->tmpbuf[ncfft - k], fek, fok);
    st->tmpbuf[ncfft - k].i *= -1;
  }
  kiss_fft(st->substate, st->tmpbuf, (kiss_fft_cpx *)timedata);
}
