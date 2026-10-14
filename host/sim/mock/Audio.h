#ifndef SIM_AUDIO_H
#define SIM_AUDIO_H
#include "Arduino.h"
#define AUDIO_INPUT_LINEIN 0
struct AudioControlSGTL5000 { void enable() {} void inputSelect(int) {} void volume(float) {} void lineInLevel(int) {} void lineOutLevel(int) {} };
inline void AudioMemory(int) {}
inline float AudioProcessorUsageMax() { return 0; }
#endif
