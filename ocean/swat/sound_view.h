#ifndef SWAT_SOUND_VIEW_H
#define SWAT_SOUND_VIEW_H
#include "sim.h"
#include "audio_dsp.h"
#include "spatial_audio.h"
#include "raylib.h"
#define SWAT_SOUND_CLIPS 80
typedef struct SwatSoundClip {
    float* pcm;
    int frames,rate,kind,material,profile;
    float gain;
} SwatSoundClip;
typedef struct SwatSoundView {
    bool initialized;
    AudioStream stream;
    SwatAudioMixer mixer;
    SwatHearingMemory memory;
    SwatSpatialAudio* spatial;
    int episode,actor,next_acoustic_tick;
    uint32_t path_ids[SWAT_SOUND_CAPACITY];
    int path_ticks[SWAT_SOUND_CAPACITY],path_generation;
    SwatAcousticPath paths[SWAT_SOUND_CAPACITY];
    SwatSoundClip clips[SWAT_SOUND_CLIPS];
    int clip_count;
} SwatSoundView;
void swat_sound_view_init(SwatSoundView* view);
void swat_sound_view_update(SwatSoundView* view,const SwatSim* sim,int actor,float volume,uint32_t sound_floor,float yaw_offset);
void swat_sound_view_close(SwatSoundView* view);
#endif
