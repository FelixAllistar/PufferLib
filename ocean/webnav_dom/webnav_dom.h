#pragma once
#include "pufferenv.h"
#include "../webnav/training.h"
#include "../webnav/training_contract.h"
#define OBS_SIZE WT_FEATURES
#define ACT_SIZES {WT_ACTIONS}
#define NUM_ATNS 1
typedef float obs_t;
static int wt_mode_potion,wt_hidden,wt_layers;
static void wt_configure(Ini *ini,const char *mode){
 int n=(int)puf_ini_get(ini,"vec","total_agents"),b=(int)puf_ini_get(ini,"vec","num_buffers");
 if(b<1||n<32*b||n%(32*b)){fprintf(stderr,"webnav_dom needs total_agents divisible by 32*num_buffers\n");exit(1);}
 wt_mode_potion=(int)puf_ini_get(ini,"env","potion");wt_hidden=(int)puf_ini_get(ini,"policy","hidden_size");wt_layers=(int)puf_ini_get(ini,"policy","num_layers");
 if(wt_mode_potion<0||wt_mode_potion>1||!strcmp(mode,"trace")){fprintf(stderr,"invalid webnav_dom mode/config; use training_eval for traces\n");exit(1);}
}
static void wt_checkpoint(const char *path,Ini *ini){if(wt_contract_write(path,wt_mode_potion,wt_hidden,wt_layers,(unsigned)puf_ini_get(ini,"env","seed"),(unsigned)puf_ini_get(ini,"env","task_mask"))){fprintf(stderr,"failed webnav checkpoint contract\n");exit(1);}}
static void wt_validate(const char *path){int p,h,l;if(wt_contract_read(path,&p,&h,&l)||p!=wt_mode_potion||h!=wt_hidden||l!=wt_layers){fprintf(stderr,"webnav checkpoint observation/encoder/architecture contract mismatch: %s\n",path);exit(1);}}
#define PUF_CONFIGURE wt_configure
#define PUF_CHECKPOINT_HOOK wt_checkpoint
#define PUF_VALIDATE_CHECKPOINT wt_validate
struct Log{float perf,score,episode_length,invalid,n;};
struct Env{Log log;int num_agents;unsigned rng;Agent agents[32];int tag,boundary_reached;uint32_t words[8192];WTView views[32];unsigned ticks[32],bad[32],tasks,seed,step_ms;WTFeatures *features;};
static unsigned wt_next(Env *e){e->rng=e->rng*1664525u+1013904223u;return e->rng;}
static void wt_reset_row(Env *e,unsigned i){unsigned count=0;for(unsigned t=0;t<12;t++)count+=(e->tasks>>t)&1;unsigned pick=wt_next(e)%count,task=0;for(;task<12;task++)if((e->tasks>>task)&1){if(!pick)break;pick--;}wt_request(e->words+256*i,task,(wt_next(e)^e->seed)&0x7fffffffU);e->ticks[i]=e->bad[i]=0;}
static void wt_observe_env(Env *e){for(unsigned i=0;i<32;i++){if(wt_view(e->words+256*i,e->views+i)){fprintf(stderr,"invalid generated public view\n");abort();}wt_features(e->features,e->views+i,(float*)e->agents[i].observations);if(e->agents[i].action_mask)wt_mask(e->views+i,e->agents[i].action_mask);}}
void puf_init(Env *e,Dict *kw){e->num_agents=32;e->tasks=(unsigned)dict_get(kw,"task_mask");e->seed=(unsigned)dict_get(kw,"seed");e->step_ms=(unsigned)dict_get(kw,"step_ms");if(!e->tasks||e->tasks>4095||!e->step_ms||e->step_ms>10000){fprintf(stderr,"invalid webnav task_mask/step_ms\n");abort();}e->features=wt_features_new((int)dict_get(kw,"potion"));if(!e->features){fprintf(stderr,"cannot load text feature backend; run test-text\n");abort();}for(int i=0;i<32;i++)e->agents[i].policy=0;}
void puf_reset(Env *e){for(unsigned i=0;i<32;i++)wt_reset_row(e,i);webnav_batch(e->words);wt_observe_env(e);}
void puf_step(Env *e){
 for(unsigned i=0;i<32;i++){unsigned elapsed=++e->ticks[i]*e->step_ms;e->bad[i]+=wt_action(e->words+256*i,e->views+i,(unsigned)e->agents[i].actions[0],elapsed)!=0;e->agents[i].rewards[0]=0;e->agents[i].terminals[0]=0;}
 webnav_batch(e->words);int reset=0;
 for(unsigned i=0;i<32;i++){uint32_t *r=e->words+256*i;if(wt_done(r)){e->agents[i].rewards[0]=wt_reward(r);e->agents[i].terminals[0]=1;e->log.perf+=wt_raw_reward(r)>=0.999f;e->log.score+=wt_reward(r);e->log.episode_length+=e->ticks[i];e->log.invalid+=e->bad[i];e->log.n++;wt_reset_row(e,i);reset=1;}else{wt_action(r,e->views+i,0,e->ticks[i]*e->step_ms);}}
 if(reset)webnav_batch(e->words);wt_observe_env(e);
}
void puf_log(Log *l,Dict *o){dict_set(o,"perf",l->perf);dict_set(o,"score",l->score);dict_set(o,"episode_length",l->episode_length);dict_set(o,"invalid",l->invalid);}
void puf_render(Env *e){(void)e;}
void puf_close(Env *e){wt_features_free(e->features);}
