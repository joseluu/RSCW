/* shim FFTW3 minimal pour compiler original-rscw-0.1e/rscw.c sans libfftw3.
 * N'implémente que ce que rscw.c utilise : fftw_plan_r2r_1d(n, in, out, FFTW_R2HC, ...).
 * Format « halfcomplex » de FFTW : out[k] = Re(X_k) pour 0<=k<=n/2, out[n-k] = Im(X_k) pour 0<k<n/2.
 * Calcul délégué à rscw_fft.h, la FFT du portage : référence et portage calculent les mêmes valeurs.
 */
#ifndef FFTW3_SHIM_H
#define FFTW3_SHIM_H
#include <stdlib.h>
#include "../../RSCW_Teensy/rscw_fft.h"

#define FFTW_R2HC 0
#define FFTW_ESTIMATE 0

typedef struct { int n; double *in, *out; } *fftw_plan;

static inline void *fftw_malloc(size_t s) { return malloc(s); }
static inline void fftw_free(void *p) { free(p); }

static inline fftw_plan fftw_plan_r2r_1d(int n, double *in, double *out, int kind, unsigned flags)
{
   fftw_plan p = (fftw_plan)malloc(sizeof(*p));
   (void)kind; (void)flags;
   p->n = n; p->in = in; p->out = out;
   return p;
}

static inline void fftw_destroy_plan(fftw_plan p) { free(p); }

static inline void fftw_execute(fftw_plan p)
{
   int n = p->n, k;
   double *z = (double *)malloc(n * sizeof(double));
   double *re = (double *)malloc((n / 2 + 1) * sizeof(double));
   double *im = (double *)malloc((n / 2 + 1) * sizeof(double));
   rscw_rfft(p->in, n, z, re, im);
   for (k = 0; k <= n / 2; k++) p->out[k] = re[k];
   for (k = 1; k < n / 2; k++) p->out[n - k] = im[k];
   free(z); free(re); free(im);
}
#endif
