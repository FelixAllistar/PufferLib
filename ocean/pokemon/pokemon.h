#pragma once
/* The original adapter is unchanged in pokemon_base.h. Keeping the optional
 * shaping hook separate leaves the engine, draft, masks, and observation ABI
 * untouched. This wrapper is for the native ./puffer entry point.
 */
#define puf_step pk_base_step
#include "pokemon_base.h"
#undef puf_step
#include "personality.h"
#include "behavior.h"
/* build.sh checks the entry header for this ABI declaration. Identical to base. */
typedef uint8_t obs_t;

/* One immutable config per native training process, initialized before workers.
 * Standalone match evaluation uses PKGame directly and does not use rewards.
 */
static double pk_personality_weights[PK_PERSONALITY_DIM];
static int pk_personality_enabled;
static double pk_behavior_weights[PK_BEHAVIOR_DIM];
static int pk_behavior_enabled;
static inline void pk_personality_configure(Ini* ini, const char* mode) {
    pk_configure(ini,mode);
    double norm=0;
    pk_personality_enabled=0;
    for(int k=0;k<PK_PERSONALITY_DIM;k++) {
        char key[64]; snprintf(key,sizeof(key),"personality_%s",pk_personality_names[k]);
        DictItem* item=dict_find(puf_ini_section(ini,"env",0),key);
        double value=item?item->value:0;
        if(!isfinite(value)) { fprintf(stderr,"Non-finite Pokemon personality\n"); exit(1); }
        pk_personality_weights[k]=value; norm+=fabs(value);
    }
    if(norm>1.000001) { fprintf(stderr,"Pokemon personality L1 norm must be <= 1\n"); exit(1); }
    pk_personality_enabled=norm>0;
    double behavior_norm=0;
    for(int k=0;k<PK_BEHAVIOR_DIM;k++) {
        char key[64]; snprintf(key,sizeof(key),"behavior_%s",pk_behavior_names[k]);
        DictItem* item=dict_find(puf_ini_section(ini,"env",0),key);
        double value=item?item->value:0;
        if(!isfinite(value)) { fprintf(stderr,"Non-finite behavior weight\n"); exit(1); }
        pk_behavior_weights[k]=value; behavior_norm+=fabs(value);
    }
    if(behavior_norm>1.000001 || (behavior_norm>0 && pk_personality_enabled)) {
        fprintf(stderr,"Behavior weights require L1<=1 and potential personality weights off\n"); exit(1);
    }
    pk_behavior_enabled=behavior_norm>0;
    DictItem* resets=dict_find(puf_ini_section(ini,"env",0),"reset_state_prob");
    if(resets && resets->value>0 && (pk_personality_enabled || pk_behavior_enabled)) {
        fprintf(stderr,"State-bank experiment requires win-only rewards\n"); exit(1);
    }
    if(pk_behavior_enabled && (puf_ini_get(ini,"train","reward_clip")!=0 ||
            puf_ini_get(ini,"env","reward_hp_scale")!=0 || puf_ini_get(ini,"env","reward_ko_scale")!=0)) {
        fprintf(stderr,"Behavior test requires reward clipping and HP/KO potentials off\n"); exit(1);
    }
    if(pk_personality_enabled && puf_ini_get(ini,"train","reward_clip")!=0) {
        fprintf(stderr,"Pokemon personalities require train.reward_clip=0\n"); exit(1);
    }
    if(!strcmp(mode,"train")) {
        printf("PK_PERSONALITY_CONFIG v=1 weights=");
        for(int k=0;k<PK_PERSONALITY_DIM;k++) printf("%s%.12g",k?",":"",pk_personality_weights[k]);
        printf("\n"); fflush(stdout);
        printf("PK_BEHAVIOR_CONFIG v=1 weights=%.12g,%.12g early_turns=5 paralysis_cap=3\n",
               pk_behavior_weights[0],pk_behavior_weights[1]); fflush(stdout);
    }
}
#undef PUF_CONFIGURE
#define PUF_CONFIGURE(ini,mode) pk_personality_configure(ini,mode)

void puf_step(Env* env) {
    if(!pk_personality_enabled && !pk_behavior_enabled) { pk_base_step(env); return; }
    double before=pk_personality_potential(env->game.obs[0],env->game.obs[1],pk_personality_weights);
    pk_base_step(env);
    /* Base step resets PKGame immediately on a terminal, while preserving the
     * terminal and reward. Do NOT compute successor potential from reset state.
     */
    int terminal=env->agents[0].terminals[0]!=0;
    double after=terminal?0:pk_personality_potential(env->game.obs[0],env->game.obs[1],pk_personality_weights);
    float extra=pk_behavior_enabled ? pk_behavior_extra(env->behavior_delta,pk_behavior_weights)
        : (float)pk_personality_shaping(before,after,env->reward_gamma,terminal);
    env->agents[0].rewards[0]+=extra;
    env->agents[1].rewards[0]=-env->agents[0].rewards[0];
    if(terminal) env->log.episode_return+=extra;
    else env->episode_return+=extra;
    pk_audit(env,0);
}
