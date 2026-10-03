#ifndef SWAT_AUDIO_DSP_H
#define SWAT_AUDIO_DSP_H
#include "acoustics.h"
#define SWAT_AUDIO_RATE 48000
#define SWAT_AUDIO_VOICES 32
typedef struct SwatAudioVoice {
    bool active;
    SwatSoundEvent event;
    uint32_t noise;
    float time,duration,left,right,filter,lowpass;
} SwatAudioVoice;
typedef struct SwatAudioMixer {
    SwatAudioVoice voices[SWAT_AUDIO_VOICES];
    int sample_rate;
} SwatAudioMixer;

void swat_audio_init(SwatAudioMixer* mixer,int sample_rate);
void swat_audio_spatial(SwatAudioVoice* voice,SwatAcousticPath path,b3Vec3 listener_right,int sample_rate);
void swat_audio_start(SwatAudioMixer* mixer,SwatSoundEvent event,SwatAcousticPath path,b3Vec3 listener_right);
// Portable stereo float PCM: usable without Raylib/device for recordings and
// future waveform listeners. Current agents use acoustic cues from the same path.
void swat_audio_mix(SwatAudioMixer* mixer,float* stereo,int frames,float volume);
#endif
