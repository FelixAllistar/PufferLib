#pragma once
/* The original adapter is unchanged in pokemon_base.h. Keeping the optional
 * shaping hook separate leaves the engine, draft, masks, and observation ABI
 * untouched. This wrapper is for the native ./puffer entry point.
 */
#define puf_step pk_base_step
#include "pokemon_base.h"
#undef puf_step
#include "personality.h"
/* build.sh checks the entry header for this ABI declaration. Identical to base. */
typedef uint8_t obs_t;

/* One immutable config per native training process, initialized before workers.
 * Standalone match evaluation uses PKGame directly and does not use rewards.
 */
static double pk_personality_weights[PK_PERSONALITY_DIM];
static int pk_personality_enabled;
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
    if(pk_personality_enabled && puf_ini_get(ini,"train","reward_clip")!=0) {
        fprintf(stderr,"Pokemon personalities require train.reward_clip=0\n"); exit(1);
    }
    if(!strcmp(mode,"train")) {
        printf("PK_PERSONALITY_CONFIG v=1 weights=");
        for(int k=0;k<PK_PERSONALITY_DIM;k++) printf("%s%.12g",k?",":"",pk_personality_weights[k]);
        printf("\n"); fflush(stdout);
    }
}
#undef PUF_CONFIGURE
#define PUF_CONFIGURE(ini,mode) pk_personality_configure(ini,mode)

void puf_step(Env* env) {
    if(!pk_personality_enabled) { pk_base_step(env); return; }
    double before=pk_personality_potential(env->game.obs[0],env->game.obs[1],pk_personality_weights);
    pk_base_step(env);
    /* Base step resets PKGame immediately on a terminal, while preserving the
     * terminal and reward. Do NOT compute successor potential from reset state.
     */
    int terminal=env->agents[0].terminals[0]!=0;
    double after=terminal?0:pk_personality_potential(env->game.obs[0],env->game.obs[1],pk_personality_weights);
    float extra=(float)pk_personality_shaping(before,after,env->reward_gamma,terminal);
    env->agents[0].rewards[0]+=extra;
    env->agents[1].rewards[0]=-env->agents[0].rewards[0];
    if(terminal) env->log.episode_return+=extra;
    else env->episode_return+=extra;
    pk_audit(env,0);
}
