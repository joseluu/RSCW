// AudioSignalGenerator_F32.h
// Internal signal generator: DC constants and sine waves
// 7 modes: DC {1.0, 0.0, -1.0}, Sine {0.9, 9, 90, 900} Hz
// Author: Generated for Teensy + OpenAudio F32 library
// Repris de joseluu/Morse_decode_coherent (harnais commun)

#ifndef AUDIOSIGNALGENERATOR_F32_H
#define AUDIOSIGNALGENERATOR_F32_H

#include <AudioStream_F32.h>
#include <arm_math.h>

#define SIGGEN_NUM_MODES 7

class AudioSignalGenerator_F32 : public AudioStream_F32 {
public:
    AudioSignalGenerator_F32() : AudioStream_F32(0, NULL) {
        sample_rate = 44100.0f;
        setMode(0);
    }

    void setSampleRate(float sr) { sample_rate = sr; }

    void setMode(int m) {
        if (m < 0 || m >= SIGGEN_NUM_MODES) return;
        mode = m;
        switch (m) {
            case 0: dc_value = 1.0f;  freq = 0.0f;   break;   // RSCW_Teensy : remis en accord avec
            case 1: dc_value = 0.0f;  freq = 0.0f;   break;   // getModeName() (Morse_decode_coherent
            case 2: dc_value = -1.0f; freq = 0.0f;   break;   // inversait 1.0 et -1.0)
            case 3: dc_value = 0.0f;  freq = 0.9f;   break;
            case 4: dc_value = 0.0f;  freq = 9.0f;   break;
            case 5: dc_value = 0.0f;  freq = 90.0f;  break;
            case 6: dc_value = 0.0f;  freq = 900.0f; break;
        }
        phase = 0.0f;
    }

    int getMode() const { return mode; }

    static const char* getModeName(int m) {
        static const char* names[SIGGEN_NUM_MODES] = {
            "1.0", "0.0", "-1.0", "0.9Hz", "9Hz", "90Hz", "900Hz"
        };
        if (m < 0 || m >= SIGGEN_NUM_MODES) return "?";
        return names[m];
    }

    virtual void update(void) {
        audio_block_f32_t *block = allocate_f32();
        if (!block) return;

        if (freq == 0.0f) {
            // DC mode
            for (int i = 0; i < block->length; i++) {
                block->data[i] = dc_value;
            }
        } else {
            // Sine mode
            float phase_inc = 2.0f * PI * freq / sample_rate;
            for (int i = 0; i < block->length; i++) {
                block->data[i] = arm_sin_f32(phase);
                phase += phase_inc;
            }
            // Keep phase in [0, 2*PI) to avoid float precision loss
            while (phase >= 2.0f * PI) phase -= 2.0f * PI;
        }

        transmit(block);
        release(block);
    }

private:
    int mode = 0;
    float dc_value = 1.0f;
    float freq = 0.0f;
    float phase = 0.0f;
    float sample_rate;
};

#endif
