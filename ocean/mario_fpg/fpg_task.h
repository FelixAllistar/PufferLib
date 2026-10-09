#pragma once
#include "fpg_core.h"
#include "fpg_enemies.h"

#define FPG_VERSION 1
#define FPG_PATH_MAX 240
#define FPG_EGO 32
#define FPG_GRID_W 16
#define FPG_GRID_H 13
#define FPG_OBS (FPG_EGO + FPG_GRID_W * FPG_GRID_H * 2)

typedef struct { uint32_t seed; int split, pole, height, gap, pipe_height; } FpgCourse;
typedef struct { FpgBody body; int action; } FpgFrame;
typedef struct { FpgCourse course; int length; FpgFrame frames[FPG_PATH_MAX]; } FpgCase;
typedef struct { int max_frames, seed, split, adaptive, fixed_tier, augment, contract_version; } FpgConfig;
typedef struct { unsigned int tries[4], wins[4]; float success[4]; } FpgCurriculum;
typedef struct {
    FpgBody body;
    FpgWorld world;
    FpgCamera camera;
    FpgActors actors;
    int tick, status, tier, case_index, reset_remaining, augmented;
    uint32_t level_seed;
} FpgState;

FPG_HD uint32_t fpg_rand(uint32_t* rng) {
    uint32_t x=*rng; x^=x<<13; x^=x>>17; x^=x<<5; *rng=x; return x;
}
FPG_HD uint32_t fpg_hash(uint32_t x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);
}
FPG_HD void fpg_generate(const FpgCourse* c, FpgWorld* w) {
    memset(w,0,sizeof(*w)); w->pole_col=c->pole;
    memset(w->known,1,sizeof(w->known));
    for(int col=0;col<FPG_COLS;col++) for(int row=13;row<16;row++) fpg_put(w,col,row,FPG_SOLID);
    int end=c->pole-c->gap-1, first=end-c->height;
    for(int col=first;col<=end;col++) {
        int h=col-first+1;if(h>c->height) h=c->height;
        for(int row=13-h;row<13;row++) fpg_put(w,col,row,FPG_SOLID);
    }
    // Authored post-pipe approach scenery; pipe entry/exit animation is not
    // implemented in this precision task. Root starts currently use the stairs.
    for(int col=first-8;col<first-6;col++)
        for(int row=13-c->pipe_height;row<13;row++) fpg_put(w,col,row,FPG_SOLID);
    for(int row=2;row<12;row++) fpg_put(w,c->pole,row,FPG_POLE);
    fpg_put(w,c->pole,12,FPG_SOLID);
}
FPG_HD FpgBody fpg_initial(const FpgCourse* c) {
    uint32_t rng=fpg_hash(c->seed)|1u;
    int step=3+(int)(fpg_rand(&rng)%3), end=c->pole-c->gap-1;
    int first=end-c->height;
    FpgBody b;memset(&b,0,sizeof(b));
    int phase=(int)(fpg_rand(&rng)%13);
    b.x=(first+step-1)*16+phase;
    // The right foot leads the visible origin by 12 pixels. Once it reaches
    // the next stair, a grounded reset must stand on that higher surface.
    int support=step+(phase>=4);
    b.y=(13-support)*16-32;
    b.vx=16+(int)(fpg_rand(&rng)%25);b.abs_vx=b.vx;
    b.xsub=(int)(fpg_rand(&rng)%16)*16;b.ax=(int)(fpg_rand(&rng)%64)*4;
    b.ysub=(int)(fpg_rand(&rng)%128)*2;
    b.facing=b.moving=1;b.run_timer=10;b.running=b.vx>=28?b.vx:0;
    b.gravity=40;b.fall_gravity=144;b.jump_y=b.y;b.collision=255;
    b.routine=8;b.flag_y=48;
    return b;
}
FPG_HD int fpg_outcome(const FpgBody* b) {
    if(b->routine==5 && b->grab_y>=162 && b->flag_y==48) return FPG_SUCCESS;
    if(b->routine==4 || b->routine==5) return FPG_NORMAL_FLAG;
    if(b->routine==11 || b->y>=240 || b->x<0) return FPG_DEAD;
    return FPG_ACTIVE;
}
FPG_HD int fpg_choose_tier(const FpgConfig* c,const FpgCurriculum* curriculum,uint32_t* rng) {
    if(c->fixed_tier>=0) return c->fixed_tier;
    if(!c->adaptive || fpg_rand(rng)%10<4) return (int)(fpg_rand(rng)%4);
    // A 10% floor per tier is retained even before mastery. The other 60%
    // goes to the earliest weak tier; this is reconsidered after every episode.
    for(int k=0;k<3;k++) if(curriculum->tries[k]<32 || curriculum->success[k]<0.75f) return k;
    return 3;
}
FPG_HD void fpg_record(FpgCurriculum* c,const FpgState* s) {
    int k=s->tier,won=s->status==FPG_SUCCESS;
    c->tries[k]++;c->wins[k]+=won;c->success[k]+=(won-c->success[k])*(1.0f/32);
}
FPG_HD void fpg_reset_task(FpgState* s,const FpgConfig* c,const FpgCase* bank,int count,
        const FpgCurriculum* curriculum,uint32_t* rng) {
    memset(s,0,sizeof(*s));
    int start=(int)(fpg_rand(rng)%(unsigned)count),which=start;
    // Cases are interleaved by split by the bank builder. Scan from a uniformly
    // selected position; equal split population is not required.
    while(bank[which].course.split!=c->split) which=(which+1)%count;
    const FpgCase* sample=&bank[which];
    s->tier=fpg_choose_tier(c,curriculum,rng);s->case_index=which;s->level_seed=sample->course.seed;
    int low=s->tier==0?1:s->tier==1?9:33;
    int high=s->tier==0?8:s->tier==1?32:96;
    if(high>sample->length) high=sample->length;
    if(low>high) low=high;
    int remaining=s->tier==3?sample->length:low+(int)(fpg_rand(rng)%(unsigned)(high-low+1));
    int offset=sample->length-remaining;s->reset_remaining=remaining;
    s->body=sample->frames[offset].body;fpg_generate(&sample->course,&s->world);
    if(c->contract_version>=2) {
        FpgBody prefix=sample->frames[0].body;
        s->camera=fpg_camera_initial(&prefix);
        if(s->world.pole_col>=s->camera.parsed_col)prefix.flag_y=0;
        for(int j=0;j<offset;j++)
            fpg_physics_camera(&prefix,&s->world,&s->camera,fpg_buttons(sample->frames[j].action));
        s->body=prefix;
    }
    if(c->augment) for(int retry=0;retry<4;retry++) {
        FpgBody candidate=s->body;
        candidate.xsub=(int)(fpg_rand(rng)%16)*16;
        candidate.ax=(int)(fpg_rand(rng)%64)*4;
        // Accept phase changes only when the known synthetic suffix still
        // succeeds. This prevents randomized reset states with no solution.
        FpgBody probe=candidate;
        FpgCamera camera=s->camera;
        for(int j=offset;j<sample->length && probe.routine==8;j++)
            fpg_physics_camera(&probe,&s->world,&camera,fpg_buttons(sample->frames[j].action));
        if(fpg_outcome(&probe)==FPG_SUCCESS) {s->body=candidate;s->augmented=1;break;}
    }
}
FPG_HD float fpg_step_task(FpgState* s,const FpgConfig* c,int action) {
    if(s->status) return 0;
    if(action<0||action>=12) action=0;
    if(c->contract_version>=2) {
        int bounds[4]={};fpg_actor_timers(&s->actors);
        fpg_player_frame(&s->body,&s->world,&s->camera,fpg_buttons(action),bounds);
        fpg_actors_step(&s->actors,&s->body,&s->world,&s->camera,bounds);
    } else fpg_physics_camera(&s->body,&s->world,&s->camera,fpg_buttons(action));
    s->tick++;
    s->status=fpg_outcome(&s->body);
    if(!s->status && s->tick>=c->max_frames) s->status=FPG_TIMEOUT;
    return s->status==FPG_SUCCESS?1.0f:0.0f;
}
// Contract v2: the same camera window and vertical extent in both engines.
// Unknown columns use (-1,-1), distinct from known empty cells (0,0).
FPG_HD int fpg_visible_column(const FpgState* s,int col) {
    return col>=fpg_floor16(s->camera.left) && col<=fpg_floor16(s->camera.left+255)
        && fpg_column_known(&s->world,col);
}
FPG_HD float fpg_observation(const FpgState* s,const FpgConfig* c,int i) {
    const FpgBody* b=&s->body;
    if(i<FPG_EGO) {
        switch(i) {
        case 0:return b->vx/40.0f;case 1:return (b->vy+b->vyfrac/256.0f)/5;
        case 2:return b->xsub/256.0f;case 3:return b->ysub/256.0f;case 4:return b->ax/256.0f;
        case 5:return (b->x&15)/16.0f;case 6:return (b->y&15)/16.0f;
        case 7:return b->motion==0;case 8:return b->motion==1;case 9:return b->motion==2;
        case 10:return b->facing==1?1.0f:-1.0f;case 11:return b->moving==1?1.0f:-1.0f;
        case 12:return b->abs_vx/40.0f;case 13:return b->running/40.0f;
        case 14:return b->run_timer/10.0f;case 15:return b->gravity/256.0f;
        case 16:return b->fall_gravity/256.0f;case 17:return (b->jump_y-b->y)/128.0f;
        case 18:return (b->previous_ab&FPG_A)!=0;case 19:return (b->previous_ab&FPG_B)!=0;
        case 20:return (b->collision&1)!=0;case 21:return (b->collision&2)!=0;
        case 22:return b->side_timer/16.0f;
        case 23:return c->contract_version>=2&&!fpg_visible_column(s,s->world.pole_col)
            ?0:(s->world.pole_col*16+6-b->x)/512.0f;
        case 24:return (162-b->y)/208.0f;
        case 25:return (c->max_frames-s->tick)/(float)c->max_frames;
        case 26:return b->vyfrac/256.0f;
        case 27:return c->contract_version>=2?fpg_visible_column(s,s->world.pole_col):0;
        case 28:return c->contract_version>=2?(b->x-s->camera.left)/256.0f:0;
        case 29:case 30:case 31: {
            if(c->contract_version<2)return 0;
            const FpgEnemy* nearest=NULL;int distance=100000;
            for(int k=0;k<FPG_ENEMIES;k++) {
                const FpgEnemy* e=&s->actors.slots[k];int dx=e->x-b->x;if(dx<0)dx=-dx;
                if(e->active&&e->type==6&&e->state<2&&fpg_enemy_visible(e,&s->camera)&&dx<distance){nearest=e;distance=dx;}
            }
            if(!nearest)return 0;
            return i==29?(nearest->x-b->x)/256.0f:i==30?(nearest->y-b->y)/256.0f:nearest->vx/16.0f;
        }
        // No reset-tier/teacher-action identifiers in observations.
        default:return 0;
        }
    }
    i-=FPG_EGO;int channel=i%2,cell=i/2;
    int x=(fpg_floor16(b->x)-4+cell%FPG_GRID_W)*16;
    int y=(fpg_floor16(b->y+32)-8+cell/FPG_GRID_W)*16;
    if(c->contract_version>=2) {
        if(!fpg_visible_column(s,fpg_floor16(x))) return -1;
        if(y<32||y>=240) return 0;
    }
    int tile=fpg_tile(&s->world,x,y);
    if(c->contract_version>=2&&channel==1)for(int k=0;k<FPG_ENEMIES;k++) {
        const FpgEnemy* e=&s->actors.slots[k];
        if(e->active&&e->type==6&&e->state<2&&e->x+8>=x&&e->x+8<x+16&&e->y+16>=y&&e->y+16<y+16)return 2;
    }
    return channel==0 ? tile==FPG_SOLID : tile==FPG_POLE;
}
