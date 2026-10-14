/* AudioMixer11_F32
 * Based on AudioMixer9_F32 by Bob Larkin / Chip Audette
 * Extended to 11 inputs.
 *
 * MIT License.  use at your own risk.
*/

#include "AudioMixer11_F32.h"

void AudioMixer11_F32::update(void) {
  audio_block_f32_t *in, *out=NULL;

  //get the first available channel
  int channel = 0;
  while  (channel < 11) {
	  out = receiveWritable_f32(channel);
	  if (out) break;
	  channel++;
  }
  if (!out) return;  //there was no data output array.  so exit.
  arm_scale_f32(out->data, multiplier[channel], out->data, out->length);

  //add in the remaining channels, as available
  channel++;
  while  (channel < 11) {
    in = receiveReadOnly_f32(channel);
    if (in) {
		audio_block_f32_t *tmp = allocate_f32();

		arm_scale_f32(in->data, multiplier[channel], tmp->data, tmp->length);
		arm_add_f32(out->data, tmp->data, out->data, tmp->length);

		AudioStream_F32::release(tmp);
		AudioStream_F32::release(in);
	} else {
		//do nothing, this vector is empty
	}
	channel++;
  }
  AudioStream_F32::transmit(out);
  AudioStream_F32::release(out);
}
