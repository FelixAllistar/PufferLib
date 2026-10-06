#ifndef SWAT_AUDIO_DSP_H
#define SWAT_AUDIO_DSP_H
#include "acoustics.h"
#define SWAT_AUDIO_RATE 48000
#define SWAT_AUDIO_VOICES 32
#define SWAT_AUDIO_BLOCK 256
#define SWAT_REVERB_BUFFER 4096
#define SWAT_SOURCE_BUFFER 2048
typedef void (*SwatBinauralProcess)(void*,int,uint32_t,b3Vec3,const float*,int,float*,float*);
typedef struct SwatAudioVoice {
    bool active;
    SwatSoundEvent event;
    uint32_t noise;
    float time,duration,left,right,filter,lowpass;
    float gain, smooth_gain, smooth_filter;
    b3Vec3 direction;
    uint32_t token;
    float source_echo[2][SWAT_SOURCE_BUFFER],source_feedback[2],source_damping[2];
    int source_delay[2],source_position[2];
    float source_filter,source_wet,source_smooth_wet,source_tail;
    const float* recording;
    int recording_frames,recording_rate;
    float recording_gain;
} SwatAudioVoice;
typedef struct SwatAudioMixer {
    SwatAudioVoice voices[SWAT_AUDIO_VOICES];
    int sample_rate;
    SwatBinauralProcess binaural;
    void* spatial;
    b3Vec3 forward,right,up;
    uint32_t next_token;
    SwatRoomAcoustics room;
    float feedback[4],damping[4],reverb[4][SWAT_REVERB_BUFFER],early[SWAT_REVERB_BUFFER];
    int delays[4],positions[4],early_position,early_delays[4];
    float damp_coefficient,wet;
    float output[SWAT_AUDIO_BLOCK*2];
    int output_cursor;
} SwatAudioMixer;

void swat_audio_init(SwatAudioMixer* mixer,int sample_rate);
void swat_audio_room(SwatAudioMixer* mixer,SwatRoomAcoustics room);
void swat_audio_listener(SwatAudioMixer* mixer,b3Vec3 forward,b3Vec3 right,b3Vec3 up);
void swat_audio_spatial(SwatAudioVoice* voice,SwatAcousticPath path,b3Vec3 listener_right,int sample_rate);
SwatAudioVoice* swat_audio_start(SwatAudioMixer* mixer,SwatSoundEvent event,SwatAcousticPath path,b3Vec3 listener_right);
void swat_audio_recording(SwatAudioVoice* voice,const float* mono,int frames,int rate,float gain);
// A source in another room carries its own filtered decay along its acoustic
// path, including to an outdoor listener. Same-room decay uses the listener bus.
void swat_audio_source_room(SwatAudioVoice* voice,SwatRoomAcoustics room,bool different_room,int sample_rate);
// Portable stereo float PCM: usable without Raylib/device for recordings and
// future waveform listeners. Current agents use acoustic cues from the same path.
void swat_audio_mix(SwatAudioMixer* mixer,float* stereo,int frames,float volume);
#endif
