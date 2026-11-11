// Training/evaluation split for the role task (CONTRACT.md "Held-out
// evaluation suite"). Every episode's simulation seed and layout seed comes
// from one of two disjoint streams, separated by bit 31: training values
// always have it clear, evaluation values always have it set. The training
// env can only draw from the training stream, and its configuration cannot
// name an evaluation layout, so no held-out situation can reach training.
// Dependency-free so tests can check the streams without the simulation.
#ifndef SWAT_ROLES_SPLIT_H
#define SWAT_ROLES_SPLIT_H
#include <stdbool.h>
#include <stdint.h>

#define SWAT_ROLES_EVAL_BIT 0x80000000u
// Motel occupancy profile 3 (busy, six occupants) is reserved for
// evaluation: training configs and curriculum stages may not select it.
#define SWAT_ROLES_EVAL_OCCUPANCY 3
// Largest per-episode training layout pool (roles_layouts).
#define SWAT_ROLES_MAX_LAYOUTS (1<<20)

typedef enum SwatRolesSplit { SWAT_ROLES_TRAIN, SWAT_ROLES_EVAL } SwatRolesSplit;

static inline uint32_t swat_roles_split_mix(uint32_t x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16;
    return x;
}
static inline bool swat_roles_eval_value(uint32_t value) { return (value&SWAT_ROLES_EVAL_BIT)!=0; }

// Simulation seed of training episode `episode` (1-based) of an env whose
// seed is env_seed. Odd, so the sim's xorshift state is never zero.
static inline uint32_t swat_roles_train_seed(uint32_t env_seed,uint32_t episode) {
    return (swat_roles_split_mix(env_seed^episode*0x9e3779b9u^0x3c6ef372u)&~SWAT_ROLES_EVAL_BIT)|1u;
}
// Training layout seed for an episode: the configured layout_seed, or with
// roles_layouts=N one of 1..N drawn from the episode seed. Both are below
// 2^31 by validation, so never an evaluation layout.
static inline uint32_t swat_roles_train_layout(uint32_t episode_seed,uint32_t fixed,int layouts) {
    if(layouts<=0) return fixed;
    return 1u+swat_roles_split_mix(episode_seed^0xa54ff53au)%(uint32_t)layouts;
}
// Held-out simulation and layout seeds of evaluation case `index`, episode
// `episode` (0-based). Fixed by the suite version; never depend on an env.
static inline uint32_t swat_roles_eval_seed(uint32_t suite,uint32_t index,uint32_t episode) {
    return swat_roles_split_mix(suite*0x27d4eb2fu^index*0x165667b1u^episode*0x9e3779b9u^0x510e527fu)|SWAT_ROLES_EVAL_BIT|1u;
}
static inline uint32_t swat_roles_eval_layout(uint32_t suite,uint32_t index,uint32_t episode) {
    return swat_roles_split_mix(suite*0x85ebca6bu^index*0xc2b2ae35u^episode*0x27d4eb2fu^0x9b05688cu)|SWAT_ROLES_EVAL_BIT;
}
#endif
