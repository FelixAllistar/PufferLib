#ifndef SWAT_LOCOMOTION_H
#define SWAT_LOCOMOTION_H
#include "controller.h"
#define SWAT_LOCOMOTION_VERSION 1
#define SWAT_LOCOMOTION_OBS 32
#define SWAT_LOCOMOTION_HIDDEN 64
#define SWAT_LOCOMOTION_LOGITS 18
#define SWAT_LOCOMOTION_HEADS 6
struct SwatSim;
void swat_locomotion_observe(const struct SwatSim* sim,int actor,b3Pos goal,float out[SWAT_LOCOMOTION_OBS]);
SwatInput swat_locomotion_decode(const float action[SWAT_LOCOMOTION_HEADS]);
bool swat_locomotion_load(const char* path);
bool swat_locomotion_logits(const float observation[SWAT_LOCOMOTION_OBS],float logits[SWAT_LOCOMOTION_LOGITS]);
bool swat_locomotion_input(const struct SwatSim* sim,int actor,b3Pos goal,SwatInput* input);
// Headless curriculum ABI. Explicit reset, terminal transition retained.
void* swat_training_create(uint32_t seed,int role,int stage);
void swat_training_reset(void* env,uint32_t seed,int role,int stage);
void swat_training_observe(void* env,float* observation);
int swat_training_step(void* env,const float* action,float* observation,float* reward);
void swat_training_close(void* env);
#endif
