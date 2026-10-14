/* cwgen.h — générateur de test : CW manipulé + bruit blanc gaussien, à n'importe quelle fréquence
 * d'échantillonnage, avec la définition du bruit de noisycw (Eb/N0 sur l'énergie d'un point,
 * même N0 quelle que soit la fréquence). Utilisé par decim_test et la simulation du sketch. */
#ifndef CWGEN_H
#define CWGEN_H
#include <math.h>
#include <stdint.h>
#include <string>
#include <vector>

static inline const char *cwMorse(char ch)
{
   static const char *tab[][2] = {
      {"A",".-"},{"B","-..."},{"C","-.-."},{"D","-.."},{"E","."},{"F","..-."},{"G","--."},{"H","...."},
      {"I",".."},{"J",".---"},{"K","-.-"},{"L",".-.."},{"M","--"},{"N","-."},{"O","---"},{"P",".--."},
      {"Q","--.-"},{"R",".-."},{"S","..."},{"T","-"},{"U","..-"},{"V","...-"},{"W",".--"},{"X","-..-"},
      {"Y","-.--"},{"Z","--.."},{"0","-----"},{"1",".----"},{"2","..---"},{"3","...--"},{"4","....-"},
      {"5","....."},{"6","-...."},{"7","--..."},{"8","---.."},{"9","----."},{"/","-..-."},{"=","-...-"},{"?","..--.."}};
   for (auto &t : tab) if (t[0][0] == ch) return t[1];
   return 0;
}

/* séquence de points (1 = porteuse) comme noisycw : élément + 1 espace, lettre + 2, mot + 4 */
static inline std::vector<char> cwKeying(const char *txt)
{
   std::vector<char> k;
   for (const char *p = txt; *p; p++) {
      if (*p == ' ') { for (int i = 0; i < 4; i++) k.push_back(0); continue; }
      const char *m = cwMorse(*p);
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

struct CwGen {
   std::vector<char> key;
   double fs, dot, asig, sig, scale, ph, dph;
   uint64_t rng;
   long n;
   CwGen(const char *txt, double ebn0, double wpm, double freq, double fs_, uint64_t seed, double fullScale = 1.0)
      : key(cwKeying(txt)), fs(fs_), rng(seed), n(0)
   {
      double dot8 = 8000 * 1.2 / wpm;
      dot = dot8 * fs / 8000;
      asig = sqrt(pow(10, ebn0 / 10) / dot8 * 2);
      sig = sqrt(0.5 * fs / 8000.0);
      scale = fullScale / (5 * sqrt(0.5 * 6) + asig);
      ph = 0; dph = 2 * M_PI * freq / fs;
   }
   long length(int tailDots = 60) const { return (long)((key.size() + tailDots) * dot); }
   int keyAt(long i) const { long d = (long)(i / dot); return (d < (long)key.size()) ? key[d] : 0; }
   double urand() { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return ((rng >> 11) + 0.5) / 9007199254740992.0; }
   double grand() { return sqrt(-2 * log(urand())) * cos(2 * M_PI * urand()); }
   float next(int *k)
   {
      *k = keyAt(n++);
      float x = (float)(scale * (sig * grand() + (*k ? asig * cos(ph) : 0)));
      ph += dph; if (ph > 2 * M_PI) ph -= 2 * M_PI;
      return x;
   }
};

static inline int cwEditDistance(const std::string &a, const std::string &b)
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

/* distance d'édition entre ref et la meilleure sous-chaîne de got (les caractères décodés dans le
 * bruit avant et après le message ne comptent pas : RSCW décode toujours quelque chose) */
static inline int cwInfixDistance(const std::string &ref, const std::string &got)
{
   std::vector<int> d(got.size() + 1, 0), e(got.size() + 1);
   for (size_t i = 1; i <= ref.size(); i++) {
      e[0] = i;
      for (size_t j = 1; j <= got.size(); j++)
         e[j] = std::min(std::min(d[j] + 1, e[j - 1] + 1), d[j - 1] + (ref[i - 1] != got[j - 1]));
      d.swap(e);
   }
   int best = d[0];
   for (int v : d) best = std::min(best, v);
   return best;
}

static inline std::string cwSqueeze(const std::string &s)
{
   std::string r;
   for (char c : s) if (!(c == ' ' && (r.empty() || r.back() == ' '))) r += c;
   while (!r.empty() && r.back() == ' ') r.pop_back();
   return r;
}
#endif
