/* rscw_host — le cœur porté (RSCW_Teensy/rscw_core.cpp) avec l'interface du rscw d'origine :
 * audio non signé 8 bits, 8000 éch./s sur stdin, texte décodé sur stdout. Options -w, -t, -s, -f
 * comme rscw (sans X11). Options en plus :
 *   -F 16   entrée en entiers signés 16 bits (au lieu de u8)
 *   -S      statistiques sur stderr (pool d'hypothèses, latence des bits)
 * Sert au test d'équivalence (host/tests/equiv.sh) et aux mesures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "../RSCW_Teensy/rscw_core.h"

static Rscw dec;
static int maxLag = 0;

static void onChar(char c, void *) { putchar(c); fflush(stdout); }

int main(int argc, char **argv)
{
   float wpm = 22;
   int fmode = Rscw::STRONGEST;
   double Fmin = 1, Fmax = Rscw::SAMP / 2 - 1;
   int s16 = 0, stats = 0;
   int c;
   while ((c = getopt(argc, argv, "xtsw:f:F:S")) != -1) {
      switch (c) {
         case 'x': break;
         case 't': fmode = Rscw::TRACK; break;
         case 's': fmode = Rscw::STRONGEST; break;
         case 'w': wpm = atof(optarg); break;
         case 'f':
            if (strchr(optarg, '-')) { int l = 0; sscanf(optarg, "%lg%n", &Fmin, &l); sscanf(optarg + l + 1, "%lg", &Fmax); }
            else Fmin = Fmax = atof(optarg);
            break;
         case 'F': s16 = (atoi(optarg) == 16); break;
         case 'S': stats = 1; break;
         default: fprintf(stderr, "usage: rscw_host [-w wpm] [-t|-s] [-f f|fmin-fmax] [-F 16] [-S] < audio\n"); return 1;
      }
   }
   printf("RSCW   %g wpm   mode %s   %g - %g Hz    ( rscw -h  for help)\n",
          wpm, (fmode == Rscw::TRACK) ? "track" : (fmode == Rscw::STRONGEST) ? "strongest" : "fixed", Fmin, Fmax);
   dec.setWpm(wpm);
   dec.setFreqMode(fmode);
   dec.setRange(Fmin, Fmax);
   dec.begin(onChar, 0);
   int lastBit = -0x40000000;
   for (;;) {
      float x;
      if (s16) {
         short v;
         if (fread(&v, 2, 1, stdin) != 1) break;
         x = v / 32768.f;
      } else {
         int ch = getchar();
         if (ch == EOF) break;
         x = (ch - 128) / 128.;
      }
      dec.push(x);
      int lb = dec.latestBitTime();
      if (lb != lastBit) {
         lastBit = lb;
         int lag = dec.time() - lb;
         if (lb > 0 && lag > maxLag) maxLag = lag;
      }
   }
   if (stats)
      fprintf(stderr, "\nstats: chars=%u msgHigh=%d msgOverflow=%u maxBitLag=%d scopeDelay=%d carrier=%.1f\n",
              dec.charCount(), dec.msgHighWater(), dec.msgOverflows(), maxLag, dec.scopeDelay(), dec.carrier());
   return 0;
}
