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
#include <string>
#include <vector>
#include "../RSCW_Teensy/rscw_core.h"
#include "../RSCW_Teensy/rscw_decim.h"

static const char *MORSE[128];
static void initMorse()
{
   const char *tab[][2] = {
      {"A",".-"},{"B","-..."},{"C","-.-."},{"D","-.."},{"E","."},{"F","..-."},{"G","--."},{"H","...."},
      {"I",".."},{"J",".---"},{"K","-.-"},{"L",".-.."},{"M","--"},{"N","-."},{"O","---"},{"P",".--."},
      {"Q","--.-"},{"R",".-."},{"S","..."},{"T","-"},{"U","..-"},{"V","...-"},{"W",".--"},{"X","-..-"},
      {"Y","-.--"},{"Z","--.."},{"0","-----"},{"1",".----"},{"2","..---"},{"3","...--"},{"4","....-"},
      {"5","....."},{"6","-...."},{"7","--..."},{"8","---.."},{"9","----."},{"/","-..-."},{"=","-...-"},{"?","..--.."}};
   for (auto &t : tab) MORSE[(int)t[0][0]] = t[1];
}

/* séquence de points (1 = porteuse) comme noisycw : élément + 1 espace, lettre + 2, mot = 4 */
static std::vector<char> keying(const char *txt)
{
   std::vector<char> k;
   for (const char *p = txt; *p; p++) {
      if (*p == ' ') { for (int i = 0; i < 4; i++) k.push_back(0); continue; }
      const char *m = MORSE[(int)*p];
      if (!m) continue;
      for (; *m; m++) {
         int n = (*m == '.') ? 1 : 3;
         for (int i = 0; i < n; i++) k.push_back(1);
         k.push_back(0);
      }
      k.push_back(0); k.push_back(0);
   }
   return k;
}

static uint64_t rng = 1;
static double urand() { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return ((rng >> 11) + 0.5) / 9007199254740992.0; }
static double grand() { return sqrt(-2 * log(urand())) * cos(2 * M_PI * urand()); }

static std::string out;
static void onChar(char c, void *) { out += c; }

static int editDistance(const std::string &a, const std::string &b)
{
   std::vector<int> d(b.size() + 1), e(b.size() + 1);
   for (size_t j = 0; j <= b.size(); j++) d[j] = j;
   for (size_t i = 1; i <= a.size(); i++) {
      e[0] = i;
      for (size_t j = 1; j <= b.size(); j++)
         e[j] = std::min(std::min(d[j] + 1, e[j - 1] + 1), d[j - 1] + (a[i - 1] != b[j - 1]));
      d.swap(e);
   }
   return d[b.size()];
}

static std::string squeeze(const std::string &s)
{
   std::string r;
   for (char c : s) if (!(c == ' ' && (r.empty() || r.back() == ' '))) r += c;
   while (!r.empty() && r.back() == ' ') r.pop_back();
   return r;
}

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
   std::vector<char> k = keying(txt);
   double dot8 = 8000 * 1.2 / wpm;
   double asig = sqrt(pow(10, ebn0 / 10) / dot8 * 2);   /* noisycw */
   double sig = sqrt(0.5 * fs / 8000.0);                  /* même N0 */
   double scale = 1.0 / (5 * sqrt(0.5 * 6) + asig);       /* même échelle pour 8 et 48 kHz */
   rng = seed;
   out.clear();
   dec.setWpm(wpm);
   dec.setFreqMode(Rscw::STRONGEST);
   dec.setRange(1, 3999);
   dec.begin(onChar, 0);
   decim.design(48000, 6, 3800, 7);
   q.clear();
   keyHist.clear();
   double ph = 0, dph = 2 * M_PI * freq / fs;
   long nsamp = (long)((k.size() + 60) * dot8 * fs / 8000);   /* + 60 points de silence final */
   Smp blk[128]; int nb = 0;
   for (long n = 0; n < nsamp; n++) {
      long dot = (long)(n * 8000.0 / fs / dot8);
      int key = (dot < (long)k.size()) ? k[dot] : 0;
      float x = (float)(scale * (sig * grand() + (key ? asig * cos(ph) : 0)));
      ph += dph; if (ph > 2 * M_PI) ph -= 2 * M_PI;
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
   std::string ref = squeeze(txt), got = squeeze(out);
   return (double)editDistance(ref, got) / ref.size();
}

int main(int argc, char **argv)
{
   initMorse();
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

   printf("wpm=%g f=%g Hz, %d essais ; CER direct 8 kHz | 48 kHz + decimation + file\n", wpm, freq, trials);
   int fail = 0;
   for (double eb : {20.0, 14.0, 10.0, 8.0}) {
      double c8 = 0, c48 = 0;
      for (int t = 0; t < trials; t++) {
         c8 += runOnce(txt, eb, wpm, freq, 8000, 1000 + t);
         c48 += runOnce(txt, eb, wpm, freq, 48000, 1000 + t);
      }
      c8 /= trials; c48 /= trials;
      printf("Eb/N0 %5.1f dB : CER %6.3f | %6.3f\n", eb, c8, c48);
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
