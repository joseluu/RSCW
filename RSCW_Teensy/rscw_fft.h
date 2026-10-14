/* rscw_fft.h — FFT réelle radix-2 en double, sans dépendance.
 *
 * Remplace FFTW dans le portage de RSCW (search_carrier()). Utilisée aussi par le shim FFTW
 * de host/shim/fftw3.h pour compiler le rscw.c d'origine : les deux calculent donc exactement
 * les mêmes valeurs, ce qui permet le test d'équivalence bit à bit du texte décodé.
 *
 * rscw_rfft(in, n, z, re, im) : n réels in[0..n-1] (n puissance de 2, n >= 4)
 *   → re[k] + j·im[k] = X_k = sum in[t]·exp(-2πj·k·t/n), pour k = 0..n/2.
 *   z : tableau de travail de n doubles (n/2 complexes entrelacés).
 * Méthode : FFT complexe de n/2 points sur les paires (pair, impair), puis séparation.
 */
#ifndef RSCW_FFT_H
#define RSCW_FFT_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline void rscw_cfft(double *z, int m)   /* FFT complexe directe en place, m points */
{
   int i, j, len, k;
   for (i = 1, j = 0; i < m; i++) {               /* permutation bit-reverse */
      int bit = m >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
         double t;
         t = z[2*i]; z[2*i] = z[2*j]; z[2*j] = t;
         t = z[2*i+1]; z[2*i+1] = z[2*j+1]; z[2*j+1] = t;
      }
   }
   for (len = 2; len <= m; len <<= 1) {
      double ang = -2 * M_PI / len;
      int h = len / 2;
      for (k = 0; k < h; k++) {
         double wr = cos(ang * k), wi = sin(ang * k);
         for (i = k; i < m; i += len) {
            double *a = z + 2*i, *b = z + 2*(i + h);
            double xr = b[0] * wr - b[1] * wi;
            double xi = b[0] * wi + b[1] * wr;
            b[0] = a[0] - xr; b[1] = a[1] - xi;
            a[0] += xr;       a[1] += xi;
         }
      }
   }
}

static inline void rscw_rfft(const double *in, int n, double *z, double *re, double *im)
{
   int m = n / 2, k;
   for (k = 0; k < m; k++) { z[2*k] = in[2*k]; z[2*k+1] = in[2*k+1]; }
   rscw_cfft(z, m);
   /* X_k = E_k + W^k·O_k, avec E_k = (Z_k + conj(Z_{m-k}))/2, O_k = (Z_k − conj(Z_{m-k}))/(2j) */
   for (k = 0; k <= m; k++) {
      int k1 = (k == m) ? 0 : k;
      int k2 = (k == 0 || k == m) ? 0 : m - k;
      double zr = z[2*k1], zi = z[2*k1+1];
      double cr = z[2*k2], ci = -z[2*k2+1];          /* conj(Z_{m-k}) */
      double er = (zr + cr) / 2, ei = (zi + ci) / 2;
      double dr = (zr - cr) / 2, di = (zi - ci) / 2;
      double orr = di, oi = -dr;                      /* (d)/j */
      double ang = -2 * M_PI * k / n;
      double wr = cos(ang), wi = sin(ang);
      re[k] = er + (orr * wr - oi * wi);
      im[k] = ei + (orr * wi + oi * wr);
   }
}

#endif
