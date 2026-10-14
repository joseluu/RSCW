/* rscw_decim.h — décimateur FIR (passe-bas à fenêtre de Kaiser) et file SPSC, portables.
 *
 * Utilisés par AudioRSCW_F32 pour ramener l'audio I2S (48 kHz) aux 8 kHz qu'attend RSCW, et pour
 * passer les échantillons de l'interruption audio à loop(). Testés sur PC par host/decim_test.cpp.
 */
#ifndef RSCW_DECIM_H
#define RSCW_DECIM_H

#include <math.h>
#include <stdint.h>

/* FIR passe-bas + décimation par M. Coefficients calculés à l'initialisation (sinc fenêtré
 * Kaiser), gain unité en continu. NTAPS impair. */
template <int NTAPS>
class RscwDecimator {
public:
   RscwDecimator() : M(1), phase(0), w(0) { for (int i = 0; i < 2 * NTAPS; i++) buf[i] = 0; coef[0] = 1; }

   /* fs : fréquence d'entrée, m : facteur, fc : fréquence de coupure (-6 dB), beta : Kaiser */
   void design(float fs, int m, float fc, float beta)
   {
      M = m; phase = 0;
      double sum = 0;
      int c = NTAPS / 2;
      for (int i = 0; i < NTAPS; i++) {
         double x = i - c;
         double h = (x == 0) ? 2 * fc / fs : sin(2 * M_PI * fc / fs * x) / (M_PI * x);
         double r = x / c;
         h *= bessel_i0(beta * sqrt(1 - r * r)) / bessel_i0(beta);
         coef[i] = (float)h;
         sum += h;
      }
      for (int i = 0; i < NTAPS; i++) coef[i] = (float)(coef[i] / sum);
   }

   /* Pousse un échantillon d'entrée ; renvoie true et écrit *out tous les M échantillons. */
   inline bool push(float x, float *out)
   {
      buf[w] = x; buf[w + NTAPS] = x;             /* double tampon : fenêtre contiguë sans modulo */
      w = (w + 1 == NTAPS) ? 0 : w + 1;
      if (++phase < M) return false;
      phase = 0;
      if (M == 1) { *out = x; return true; }
      const float *p = buf + w;                   /* plus ancien échantillon */
      float acc = 0;
      for (int i = 0; i < NTAPS; i++) acc += p[i] * coef[NTAPS - 1 - i];
      *out = acc;
      return true;
   }

   int factor() const { return M; }

private:
   static double bessel_i0(double x)
   {
      double s = 1, t = 1;
      for (int k = 1; k < 50; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-12 * s) break; }
      return s;
   }
   int M, phase, w;
   float coef[NTAPS];
   float buf[2 * NTAPS];
};

/* File à un producteur (interruption audio) et un consommateur (loop()), sans verrou.
 * N puissance de 2. Les éléments perdus quand la file est pleine sont comptés. */
template <typename T, unsigned N>
class RscwSpscQueue {
public:
   RscwSpscQueue() : head(0), tail(0), dropped(0), highWater(0) {}
   bool push(const T &v)
   {
      unsigned h = head, t = tail;
      if (h - t >= N) { dropped++; return false; }
      data[h & (N - 1)] = v;
      __sync_synchronize();
      head = h + 1;
      if (h + 1 - t > highWater) highWater = h + 1 - t;
      return true;
   }
   bool pop(T *v)
   {
      unsigned t = tail;
      if (head == t) return false;
      __sync_synchronize();
      *v = data[t & (N - 1)];
      tail = t + 1;
      return true;
   }
   unsigned size() const { return head - tail; }
   void clear() { tail = head; }
   volatile unsigned head, tail;
   volatile uint32_t dropped;
   volatile unsigned highWater;
private:
   T data[N];
};

#endif
