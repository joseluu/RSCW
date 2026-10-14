/* AudioRSCW_F32.h — objet audio OpenAudio F32 qui alimente le décodeur RSCW.
 *
 * Dans l'interruption audio (update()) :
 *   - décimation 48 kHz -> 8 kHz (RscwDecimator, FIR 181 coefficients) ;
 *   - mise en file des échantillons 8 kHz pour loop(), avec l'état des broches 14/15 (vérité
 *     terrain du codeur), pour que l'oscilloscope puisse les afficher alignés sur le signal ;
 *   - crête de l'entrée (commande `peak`) ;
 *   - sorties analogiques (pour un oscilloscope externe, via les mixeurs du harnais).
 * Dans loop() (process()) : vidage de la file dans le cœur Rscw (FFT, horloge, décision),
 * historique des broches, statistiques de charge.
 *
 * Sorties (index = entrée des mixeurs AudioMixer11_F32 du harnais) :
 *   RSCW_OUT_MAG    module bf (étape 4 de RSCW), retardé de scopeDelay(), ×2
 *   RSCW_OUT_I      bande de base I (mélange à la porteuse estimée + moyenne sur 48 éch.), ×2
 *   RSCW_OUT_Q      bande de base Q, ×2
 *   RSCW_OUT_THRESH seuil de décision (estimate_threshold2a), retardé, ×2
 *   RSCW_OUT_BIT    bit dur (bt > 0), retardé : 0 ou 0,5
 *   RSCW_OUT_DECIM  signal décimé à 8 kHz (maintenu sur 6 échantillons)
 * Les sorties retardées sont fournies par le harnais via setDelayedOutputs() (calculées par
 * l'oscilloscope au même instant que les traces de l'écran).
 */
#ifndef AUDIO_RSCW_F32_H
#define AUDIO_RSCW_F32_H

#include <Arduino.h>
#include <AudioStream_F32.h>
#include "rscw_core.h"
#include "rscw_decim.h"

enum {
   RSCW_OUT_MAG = 0,
   RSCW_OUT_I,
   RSCW_OUT_Q,
   RSCW_OUT_THRESH,
   RSCW_OUT_BIT,
   RSCW_OUT_DECIM,
   RSCW_NUM_OUTPUTS
};

#define RSCW_PIN_SYNC 14
#define RSCW_PIN_TONE 15

class AudioRSCW_F32 : public AudioStream_F32 {
public:
   static const int FS_IN = 48000;               /* fréquence I2S (setI2SFreq dans le sketch) */
   static const int DECIM = FS_IN / 8000;
   static const unsigned PIN_HIST = 8192;         /* historique des broches à 1 kHz : 8,2 s */

   AudioRSCW_F32();
   virtual void update(void);

   void begin(Rscw::CharCallback cb, void *ctx);
   void process();                               /* à appeler dans loop() */
   void resetDecoder();                          /* vide la file et remet le cœur à zéro */

   Rscw &core() { return dec; }

   /* broches 14/15 à l'instant t (horloge 8 kHz du cœur) ; bit0 = 14 (sync), bit1 = 15 (tone) */
   uint8_t pinsAt(int t) const { return pinHist[((unsigned)t >> 3) & (PIN_HIST - 1)]; }

   void setDelayedOutputs(float mag, float thr, bool bit) { outMag = mag; outThr = thr; outBit = bit; }

   float peakMax() const { return pkMax; }
   float peakMin() const { return pkMin; }
   void resetPeak() { pkMax = 0; pkMin = 0; }
   unsigned queueHighWater() const { return q.highWater; }
   uint32_t queueDropped() const { return q.dropped; }
   uint32_t procMaxUs() const { return procMax; }
   void resetStats() { q.highWater = 0; q.dropped = 0; procMax = 0; }

private:
   struct Smp { float x; uint8_t pins; };
   audio_block_f32_t *inputQueueArray[1];
   Rscw dec;
   RscwDecimator<181> decim;
   RscwSpscQueue<Smp, 4096> q;
   uint8_t pinHist[PIN_HIST];
   /* bande de base temps réel (sorties I/Q) */
   double ncoPhase;
   float bbI[48], bbQ[48]; float sumI, sumQ; int bbIdx;
   float lastDecim;
   volatile float outMag, outThr; volatile bool outBit;
   volatile float carrierHz;
   volatile float pkMax, pkMin;
   uint32_t procMax;
};

#endif
