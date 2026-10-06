#ifndef SWAT_LOCOMOTION_H
#define SWAT_LOCOMOTION_H
#include "controller.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SWAT_LOCOMOTION_VERSION 2
#define SWAT_LOCOMOTION_OBS 32
#define SWAT_LOCOMOTION_LOGITS 18
#define SWAT_LOCOMOTION_HEADS 6
#define SWAT_LOCOMOTION_ACTION_SIZES {5,3,3,2,2,3}
struct SwatSim;
void swat_locomotion_observe(const struct SwatSim* sim,int actor,b3Pos goal,float out[SWAT_LOCOMOTION_OBS]);
SwatInput swat_locomotion_decode(const float action[SWAT_LOCOMOTION_HEADS]);
typedef bool (*SwatMovementPolicy)(void* context,const struct SwatSim* sim,int actor,
                                  const float observation[SWAT_LOCOMOTION_OBS],float action[SWAT_LOCOMOTION_HEADS]);
void swat_locomotion_set_policy(SwatMovementPolicy policy,void* context);
bool swat_locomotion_input(const struct SwatSim* sim,int actor,b3Pos goal,SwatInput* input);
// Headless curriculum ABI. Explicit reset, terminal transition retained.
void* swat_training_create(uint32_t seed,int role,int stage);
void swat_training_reset(void* env,uint32_t seed,int role,int stage);
void swat_training_observe(void* env,float* observation);
int swat_training_step(void* env,const float* action,float* observation,float* reward);
void swat_training_close(void* env);
struct SwatSim* swat_training_sim(void* env);
int swat_training_actor(void* env);
void swat_training_limit(void* env,int steps);
void swat_training_rewards(void* env,float progress,float step_cost,float success,float fall_penalty);
#ifdef __cplusplus
}
#endif
#endif
