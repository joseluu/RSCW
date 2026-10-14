/* decim_test — vérifie la chaîne d'entrée du Teensy sur PC :
 *   CW + bruit blanc généré à 48 kHz -> RscwDecimator (/6) -> RscwSpscQueue (blocs de 128) -> Rscw
 * contre la même chose générée directement à 8 kHz -> Rscw.
 * Bruit défini comme noisycw (Eb/N0 sur l'énergie d'un point, même N0 quelle que soit la fréquence
 * d'échantillonnage). Mesure le taux d'erreur caractères (CER, distance d'édition) sur plusieurs
 * graines et affiche aussi la réponse du filtre.
 * Usage : decim_test [wpm] [freq] [essais]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../RSCW_Teensy/rscw_core.h"
#include "../RSCW_Teensy/rscw_decim.h"

#include "cwgen.h"

static std::string out;
static void onChar(char c, void *) { out += c; }

static Rscw dec;
/* vérification de l'alignement du scope : historique de la manipulation indexé par l'horloge du
   cœur (comme AudioRSCW_F32::pinsAt), comparé au bit lu par bitAt(T), T = time - scopeDelay() */
static std::vector<uint8_t> keyHist;
static long alignN, alignOk;
static RscwDecimator<181> decim;
struct Smp { float x; uint8_t pins; };
static RscwSpscQueue<Smp, 4096> q;

/* fs = 8000 : direct ; fs = 48000 : décimation + file */
static double runOnce(const char *txt, double ebn0, float wpm, double freq, int fs, uint64_t seed)
{
   CwGen g(txt, ebn0, wpm, freq, fs, seed);
   out.clear();
   dec.setWpm(wpm);
   dec.setFreqMode(Rscw::STRONGEST);
   dec.setRange(1, 3999);
   dec.begin(onChar, 0);
   decim.design(48000, 6, 3800, 7);
   q.clear();
   keyHist.clear();
   long nsamp = g.length();
   Smp blk[128]; int nb = 0;
   for (long n = 0; n < nsamp; n++) {
      int key;
      float x = g.next(&key);
      if (fs == 8000) { dec.push(x); continue; }
      float y;
      if (decim.push(x, &y)) { blk[nb].x = y; blk[nb].pins = key; nb++; }
      if ((n + 1) % 128 == 0) {                              /* fin d'un bloc audio : file, puis loop() */
         for (int i = 0; i < nb; i++) q.push(blk[i]);
         nb = 0;
         Smp s;
         while (q.pop(&s)) {
            dec.push(s.x);
            int t = dec.time();
            if (t >= 0) { if ((size_t)t >= keyHist.size()) keyHist.resize(t + 1); keyHist[t] = s.pins; }
            if (t > 0 && t % 80 == 0) {                   /* toutes les 10 ms, comme le scope */
               int T = t - dec.scopeDelay();
               float soft, thr;
               if (T > 0 && dec.bitAt(T, &soft, &thr) && (size_t)T < keyHist.size()) {
                  alignN++;
                  if ((soft > 0) == (keyHist[T] != 0)) alignOk++;
               }
            }
         }
      }
   }
   std::string ref = cwSqueeze(txt), got = cwSqueeze(out);
   return (double)cwEditDistance(ref, got) / ref.size();
}

int main(int argc, char **argv)
{
   float wpm = argc > 1 ? atof(argv[1]) : 20;
   double freq = argc > 2 ? atof(argv[2]) : 900;
   int trials = argc > 3 ? atoi(argv[3]) : 4;
   const char *txt = "CQ CQ DE F1FGV F1FGV TEST 1234567890 PARIS PARIS THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG";

   /* réponse du filtre de décimation (sinus purs) */
   printf("decimateur 48k/6, 181 coefficients, fc 3800 Hz :");
   for (double f : {500.0, 1000.0, 3000.0, 3400.0, 4000.0, 4500.0, 6000.0, 7200.0, 12000.0}) {
      decim.design(48000, 6, 3800, 7);
      double e = 0; int n = 0; float y;
      for (int i = 0; i < 48000; i++) if (decim.push(sin(2 * M_PI * f * i / 48000), &y) && i > 4800) { e += y * y; n++; }
      printf(" %g:%.1fdB", f, 10 * log10(2 * e / n + 1e-20));
   }
   printf("\n");

   printf("wpm=%g f=%g Hz, %d essais ; CER direct 8 kHz | 48 kHz + decimation + file | erreurs bit dur (48 kHz)\n", wpm, freq, trials);
   int fail = 0;
   for (double eb : {20.0, 14.0, 10.0, 8.0}) {
      double c8 = 0, c48 = 0;
      alignN = alignOk = 0;
      for (int t = 0; t < trials; t++) {
         c8 += runOnce(txt, eb, wpm, freq, 8000, 1000 + t);
         c48 += runOnce(txt, eb, wpm, freq, 48000, 1000 + t);
      }
      c8 /= trials; c48 /= trials;
      printf("Eb/N0 %5.1f dB : CER %6.3f | %6.3f | %5.1f%%\n", eb, c8, c48, alignN ? 100.0 * (alignN - alignOk) / alignN : 0.0);
      if (eb >= 20 && c48 > 0.02) fail = 1;           /* à fort SNR la chaîne doit être propre */
   }
   /* alignement scope à fort SNR */
   alignN = alignOk = 0;
   runOnce(txt, 25, wpm, freq, 48000, 7);
   double al = alignN ? (double)alignOk / alignN : 0;
   printf("alignement scope (Eb/N0 25 dB) : bit RSCW = manipulation reelle sur %.1f%% de %ld points\n", 100 * al, alignN);
   if (al < 0.9) fail = 1;
   printf("queue: highWater=%u dropped=%u\n", q.highWater, (unsigned)q.dropped);
   return fail;
}
