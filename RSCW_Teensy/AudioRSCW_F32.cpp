/* AudioRSCW_F32.cpp — voir AudioRSCW_F32.h. */
#include "AudioRSCW_F32.h"

AudioRSCW_F32::AudioRSCW_F32()
   : AudioStream_F32(1, inputQueueArray), ncoPhase(0), sumI(0), sumQ(0), bbIdx(0), lastDecim(0),
     outMag(0), outThr(0), outBit(false), carrierHz(0), pkMax(0), pkMin(0), procMax(0)
{
   for (int i = 0; i < 48; i++) bbI[i] = bbQ[i] = 0;
   for (unsigned i = 0; i < PIN_HIST; i++) pinHist[i] = 0;
   decim.design(FS_IN, DECIM, 3800, 7);
}

void AudioRSCW_F32::begin(Rscw::CharCallback cb, void *ctx)
{
   dec.begin(cb, ctx);
}

void AudioRSCW_F32::resetDecoder()
{
   q.clear();
   dec.reset();
}

void AudioRSCW_F32::update(void)
{
   audio_block_f32_t *in = receiveReadOnly_f32(0);
   if (!in) return;

   uint8_t pins = (digitalReadFast(RSCW_PIN_SYNC) ? 1 : 0) | (digitalReadFast(RSCW_PIN_TONE) ? 2 : 0);
   float f = carrierHz;
   double dph = 2 * M_PI * f / 8000.0;

   audio_block_f32_t *out[RSCW_NUM_OUTPUTS];
   for (int k = 0; k < RSCW_NUM_OUTPUTS; k++) out[k] = allocate_f32();

   float mag = outMag * 2, thr = outThr * 2, bit = outBit ? 0.5f : 0.0f;
   float vI = sumI * (2.0f / 48), vQ = sumQ * (2.0f / 48);
   float pmax = pkMax, pmin = pkMin;

   for (int i = 0; i < in->length; i++) {
      float x = in->data[i];
      if (x > pmax) pmax = x;
      if (x < pmin) pmin = x;
      float y;
      if (decim.push(x, &y)) {
         Smp s; s.x = y; s.pins = pins;
         q.push(s);
         lastDecim = y;
         /* bande de base pour les sorties I/Q : même principe que les étapes 2-3 de RSCW */
         float c = cosf((float)ncoPhase), sn = sinf((float)ncoPhase);
         ncoPhase += dph;
         if (ncoPhase > 2 * M_PI) ncoPhase -= 2 * M_PI;
         float vi = y * c, vq = y * sn;
         sumI += vi - bbI[bbIdx]; sumQ += vq - bbQ[bbIdx];
         bbI[bbIdx] = vi; bbQ[bbIdx] = vq;
         bbIdx = (bbIdx + 1) % 48;
         vI = sumI * (2.0f / 48); vQ = sumQ * (2.0f / 48);
      }
      if (out[RSCW_OUT_MAG]) out[RSCW_OUT_MAG]->data[i] = mag;
      if (out[RSCW_OUT_I]) out[RSCW_OUT_I]->data[i] = vI;
      if (out[RSCW_OUT_Q]) out[RSCW_OUT_Q]->data[i] = vQ;
      if (out[RSCW_OUT_THRESH]) out[RSCW_OUT_THRESH]->data[i] = thr;
      if (out[RSCW_OUT_BIT]) out[RSCW_OUT_BIT]->data[i] = bit;
      if (out[RSCW_OUT_DECIM]) out[RSCW_OUT_DECIM]->data[i] = lastDecim;
   }
   pkMax = pmax; pkMin = pmin;

   for (int k = 0; k < RSCW_NUM_OUTPUTS; k++) {
      if (!out[k]) continue;
      out[k]->length = in->length;
      transmit(out[k], k);
      release(out[k]);
   }
   release(in);
}

void AudioRSCW_F32::process()
{
   Smp s;
   while (q.pop(&s)) {
      uint32_t t0 = micros();
      dec.push(s.x);
      int t = dec.time();
      if ((t & 7) == 0) pinHist[((unsigned)t >> 3) & (PIN_HIST - 1)] = s.pins;
      uint32_t dt = micros() - t0;
      if (dt > procMax) procMax = dt;
   }
   carrierHz = (float)dec.carrier();
}
