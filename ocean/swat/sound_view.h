#ifndef SWAT_SOUND_VIEW_H
#define SWAT_SOUND_VIEW_H
#include "sim.h"
#include "audio_dsp.h"
#include "spatial_audio.h"
#include "raylib.h"
#define SWAT_SOUND_CLIPS 96
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
    float volume; // Read by the audio-thread mixer under its lock.
    double mix_seconds; uint64_t mixed_frames; // Audio-thread mixing cost, under the same lock.
} SwatSoundView;
void swat_sound_view_init(SwatSoundView* view);
void swat_sound_view_update(SwatSoundView* view,const SwatSim* sim,int actor,float volume,uint32_t sound_floor,float yaw_offset);
void swat_sound_view_close(SwatSoundView* view);
// The audio-thread mixing step (device callback). Tools only: detach the
// device and call this themselves, e.g. to pace an unpaced null device.
void swat_sound_view_mix(SwatSoundView* view,float* stereo,int frames);
void swat_sound_view_detach(SwatSoundView* view);
#endif
