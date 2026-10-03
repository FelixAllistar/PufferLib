#ifndef SWAT_SPATIAL_AUDIO_H
#define SWAT_SPATIAL_AUDIO_H
#include "swat_math.h"
#include <stdbool.h>
#define SWAT_SPATIAL_FRAMES 256
typedef struct SwatSpatialAudio SwatSpatialAudio;
SwatSpatialAudio* swat_spatial_open(int rate,int voices);
void swat_spatial_close(SwatSpatialAudio* audio);
void swat_spatial_reset(SwatSpatialAudio* audio);
// Direction is listener-relative: +X right, +Y up, -Z ahead (Steam Audio).
void swat_spatial_process(void* audio,int voice,uint32_t event,b3Vec3 direction,
                          const float* mono,int frames,float* left,float* right);
#endif
