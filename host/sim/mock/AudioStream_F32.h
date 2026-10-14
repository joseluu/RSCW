/* Bouchon OpenAudio : juste assez d'AudioStream_F32 pour exécuter AudioRSCW_F32::update(). */
#ifndef SIM_AUDIOSTREAM_F32_H
#define SIM_AUDIOSTREAM_F32_H
#include "Arduino.h"
#define AUDIO_BLOCK_SAMPLES 128
typedef float float32_t;
struct audio_block_f32_t { float data[AUDIO_BLOCK_SAMPLES]; int length = AUDIO_BLOCK_SAMPLES; int full_length = AUDIO_BLOCK_SAMPLES; float fs_Hz = 48000; };
class AudioStream_F32 {
public:
   AudioStream_F32(int n, audio_block_f32_t **q) : nin(n), queue(q) { for (int i = 0; i < n; i++) q[i] = nullptr; }
   virtual ~AudioStream_F32() {}
   virtual void update(void) = 0;
   /* simulation : bloc d'entrée injecté par le banc ; sorties mémorisées */
   void simInput(int ch, audio_block_f32_t *b) { queue[ch] = b; }
   audio_block_f32_t simOut[16];
   static audio_block_f32_t *allocate_f32() { return new audio_block_f32_t(); }
   static void release(audio_block_f32_t *b) { (void)b; }   /* blocs d'entrée appartenant au banc ; voir sim_main */
protected:
   audio_block_f32_t *receiveReadOnly_f32(int ch = 0) { audio_block_f32_t *b = queue[ch]; queue[ch] = nullptr; return b; }
   audio_block_f32_t *receiveWritable_f32(int ch = 0) { return receiveReadOnly_f32(ch); }
   void transmit(audio_block_f32_t *b, int ch = 0) { if (ch < 16) simOut[ch] = *b; delete b; }
   int nin;
   audio_block_f32_t **queue;
};
struct AudioSettings_F32 {};
struct AudioConnection_F32 { template <typename A, typename B> AudioConnection_F32(A &, int, B &, int) {} };
inline void AudioMemory_F32(int) {}
#define AudioMemoryUsageMax_F32() 0
#endif
