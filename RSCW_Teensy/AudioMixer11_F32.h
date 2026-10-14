/*
 * AudioMixer11_F32
 *
 * Based on AudioMixer9_F32 by Patrick Radius / Chip Audette
 * Extended to 11 inputs for additional source routing (INPUT, SIGNAL INT.)
 *
 * MIT License.  use at your own risk.
*/

#ifndef AUDIOMIXER11_F32_H
#define AUDIOMIXER11_F32_H

#include <arm_math.h>
#include <AudioStream_F32.h>

class AudioMixer11_F32 : public AudioStream_F32 {
//GUI: inputs:11, outputs:1
//GUI: shortName:Mixer11
public:
    AudioMixer11_F32() : AudioStream_F32(11, inputQueueArray) { setDefaultValues();}
    AudioMixer11_F32(const AudioSettings_F32 &settings) : AudioStream_F32(11, inputQueueArray) { setDefaultValues();}

	void setDefaultValues(void) {
      for (int i=0; i<11; i++) multiplier[i] = 1.0;
    }

    virtual void update(void);

    void gain(unsigned int channel, float gain) {
      if (channel >= 11 || channel < 0) return;
      multiplier[channel] = gain;
    }

  private:
    audio_block_f32_t *inputQueueArray[11];
    float multiplier[11];
};

#endif
