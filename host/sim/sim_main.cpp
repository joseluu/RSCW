/* sim_main — exécute le VRAI sketch RSCW_Teensy.ino sur PC, avec des bouchons (host/sim/mock) pour
 * Arduino, OpenAudio, l'écran, l'encodeur et le bouton.
 *
 * Banc simulé : CW + bruit à 48 kHz en blocs de 128 échantillons -> AudioRSCW_F32::update()
 * (comme l'interruption audio), broches 14 (impulsion de 200 ms au début de chaque message, comme
 * le codeur) et 15 (manipulation), puis loop() ; les commandes série sont injectées à des instants
 * donnés. Toute la sortie série est écrite sur stdout (et vérifiée par sim_test.sh).
 *
 * Usage : sim_rscw [-w wpm] [-e EbN0] [-f Hz] [-r répétitions] [-c "t:commande"]...
 *   par défaut : 20 WPM, 20 dB, 900 Hz, 3 répétitions, commandes :
 *   0.1:status  0.2:log on  0.3:scope  <fin>:status
 */
#include "Arduino.h"
#include <vector>
#include <utility>
#include "../cwgen.h"

namespace sim {
double nowUs = 0;
int pins[64];
std::string serialIn, serialOut;
bool echo = true;
int encoderValue = 0;
int buttonPresses = 0;
std::string tftText;
long tftPixels = 0;
}
SimSerial Serial;
uint32_t CCM_CS1CDR, CCM_CS2CDR;

void setup();
void loop();
#include "AudioRSCW_F32.h"
extern AudioRSCW_F32 rscw;

int main(int argc, char **argv)
{
   double wpm = 20, eb = 20, freq = 900;
   int reps = 3;
   std::vector<std::pair<double, std::string>> cmds;
   bool userCmds = false;
   for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "-w" && i + 1 < argc) wpm = atof(argv[++i]);
      else if (a == "-e" && i + 1 < argc) eb = atof(argv[++i]);
      else if (a == "-f" && i + 1 < argc) freq = atof(argv[++i]);
      else if (a == "-r" && i + 1 < argc) reps = atoi(argv[++i]);
      else if (a == "-c" && i + 1 < argc) {
         std::string c = argv[++i];
         size_t p = c.find(':');
         cmds.push_back({atof(c.substr(0, p).c_str()), c.substr(p + 1)});
         userCmds = true;
      }
   }
   const char *msg = "CQ CQ DE F1FGV F1FGV TEST 1234567890 PARIS PARIS THE QUICK BROWN FOX ";
   /* 2 s de silence en tête : les commandes de réglage (set wpm réinitialise le décodeur)
      doivent précéder le premier message, comme sur le banc */
   std::string lead((size_t)(2.0 / (1.2 / wpm) / 4) + 1, ' ');
   std::string txt = lead;
   for (int r = 0; r < reps; r++) txt += msg;
   CwGen g(txt.c_str(), eb, wpm, freq, 48000, 42, 0.5);
   long total = g.length(120);
   double dur = total / 48000.0;
   /* début de chaque message en échantillons (pour l'impulsion de synchro sur la pin 14) */
   std::vector<long> msgStart;
   {
      long dots = cwKeying(lead.c_str()).size();
      for (int r = 0; r < reps; r++) { msgStart.push_back((long)(dots * g.dot)); dots += cwKeying(msg).size(); }
   }
   if (!userCmds) {
      cmds = {{0.1, "status"}, {0.2, "set wpm " + std::to_string((int)wpm)}, {0.3, "log on"}, {0.4, "scope"},
              {dur - 0.05, "status"}};
   }

   setup();                                  /* inclut l'écran d'accueil (delay 3 s simulé) */
   double t0 = sim::nowUs;
   size_t ci = 0;
   audio_block_f32_t blk;
   for (long n = 0; n < total; n += 128) {
      for (int i = 0; i < 128; i++) {
         int k;
         blk.data[i] = g.next(&k);
         long s = n + i;
         bool sync = false;
         for (long m : msgStart) if (s >= m && s < m + 9600) sync = true;   /* 200 ms */
         if (i == 0) { sim::pins[14] = sync; sim::pins[15] = k; }
      }
      blk.length = 128;
      rscw.simInput(0, &blk);
      rscw.update();
      sim::nowUs += 128 * 1e6 / 48000;
      double t = (sim::nowUs - t0) / 1e6;
      while (ci < cmds.size() && cmds[ci].first <= t) { sim::serialIn += cmds[ci].second + "\n"; ci++; }
      loop();
   }
   fprintf(stderr, "sim: %.1f s audio, TFT: %zu caracteres, %ld pixels scope\n", dur, sim::tftText.size(), sim::tftPixels);
   std::string ref = cwSqueeze(txt), got = cwSqueeze(sim::tftText);
   fprintf(stderr, "sent: %s\ntft:  %s\n", ref.c_str(), got.c_str());
   fprintf(stderr, "CER=%.4f\n", (double)cwInfixDistance(ref, got) / ref.size());
   fprintf(stderr, "CER_brut=%.4f (avec le bruit avant/apres le message)\n", (double)cwEditDistance(ref, got) / ref.size());
   return 0;
}
