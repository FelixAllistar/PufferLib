#pragma once
typedef float obs_t;
#include "pufferenv.h"
#include "fpg_task.h"
#define OBS_SIZE FPG_OBS
#define NUM_ATNS 1
#define ACT_SIZES {12}
struct Log {
    float perf,score,episode_return,episode_length,n;
    float normal_flag,deaths,timeouts,augmented;
    float contact_episodes,contact_success,approach_episodes,approach_success;
    float jump_episodes,jump_success,stairs_episodes,stairs_success;
};
struct Env {
    Log log;Agent agents[1];int tag,boundary_reached,num_agents;
    uint32_t rng;FpgConfig cfg;FpgCurriculum curriculum;FpgState* state;
};
#include "fpg_config.h"
FPG_HD void fpg_log_episode(Log* l,const FpgState* s) {
    float won=s->status==FPG_SUCCESS;
    l->perf+=won;l->score+=won;l->episode_return+=won;l->episode_length+=s->tick;l->n++;
    l->normal_flag+=s->status==FPG_NORMAL_FLAG;l->deaths+=s->status==FPG_DEAD;
    l->timeouts+=s->status==FPG_TIMEOUT;l->augmented+=s->augmented;
    if(s->tier==0) {l->contact_episodes++;l->contact_success+=won;}
    if(s->tier==1) {l->approach_episodes++;l->approach_success+=won;}
    if(s->tier==2) {l->jump_episodes++;l->jump_success+=won;}
    if(s->tier==3) {l->stairs_episodes++;l->stairs_success+=won;}
}
void puf_log(Log* l,Dict* out) {
#define LOG(n) dict_set(out,#n,l->n)
    LOG(perf);LOG(score);LOG(episode_return);LOG(episode_length);LOG(normal_flag);LOG(deaths);LOG(timeouts);LOG(augmented);
#define RATE(n) dict_set(out,#n "_success_rate",l->n##_episodes?l->n##_success/l->n##_episodes:0);dict_set(out,#n "_episodes",l->n##_episodes)
    RATE(contact);RATE(approach);RATE(jump);RATE(stairs);
#undef LOG
#undef RATE
}
#ifndef FPG_HEADLESS
static void fpg_render_state(const FpgState* s) {
    if(!IsWindowReady()) {InitWindow(1024,600,"Mario FPG precision task");SetTargetFPS(60);}
    int camera=s->body.x-120;if(camera<0) camera=0;
    BeginDrawing();ClearBackground((Color){24,30,44,255});
    for(int y=2;y<16;y++) for(int x=0;x<FPG_COLS;x++) {
        int tile=fpg_tile(&s->world,x*16,y*16),sx=2*(x*16-camera);
        if(sx < -32||sx>1024) continue;
        if(tile==FPG_SOLID) {DrawRectangle(sx,32+y*32,32,32,(Color){166,124,88,255});DrawRectangleLines(sx,32+y*32,32,32,BLACK);}
        if(tile==FPG_POLE) DrawRectangle(sx+14,32+y*32,4,32,WHITE);
    }
    DrawTriangle((Vector2){(float)(2*(s->world.pole_col*16-camera)+16),128},
        (Vector2){(float)(2*(s->world.pole_col*16-camera)-12),140},
        (Vector2){(float)(2*(s->world.pole_col*16-camera)+16),150},YELLOW);
    DrawRectangle(2*(s->body.x+3-camera),32+2*(s->body.y+20),20,24,RED);
    DrawText(TextFormat("seed %u | tier %d | frame %d | x %d+%d/256 | vx %d/16",s->level_seed,s->tier,s->tick,s->body.x,s->body.xsub,s->body.vx),12,10,18,WHITE);
    DrawText("Arrows move / X jump / Z run / R replay reset / N new reset / 1-4 tier",12,557,18,WHITE);
    if(s->status) DrawText(s->status==FPG_SUCCESS?"FPG":s->status==FPG_NORMAL_FLAG?"ORDINARY FLAG":"TIMEOUT / FALL",380,72,28,YELLOW);
    EndDrawing();puf_web_vsync();
}
#endif
#ifndef PUFFER_GPU_ENV
void puf_init(Env* e,Dict* kwargs) {
    e->num_agents=1;e->cfg=fpg_config(kwargs);fpg_load_bank(kwargs);
    e->rng=fpg_hash((uint32_t)e->cfg.seed^(unsigned)e->rng)|1u;
    e->state=(FpgState*)calloc(1,sizeof(FpgState));if(!e->state) abort();
}
void puf_reset(Env* e) {
    fpg_reset_task(e->state,&e->cfg,fpg_bank,fpg_bank_count,&e->curriculum,&e->rng);
    if(e->agents[0].observations) for(int i=0;i<FPG_OBS;i++) e->agents[0].observations[i]=fpg_observation(e->state,&e->cfg,i);
}
void puf_step(Env* e) {
    float reward=fpg_step_task(e->state,&e->cfg,(int)e->agents[0].actions[0]);int done=e->state->status!=FPG_ACTIVE;
    if(done) {fpg_log_episode(&e->log,e->state);fpg_record(&e->curriculum,e->state);e->boundary_reached=1;puf_reset(e);}
    else for(int i=0;i<FPG_OBS;i++) e->agents[0].observations[i]=fpg_observation(e->state,&e->cfg,i);
    e->agents[0].rewards[0]=reward;e->agents[0].terminals[0]=(float)done;
}
void puf_render(Env* e) {
#ifndef FPG_HEADLESS
    fpg_render_state(e->state);
#else
    (void)e;
#endif
}
void puf_close(Env* e) {free(e->state);e->state=NULL;}
#endif
