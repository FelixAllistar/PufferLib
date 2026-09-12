#pragma once
// CPU campaign: persistent coordinate-addressed chunks around the bounded shared
// simulator. Training still uses the same combat/economy in a fixed 64x64 window.
#ifndef AR_GPU_SIM
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>

#define AR_CHUNK_SIZE 16
#define AR_CHUNK_CELLS (AR_CHUNK_SIZE*AR_CHUNK_SIZE)

typedef struct {
    int32_t x,y;
    uint8_t tiles[AR_CHUNK_CELLS];
} ARWorldChunk;

typedef struct {
    double x,y,home_x,home_y;
    float hp,max_hp,value,cd,aux;
    int32_t kind,active;
} ARWorldEntity;

typedef struct {
    double x,y,goal_x,goal_y;
    float hp,cd,work_cd;
    int32_t active,kind,command,task;
} ARWorldPet;

typedef struct {
    uint32_t seed;
    int32_t origin_x,origin_y;
    double home_x,home_y;
    ARWorldChunk* chunks;
    int chunk_count,chunk_capacity;
    ARWorldEntity *resources,*buildings,*nests,*enemies;
    int resource_count,resource_capacity,building_count,building_capacity;
    int nest_count,nest_capacity,enemy_count,enemy_capacity;
    int resource_id[AR_MAX_SHARDS],building_id[AR_MAX_BUILDINGS];
    int nest_id[AR_MAX_NESTS],enemy_id[AR_MAX_ENEMIES];
    ARWorldPet pets[AR_MAX_PETS];
    int task_override[AR_MAX_PETS];
    int respawns,last_capture_tick;
} ARWorld;

static inline void* ar_world_grow(void* data,int* capacity,int count,size_t stride) {
    if(count<=*capacity)return data;
    int cap=*capacity ? *capacity*2 : 32;
    if(cap<count)cap=count;
    void* next=realloc(data,(size_t)cap*stride);
    if(!next){fprintf(stderr,"Hearthwild: insufficient memory for frontier state\n");abort();}
    *capacity=cap;return next;
}
static inline int ar_world_add(ARWorldEntity** array,int* count,int* capacity,ARWorldEntity value) {
    *array=(ARWorldEntity*)ar_world_grow(*array,capacity,*count+1,sizeof(**array));
    (*array)[*count]=value;return (*count)++;
}
static inline float ar_world_cell(ARPG* e){return e->cfg.arena_size/AR_DUN_W;}
static inline double ar_world_ox(ARPG* e,ARWorld* w){return (double)w->origin_x*ar_world_cell(e);}
static inline double ar_world_oy(ARPG* e,ARWorld* w){return (double)w->origin_y*ar_world_cell(e);}
static inline int ar_world_inside(ARPG* e,ARWorld* w,double x,double y) {
    double edge=e->cfg.arena_size*0.5-0.8;
    return fabs(x-ar_world_ox(e,w))<edge && fabs(y-ar_world_oy(e,w))<edge;
}
static inline void ar_world_ids_clear(ARWorld* w) {
    for(int n=0;n<AR_MAX_SHARDS;n++)w->resource_id[n]=-1;
    for(int b=0;b<AR_MAX_BUILDINGS;b++)w->building_id[b]=-1;
    for(int n=0;n<AR_MAX_NESTS;n++)w->nest_id[n]=-1;
    for(int n=0;n<AR_MAX_ENEMIES;n++)w->enemy_id[n]=-1;
}

static inline ARWorldChunk* ar_world_chunk(ARPG* e,ARWorld* w,int cx,int cy,int populate) {
    for(int i=0;i<w->chunk_count;i++)if(w->chunks[i].x==cx && w->chunks[i].y==cy)return w->chunks+i;
    w->chunks=(ARWorldChunk*)ar_world_grow(w->chunks,&w->chunk_capacity,w->chunk_count+1,sizeof(*w->chunks));
    ARWorldChunk* c=w->chunks+w->chunk_count++;memset(c,0,sizeof(*c));c->x=cx;c->y=cy;
    for(int y=0;y<AR_CHUNK_SIZE;y++)for(int x=0;x<AR_CHUNK_SIZE;x++)
        c->tiles[y*AR_CHUNK_SIZE+x]=ar_terrain_tile(w->seed,cx*AR_CHUNK_SIZE+x,cy*AR_CHUNK_SIZE+y);
    if(!populate)return c;
    uint32_t hash=ar_hash_xy(w->seed^0x13a73u,cx,cy);
    float cell=ar_world_cell(e);
    // One renewable seam per region. Its position depends only on the seed/key.
    for(int t=0;t<AR_CHUNK_CELLS;t++) {
        int i=(int)((hash+(uint32_t)t*37u)%AR_CHUNK_CELLS);
        if(c->tiles[i]==AR_TILE_ROCK || c->tiles[i]==AR_TILE_DEEP)continue;
        ARWorldEntity r={0};r.x=(cx*AR_CHUNK_SIZE+i%AR_CHUNK_SIZE+0.5)*cell;
        r.y=(cy*AR_CHUNK_SIZE+i/AR_CHUNK_SIZE+0.5)*cell;r.active=1;r.value=8;
        ar_world_add(&w->resources,&w->resource_count,&w->resource_capacity,r);break;
    }
    if(hash%3u==0) {
        int x=6+(int)((hash>>8)%5),y=6+(int)((hash>>12)%5),tile=c->tiles[y*AR_CHUNK_SIZE+x];
        double wx=(cx*AR_CHUNK_SIZE+x+0.5)*cell,wy=(cy*AR_CHUNK_SIZE+y+0.5)*cell;
        if(tile!=AR_TILE_ROCK && tile!=AR_TILE_DEEP && wx*wx+wy*wy>20*20*cell*cell) {
            ARWorldEntity n={0};n.x=wx;n.y=wy;n.active=1;n.hp=n.max_hp=e->cfg.nest_hp;
            n.cd=3+(hash%7);ar_world_add(&w->nests,&w->nest_count,&w->nest_capacity,n);
        }
    }
    return c;
}

static inline void ar_world_tiles(ARPG* e,ARWorld* w,int capture,int populate) {
    int minx=(w->origin_x-AR_DUN_W/2)/AR_CHUNK_SIZE,miny=(w->origin_y-AR_DUN_H/2)/AR_CHUNK_SIZE;
    for(int cy=0;cy<AR_DUN_H/AR_CHUNK_SIZE;cy++)for(int cx=0;cx<AR_DUN_W/AR_CHUNK_SIZE;cx++) {
        ARWorldChunk* c=ar_world_chunk(e,w,minx+cx,miny+cy,populate);
        for(int y=0;y<AR_CHUNK_SIZE;y++) {
            uint8_t* row=e->dungeon+(cy*AR_CHUNK_SIZE+y)*AR_DUN_W+cx*AR_CHUNK_SIZE;
            if(capture)memcpy(c->tiles+y*AR_CHUNK_SIZE,row,AR_CHUNK_SIZE);
            else memcpy(row,c->tiles+y*AR_CHUNK_SIZE,AR_CHUNK_SIZE);
        }
    }
}

static inline uint8_t ar_world_sample(ARPG* e,int x,int y) {
    if(x>=0 && x<AR_DUN_W && y>=0 && y<AR_DUN_H)return e->dungeon[y*AR_DUN_W+x];
    ARWorld* w=(ARWorld*)e->campaign;if(!w)return AR_TILE_ROCK;
    int gx=w->origin_x+x-AR_DUN_W/2,gy=w->origin_y+y-AR_DUN_H/2;
    int cx=(int)floor((double)gx/AR_CHUNK_SIZE),cy=(int)floor((double)gy/AR_CHUNK_SIZE);
    for(int i=0;i<w->chunk_count;i++)if(w->chunks[i].x==cx && w->chunks[i].y==cy)
        return w->chunks[i].tiles[(gy-cy*AR_CHUNK_SIZE)*AR_CHUNK_SIZE+gx-cx*AR_CHUNK_SIZE];
    return ar_terrain_tile(w->seed,gx,gy);
}

static inline void ar_world_capture(ARPG* e) {
    ARWorld* w=(ARWorld*)e->campaign;if(!w)return;
    double ox=ar_world_ox(e,w),oy=ar_world_oy(e,w);
    ar_world_tiles(e,w,1,0);
    for(int n=0;n<AR_MAX_SHARDS;n++) {
        int id=w->resource_id[n];
        if(id<0 && !e->shard_active[n])continue;
        ARWorldEntity r={0};r.x=ox+e->shard_x[n];r.y=oy+e->shard_y[n];
        r.active=e->shard_active[n];r.value=e->shard_value[n];r.cd=e->shard_cd[n];
        if(id<0)w->resource_id[n]=ar_world_add(&w->resources,&w->resource_count,&w->resource_capacity,r);
        else w->resources[id]=r;
    }
    for(int b=0;b<AR_MAX_BUILDINGS;b++) {
        int id=w->building_id[b];
        if(id<0 && !e->build_active[b])continue;
        ARWorldEntity r={0};r.x=ox+e->build_x[b];r.y=oy+e->build_y[b];r.kind=e->build_kind[b];
        r.active=e->build_active[b];r.hp=e->build_hp[b];r.max_hp=e->build_max_hp[b];r.cd=e->build_cd[b];
        if(id>=0 && r.active && (fabs(w->buildings[id].x-r.x)>0.05 || fabs(w->buildings[id].y-r.y)>0.05 || w->buildings[id].kind!=r.kind)) {
            w->buildings[id].active=0;id=-1;
        }
        if(id<0)w->building_id[b]=ar_world_add(&w->buildings,&w->building_count,&w->building_capacity,r);
        else w->buildings[id]=r;
    }
    for(int n=0;n<AR_MAX_NESTS;n++) {
        int id=w->nest_id[n];if(id<0 && !e->nest_active[n])continue;
        ARWorldEntity r={0};r.x=ox+e->nest_x[n];r.y=oy+e->nest_y[n];r.active=e->nest_active[n];
        r.hp=e->nest_hp[n];r.max_hp=e->nest_max_hp[n];r.cd=e->nest_cd[n];
        if(id<0)w->nest_id[n]=ar_world_add(&w->nests,&w->nest_count,&w->nest_capacity,r);
        else w->nests[id]=r;
    }
    for(int i=0;i<e->cfg.enemy_cap;i++) {
        int id=w->enemy_id[i];if(id<0 && !e->enemies.active[i])continue;
        ARWorldEntity r={0};r.x=ox+e->enemies.x[i];r.y=oy+e->enemies.y[i];
        r.home_x=ox+e->enemies.home_x[i];r.home_y=oy+e->enemies.home_y[i];
        r.active=e->enemies.active[i];r.kind=e->enemies.type[i];r.hp=e->enemies.hp[i];r.max_hp=e->enemies.max_hp[i];
        r.cd=(float)e->enemies.slow_timer[i];r.value=e->enemies.speed[i];r.aux=e->enemies.damage[i];
        if(id>=0 && r.active && (fabs(w->enemies[id].home_x-r.home_x)>0.05 || fabs(w->enemies[id].home_y-r.home_y)>0.05)) {
            w->enemies[id].active=0;id=-1;
        }
        if(id<0)w->enemy_id[i]=ar_world_add(&w->enemies,&w->enemy_count,&w->enemy_capacity,r);
        else w->enemies[id]=r;
    }
    for(int p=0;p<AR_MAX_PETS;p++) {
        ARWorldPet* r=w->pets+p;
        r->active=e->pets.active[p];r->kind=e->pets.kind[p];r->command=e->pets.command[p];r->task=e->pets.task[p];
        if(!e->pets.dormant[p]){r->x=ox+e->pets.x[p];r->y=oy+e->pets.y[p];r->hp=e->pets.hp[p];r->cd=e->pets.cd[p];r->work_cd=e->pets.work_cd[p];}
        r->goal_x=ox+e->pets.goal_x[p];r->goal_y=oy+e->pets.goal_y[p];
    }
    w->last_capture_tick=e->tick;
}

static inline void ar_world_activate(ARPG* e) {
    ARWorld* w=(ARWorld*)e->campaign;
    double ox=ar_world_ox(e,w),oy=ar_world_oy(e,w);
    ar_world_tiles(e,w,0,1);ar_world_ids_clear(w);
    if(!B3_IS_NULL(e->world))b3DestroyWorld(e->world);
    e->world=ar_phys_create_world(&e->cfg);
    e->player_body=ar_phys_dynamic_body(e->world,e->px,e->py,e->cfg.player_radius,4);
    for(int i=0;i<AR_MAX_OBSTACLES;i++){e->obstacle_active[i]=0;e->obstacle_body[i]=b3_nullBodyId;}
    for(int n=0;n<AR_MAX_SHARDS;n++)e->shard_active[n]=0;
    for(int b=0;b<AR_MAX_BUILDINGS;b++)e->build_active[b]=0;
    for(int n=0;n<AR_MAX_NESTS;n++)e->nest_active[n]=0;
    for(int i=0;i<AR_MAX_ENEMIES;i++){e->enemies.active[i]=0;e->enemies.dense_pos[i]=-1;e->enemy_body[i]=b3_nullBodyId;}
    e->enemy_count=0;e->next_enemy_slot=0;e->nests_alive=0;e->builds_alive=0;e->pets_alive=0;e->nearest_enemy=-1;
    int slot=0;
    for(int i=0;i<w->resource_count && slot<AR_MAX_SHARDS;i++) {
        ARWorldEntity r=w->resources[i];if(!r.active || !ar_world_inside(e,w,r.x,r.y))continue;
        w->resource_id[slot]=i;e->shard_active[slot]=1;e->shard_x[slot]=(float)(r.x-ox);e->shard_y[slot]=(float)(r.y-oy);
        e->shard_value[slot]=r.value;e->shard_cd[slot]=r.cd;slot++;
    }
    slot=0;
    for(int i=0;i<w->building_count && slot<AR_MAX_BUILDINGS;i++) {
        ARWorldEntity r=w->buildings[i];if(!r.active || !ar_world_inside(e,w,r.x,r.y))continue;
        w->building_id[slot]=i;e->build_active[slot]=1;e->build_kind[slot]=(uint8_t)r.kind;
        e->build_x[slot]=(float)(r.x-ox);e->build_y[slot]=(float)(r.y-oy);
        e->build_hp[slot]=r.hp;e->build_max_hp[slot]=r.max_hp;e->build_cd[slot]=r.cd;
        e->build_rad[slot]=e->cfg.build_radius[r.kind];e->build_hurtcd[slot]=0;e->build_flash[slot]=0;slot++;
    }
    e->builds_alive=slot;slot=0;
    for(int i=0;i<w->nest_count && slot<AR_MAX_NESTS;i++) {
        ARWorldEntity r=w->nests[i];if(!r.active || !ar_world_inside(e,w,r.x,r.y))continue;
        w->nest_id[slot]=i;e->nest_active[slot]=1;e->nest_x[slot]=(float)(r.x-ox);e->nest_y[slot]=(float)(r.y-oy);
        e->nest_hp[slot]=r.hp;e->nest_max_hp[slot]=r.max_hp;e->nest_cd[slot]=r.cd;slot++;
    }
    e->nests_alive=slot;
    for(int i=0;i<w->enemy_count && e->enemy_count<e->cfg.enemy_cap;i++) {
        ARWorldEntity r=w->enemies[i];if(!r.active || !ar_world_inside(e,w,r.x,r.y))continue;
        int n=ar_spawn_enemy(e,0,r.kind,(float)(r.x-ox),(float)(r.y-oy),1,1);if(n<0)break;
        w->enemy_id[n]=i;e->enemies.hp[n]=r.hp;e->enemies.max_hp[n]=r.max_hp;
        e->enemies.home_x[n]=(float)(r.home_x-ox);e->enemies.home_y[n]=(float)(r.home_y-oy);
        e->enemies.slow_timer[n]=(int)r.cd;e->enemies.speed[n]=r.value;e->enemies.damage[n]=r.aux;
    }
    for(int p=0;p<AR_MAX_PETS;p++) {
        ARWorldPet* r=w->pets+p;e->pet_body[p]=b3_nullBodyId;e->pets.active[p]=(uint8_t)r->active;
        e->pets.dormant[p]=0;if(!r->active)continue;e->pets_alive++;
        int follows=r->command==AR_CMD_AUTO && r->task!=AR_TASK_GATHER && r->task!=AR_TASK_HOLD && r->task!=AR_TASK_HOME && r->task!=AR_TASK_WORK &&
            r->kind!=AR_PET_MULE && r->kind!=AR_PET_EMBER;
        // Only unassigned escorts may phase back to their summoner. A worker or
        // manually stationed pet stays at its real world position when unloaded.
        if(follows && !ar_world_inside(e,w,r->x,r->y)){r->x=ox+e->px+(p%3-1)*1.2;r->y=oy+e->py+1.5;}
        e->pets.x[p]=(float)(r->x-ox);e->pets.y[p]=(float)(r->y-oy);
        e->pets.goal_x[p]=(float)(r->goal_x-ox);e->pets.goal_y[p]=(float)(r->goal_y-oy);
        e->pets.kind[p]=(uint8_t)r->kind;e->pets.command[p]=r->command;e->pets.task[p]=r->task;
        e->pets.hp[p]=r->hp;e->pets.max_hp[p]=e->cfg.pet_health[r->kind];e->pets.cd[p]=r->cd;e->pets.work_cd[p]=r->work_cd;
        e->pets.spd[p]=e->cfg.pet_speed[r->kind];e->pets.dmg[p]=e->cfg.pet_damage[r->kind];e->pets.rad[p]=e->cfg.pet_radius[r->kind];
        e->pets.vx[p]=e->pets.vy[p]=0;e->pets.target[p]=e->pets.ntarget[p]=-1;e->pets.nav_tick[p]=0;
        e->pets.dormant[p]=(uint8_t)!ar_world_inside(e,w,r->x,r->y);
        if(!e->pets.dormant[p])e->pet_body[p]=ar_phys_dynamic_body(e->world,e->pets.x[p],e->pets.y[p],e->pets.rad[p],2);
    }
    e->home_x=(float)(w->home_x-ox);e->home_y=(float)(w->home_y-oy);
    ar_compute_observations(e,0);
}

static inline void ar_world_begin(ARPG* e) {
    if(e->campaign)return;
    ARWorld* w=(ARWorld*)calloc(1,sizeof(*w));if(!w)abort();e->campaign=w;
    w->seed=e->dungeon_seed;w->home_x=e->home_x;w->home_y=e->home_y;
    for(int p=0;p<AR_MAX_PETS;p++)w->task_override[p]=-1;
    ar_world_ids_clear(w);ar_world_capture(e);
}
static inline void ar_world_close(ARPG* e) {
    ARWorld* w=(ARWorld*)e->campaign;if(!w)return;
    free(w->chunks);free(w->resources);free(w->buildings);free(w->nests);free(w->enemies);free(w);e->campaign=NULL;
}
static inline void ar_world_shift(ARPG* e,int dx,int dy) {
    ARWorld* w=(ARWorld*)e->campaign;if(!w || (!dx && !dy))return;
    ar_world_capture(e);w->origin_x+=dx;w->origin_y+=dy;
    float sx=dx*ar_world_cell(e),sy=dy*ar_world_cell(e);
    e->px-=sx;e->py-=sy;e->rally_x-=sx;e->rally_y-=sy;e->blast_x-=sx;e->blast_y-=sy;
    ar_world_activate(e);
}

static inline int ar_world_id_loaded(const int* ids,int count,int id) {
    for(int i=0;i<count;i++)if(ids[i]==id)return 1;return 0;
}
static inline void ar_world_yield(ARPG* e,ARWorldEntity* node) {
    if(node->value<1 || node->cd>0)return;
    node->value-=1;node->cd=e->cfg.gather_period;e->shards+=1;e->harvested+=1;
    e->episode_return+=e->cfg.reward_harvest;e->episode_reward_economy+=e->cfg.reward_harvest;
    e->agents[0].rewards[0]+=e->cfg.reward_harvest;
}

static inline uint8_t ar_world_global_tile(ARPG* e,ARWorld* w,int gx,int gy) {
    return ar_world_sample(e,gx-w->origin_x+AR_DUN_W/2,gy-w->origin_y+AR_DUN_H/2);
}
static inline void ar_world_remote_move(ARPG* e,ARWorld* w,ARWorldPet* pet,double tx,double ty,float stop) {
    double dx=tx-pet->x,dy=ty-pet->y,d=sqrt(dx*dx+dy*dy);
    if(d<=stop)return;
    float cell=ar_world_cell(e),distance=fminf(e->cfg.pet_speed[pet->kind],(float)d-stop);
    double nx=pet->x+dx/d*distance,ny=pet->y+dy/d*distance;
    int clear=1,steps=(int)(distance/cell*4)+1;
    for(int s=1;s<=steps;s++) {
        double x=pet->x+(nx-pet->x)*s/steps,y=pet->y+(ny-pet->y)*s/steps;
        int tile=ar_world_global_tile(e,w,(int)floor(x/cell),(int)floor(y/cell));
        if(tile==AR_TILE_ROCK||tile==AR_TILE_DEEP){clear=0;break;}
    }
    if(!clear) {
        // Coarse workers use the same grid navigation over a temporary window
        // around themselves, not the keeper's distant simulation window.
        int cx=(int)floor(pet->x/cell),cy=(int)floor(pet->y/cell);
        uint8_t tiles[AR_DUN_CELLS];
        for(int y=0;y<AR_DUN_H;y++)for(int x=0;x<AR_DUN_W;x++)
            tiles[y*AR_DUN_W+x]=ar_world_global_tile(e,w,cx+x-AR_DUN_W/2,cy+y-AR_DUN_H/2);
        float local_x=(float)(pet->x-cx*cell),local_y=(float)(pet->y-cy*cell),wx,wy;
        if(!ar_nav_next(tiles,e->cfg.arena_size,local_x,local_y,(float)(tx-cx*cell),(float)(ty-cy*cell),&wx,&wy))return;
        dx=wx-local_x;dy=wy-local_y;d=sqrt(dx*dx+dy*dy);
        if(d<0.01)return;distance=fminf(distance,(float)d);
        nx=pet->x+dx/d*distance;ny=pet->y+dy/d*distance;
    }
    pet->x=nx;pet->y=ny;
}

// Unloaded regions simulate production once per simulation second. They do not
// simulate combat, raids or elapsed time while the application is closed.
static inline void ar_world_offscreen_tick(ARPG* e) {
    ARWorld* w=(ARWorld*)e->campaign;
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.dormant[p] && w->pets[p].active) {
        ARWorldPet* pet=w->pets+p;double tx=pet->goal_x,ty=pet->goal_y;float stop=1.0f;int move=1;
        if(pet->command==AR_CMD_AUTO) {
            tx=w->home_x;ty=w->home_y;stop=e->cfg.pet_follow_distance;
            if(pet->task==AR_TASK_GATHER || (pet->task==AR_TASK_AUTO && pet->kind==AR_PET_MULE)) {
                double best=e->cfg.pet_leash_range*e->cfg.pet_leash_range;int found=-1;
                for(int n=0;n<w->resource_count;n++) {
                    ARWorldEntity r=w->resources[n];double dx=r.x-w->home_x,dy=r.y-w->home_y;
                    if(r.active && r.value>=1 && dx*dx+dy*dy<best){best=dx*dx+dy*dy;found=n;}
                }
                if(found>=0){tx=w->resources[found].x;ty=w->resources[found].y;stop=1.0f;}
                else move=0;
            } else if(pet->kind==AR_PET_EMBER && pet->task==AR_TASK_AUTO) {
                double best=1e30;int found=-1;
                for(int b=0;b<w->building_count;b++) {
                    ARWorldEntity r=w->buildings[b];double dx=r.x-pet->x,dy=r.y-pet->y;
                    if(r.active && r.kind==AR_BUILD_HARVESTER && dx*dx+dy*dy<best){best=dx*dx+dy*dy;found=b;}
                }
                if(found>=0){tx=w->buildings[found].x;ty=w->buildings[found].y;stop=1.4f;}else move=0;
            } else if(pet->task==AR_TASK_HOLD || pet->task==AR_TASK_WORK)move=0;
            else if(pet->task!=AR_TASK_HOME){tx=ar_world_ox(e,w)+e->px;ty=ar_world_oy(e,w)+e->py;}
        } else if(pet->command==AR_CMD_WORK)move=0; // excavation resumes when its region is active
        else if(pet->command==AR_CMD_ATTACK)stop=1.8f;
        if(move)ar_world_remote_move(e,w,pet,tx,ty,stop);
        if(pet->command==AR_CMD_MOVE && (pet->x-tx)*(pet->x-tx)+(pet->y-ty)*(pet->y-ty)<1.5)pet->command=AR_CMD_HOLD;
        e->pets.x[p]=(float)(pet->x-ar_world_ox(e,w));e->pets.y[p]=(float)(pet->y-ar_world_oy(e,w));e->pets.command[p]=pet->command;
        if(ar_world_inside(e,w,pet->x,pet->y)) {
            e->pets.dormant[p]=0;e->pets.nav_tick[p]=0;
            e->pet_body[p]=ar_phys_dynamic_body(e->world,e->pets.x[p],e->pets.y[p],e->pets.rad[p],2);
        }
    }
    for(int n=0;n<w->resource_count;n++) {
        if(ar_world_id_loaded(w->resource_id,AR_MAX_SHARDS,n))continue;
        ARWorldEntity* r=w->resources+n;if(!r->active)continue;
        r->value=fminf(8,r->value+e->cfg.resource_regen);r->cd=fmaxf(0,r->cd-1);
        for(int p=0;p<AR_MAX_PETS;p++) {
            ARWorldPet* pet=w->pets+p;
            if(!pet->active || !e->pets.dormant[p])continue;
            int gathers=pet->command==AR_CMD_GATHER || (pet->command==AR_CMD_AUTO &&
                (pet->task==AR_TASK_GATHER || (pet->task==AR_TASK_AUTO && pet->kind==AR_PET_MULE)));
            double dx=pet->x-r->x,dy=pet->y-r->y;
            double gx=pet->goal_x-r->x,gy=pet->goal_y-r->y;
            if(gathers && dx*dx+dy*dy<2.56 && (pet->command!=AR_CMD_GATHER || gx*gx+gy*gy<9))ar_world_yield(e,r);
        }
    }
    for(int b=0;b<w->building_count;b++) {
        if(ar_world_id_loaded(w->building_id,AR_MAX_BUILDINGS,b))continue;
        ARWorldEntity* build=w->buildings+b;if(!build->active)continue;
        build->cd=fmaxf(0,build->cd-1);
        if(build->kind!=AR_BUILD_HARVESTER || build->cd>0)continue;
        build->cd=e->cfg.harvest_period;
        for(int n=0;n<w->resource_count;n++) {
            if(ar_world_id_loaded(w->resource_id,AR_MAX_SHARDS,n))continue;
            ARWorldEntity* r=w->resources+n;double dx=build->x-r->x,dy=build->y-r->y;
            if(r->active && dx*dx+dy*dy<e->cfg.harvest_radius*e->cfg.harvest_radius)ar_world_yield(e,r);
        }
    }
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.dormant[p] && w->pets[p].active) {
        ARWorldPet* pet=w->pets+p;pet->work_cd=fmaxf(0,pet->work_cd-1);pet->cd=fmaxf(0,pet->cd-1);
        if((pet->x-w->home_x)*(pet->x-w->home_x)+(pet->y-w->home_y)*(pet->y-w->home_y)<e->cfg.home_radius*e->cfg.home_radius)
            pet->hp=fminf(e->cfg.pet_health[pet->kind],pet->hp+e->cfg.home_regen);
        if(pet->kind==AR_PET_EMBER && pet->command!=AR_CMD_WORK && pet->work_cd<=0 && e->shards>=2) {
            for(int b=0;b<w->building_count;b++) {
                ARWorldEntity* r=w->buildings+b;double dx=pet->x-r->x,dy=pet->y-r->y;
                if(r->active && r->kind==AR_BUILD_HARVESTER && dx*dx+dy*dy<9) {
                    e->shards-=2;e->cores+=1;pet->work_cd=8;break;
                }
            }
        }
        e->pets.hp[p]=pet->hp;e->pets.work_cd[p]=pet->work_cd;
    }
}

static inline void ar_world_step(ARPG* e) {
    ARWorld* w=(ARWorld*)e->campaign;if(!w)return;
    if(e->hp<=0) {
        e->hp=e->max_hp;e->shards=fmaxf(0,e->shards-5);e->px=e->home_x;e->py=e->home_y;
        e->invuln_timer=120;e->agents[0].terminals[0]=0;w->respawns++;
        ar_phys_teleport(e->player_body,e->px,e->py);
    }
    float stride=AR_CHUNK_SIZE*ar_world_cell(e);
    int dx=fabsf(e->px)>stride ? (int)floorf((e->px+stride*0.5f)/stride)*AR_CHUNK_SIZE : 0;
    int dy=fabsf(e->py)>stride ? (int)floorf((e->py+stride*0.5f)/stride)*AR_CHUNK_SIZE : 0;
    if(dx || dy)ar_world_shift(e,dx,dy);
    // A manually dispatched pet can leave without dragging the keeper's window
    // along with it. Transfer it to coarse navigation at the edge, retaining its
    // slot and world-space destination until it comes back into the live area.
    float edge=e->cfg.arena_size*0.5f;
    for(int p=0;p<AR_MAX_PETS;p++)if(e->pets.active[p] && !e->pets.dormant[p] && e->pets.command[p]!=AR_CMD_AUTO &&
            (fabsf(e->pets.goal_x[p])>edge-1 || fabsf(e->pets.goal_y[p])>edge-1) &&
            (fabsf(e->pets.x[p])>edge-1.1f || fabsf(e->pets.y[p])>edge-1.1f)) {
        ar_world_capture(e);e->pets.dormant[p]=1;e->pets.vx[p]=e->pets.vy[p]=0;
        if(!B3_IS_NULL(e->pet_body[p]))b3DestroyBody(e->pet_body[p]);e->pet_body[p]=b3_nullBodyId;
    }
    if(e->tick%60==0){ar_world_capture(e);ar_world_offscreen_tick(e);}
    ar_compute_observations(e,0);
}

// Versioned, checksummed save data. No pointers, physics handles or renderer
// state are serialized. New data is fsynced and atomically renamed into place.
typedef struct {
    char magic[8];
    uint32_t version,schema,seed,rng;
    int32_t origin_x,origin_y,counts[5],tick,camps,order,rally_active,respawns;
    int32_t task_override[AR_MAX_PETS];
    float arena_size,hp,shards,cores,harvested,summon_cd,dash_cd,nova_cd,frost_cd;
    double player_x,player_y,home_x,home_y,rally_x,rally_y;
    uint64_t checksum;
} ARWorldSaveHeader;

static inline uint64_t ar_world_hash(uint64_t hash,const void* data,size_t size) {
    const unsigned char* bytes=(const unsigned char*)data;
    for(size_t i=0;i<size;i++){hash^=bytes[i];hash*=UINT64_C(1099511628211);}return hash;
}
static inline uint64_t ar_world_checksum(ARWorldSaveHeader h,ARWorld* w) {
    h.checksum=0;uint64_t hash=ar_world_hash(UINT64_C(14695981039346656037),&h,sizeof(h));
    hash=ar_world_hash(hash,w->pets,sizeof(w->pets));
    hash=ar_world_hash(hash,w->chunks,(size_t)w->chunk_count*sizeof(*w->chunks));
    hash=ar_world_hash(hash,w->resources,(size_t)w->resource_count*sizeof(*w->resources));
    hash=ar_world_hash(hash,w->buildings,(size_t)w->building_count*sizeof(*w->buildings));
    hash=ar_world_hash(hash,w->nests,(size_t)w->nest_count*sizeof(*w->nests));
    return ar_world_hash(hash,w->enemies,(size_t)w->enemy_count*sizeof(*w->enemies));
}
static inline int ar_world_save(ARPG* e,const char* path) {
    ARWorld* w=(ARWorld*)e->campaign;if(!w || strlen(path)>4000)return 0;
    ar_world_capture(e);ARWorldSaveHeader h={0};memcpy(h.magic,"HWFRONT",8);h.version=1;h.schema=AR_OBS_VERSION;
    h.seed=w->seed;h.rng=e->rng;h.origin_x=w->origin_x;h.origin_y=w->origin_y;
    h.counts[0]=w->chunk_count;h.counts[1]=w->resource_count;h.counts[2]=w->building_count;
    h.counts[3]=w->nest_count;h.counts[4]=w->enemy_count;
    h.tick=e->tick;h.camps=e->camps_cleared;h.order=e->order;h.rally_active=e->rally_active;h.respawns=w->respawns;
    for(int p=0;p<AR_MAX_PETS;p++)h.task_override[p]=w->task_override[p];
    h.arena_size=e->cfg.arena_size;h.hp=e->hp;h.shards=e->shards;h.cores=e->cores;h.harvested=e->harvested;
    h.summon_cd=fmaxf(0,e->summon_cd);h.dash_cd=fmaxf(0,e->dash_cd);h.nova_cd=fmaxf(0,e->nova_cd);h.frost_cd=fmaxf(0,e->frost_cd);
    h.player_x=ar_world_ox(e,w)+e->px;h.player_y=ar_world_oy(e,w)+e->py;h.home_x=w->home_x;h.home_y=w->home_y;
    h.rally_x=ar_world_ox(e,w)+e->rally_x;h.rally_y=ar_world_oy(e,w)+e->rally_y;
    h.checksum=ar_world_checksum(h,w);
    char temp[4096];snprintf(temp,sizeof(temp),"%s.tmp.XXXXXX",path);
    int fd=mkstemp(temp);if(fd<0)return 0;
    FILE* file=fdopen(fd,"wb");if(!file){close(fd);unlink(temp);return 0;}
    int ok=fwrite(&h,sizeof(h),1,file)==1 && fwrite(w->pets,sizeof(w->pets),1,file)==1 &&
        fwrite(w->chunks,sizeof(*w->chunks),(size_t)w->chunk_count,file)==(size_t)w->chunk_count &&
        fwrite(w->resources,sizeof(*w->resources),(size_t)w->resource_count,file)==(size_t)w->resource_count &&
        fwrite(w->buildings,sizeof(*w->buildings),(size_t)w->building_count,file)==(size_t)w->building_count &&
        fwrite(w->nests,sizeof(*w->nests),(size_t)w->nest_count,file)==(size_t)w->nest_count &&
        fwrite(w->enemies,sizeof(*w->enemies),(size_t)w->enemy_count,file)==(size_t)w->enemy_count;
    if(fflush(file)!=0 || fsync(fd)!=0)ok=0;
    if(fclose(file)!=0)ok=0;
    if(ok && rename(temp,path)==0)return 1;
    unlink(temp);return 0;
}
static inline int ar_world_entity_valid(ARWorldEntity r,int kinds) {
    return isfinite(r.x)&&isfinite(r.y)&&fabs(r.x)<1e10&&fabs(r.y)<1e10&&
        isfinite(r.home_x)&&isfinite(r.home_y)&&isfinite(r.hp)&&isfinite(r.max_hp)&&
        isfinite(r.value)&&isfinite(r.cd)&&isfinite(r.aux)&&r.cd>=0&&
        r.kind>=0&&r.kind<kinds&&(r.active==0||r.active==1)&&(!r.active||r.hp>=0);
}
// Returns 1 loaded, 0 no save, -1 invalid/unreadable. Invalid saves are never
// overwritten, and failed loads leave the running simulation untouched.
static inline int ar_world_load(ARPG* e,const char* path) {
    FILE* file=fopen(path,"rb");if(!file)return errno==ENOENT ? 0 : -1;
    ARWorldSaveHeader h={0};int ok=fread(&h,sizeof(h),1,file)==1;
    if(!ok || memcmp(h.magic,"HWFRONT",8) || h.version!=1 || h.schema!=AR_OBS_VERSION ||
            !isfinite(h.arena_size) || fabsf(h.arena_size-e->cfg.arena_size)>0.0001f ||
            h.origin_x%AR_CHUNK_SIZE || h.origin_y%AR_CHUNK_SIZE ||
            h.origin_x<-100000000 || h.origin_x>100000000 || h.origin_y<-100000000 || h.origin_y>100000000) {fclose(file);return -1;}
    for(int i=0;i<5;i++)if(h.counts[i]<0 || h.counts[i]>1000000){fclose(file);return -1;}
    size_t expected=sizeof(h)+sizeof(ARWorldPet)*AR_MAX_PETS+(size_t)h.counts[0]*sizeof(ARWorldChunk);
    for(int i=1;i<5;i++)expected+=(size_t)h.counts[i]*sizeof(ARWorldEntity);
    if(expected>512*1024*1024 || fseek(file,0,SEEK_END) || ftell(file)!=(long)expected || fseek(file,sizeof(h),SEEK_SET)) {fclose(file);return -1;}
    const float values[]={h.hp,h.shards,h.cores,h.harvested,h.summon_cd,h.dash_cd,h.nova_cd,h.frost_cd};
    for(int i=0;i<8;i++)if(!isfinite(values[i]) || values[i]<0){fclose(file);return -1;}
    if(!isfinite(h.player_x)||!isfinite(h.player_y)||!isfinite(h.home_x)||!isfinite(h.home_y)||
            !isfinite(h.rally_x)||!isfinite(h.rally_y)||h.tick<0||h.camps<0||h.order<0||h.order>=AR_ORDER_COUNT||
            fabs(h.player_x-(double)h.origin_x*ar_world_cell(e))>e->cfg.arena_size ||
            fabs(h.player_y-(double)h.origin_y*ar_world_cell(e))>e->cfg.arena_size) {fclose(file);return -1;}
    ARWorld* w=(ARWorld*)calloc(1,sizeof(*w));if(!w){fclose(file);return -1;}
    w->seed=h.seed;w->origin_x=h.origin_x;w->origin_y=h.origin_y;w->home_x=h.home_x;w->home_y=h.home_y;w->respawns=h.respawns;
    for(int p=0;p<AR_MAX_PETS;p++)w->task_override[p]=h.task_override[p];
    w->chunk_count=w->chunk_capacity=h.counts[0];w->resource_count=w->resource_capacity=h.counts[1];
    w->building_count=w->building_capacity=h.counts[2];w->nest_count=w->nest_capacity=h.counts[3];w->enemy_count=w->enemy_capacity=h.counts[4];
    w->chunks=(ARWorldChunk*)calloc((size_t)h.counts[0],sizeof(*w->chunks));
    w->resources=(ARWorldEntity*)calloc((size_t)h.counts[1],sizeof(*w->resources));
    w->buildings=(ARWorldEntity*)calloc((size_t)h.counts[2],sizeof(*w->buildings));
    w->nests=(ARWorldEntity*)calloc((size_t)h.counts[3],sizeof(*w->nests));
    w->enemies=(ARWorldEntity*)calloc((size_t)h.counts[4],sizeof(*w->enemies));
    ok=(!h.counts[0]||w->chunks)&&(!h.counts[1]||w->resources)&&(!h.counts[2]||w->buildings)&&(!h.counts[3]||w->nests)&&(!h.counts[4]||w->enemies);
    if(ok)ok=fread(w->pets,sizeof(w->pets),1,file)==1 &&
        fread(w->chunks,sizeof(*w->chunks),(size_t)w->chunk_count,file)==(size_t)w->chunk_count &&
        fread(w->resources,sizeof(*w->resources),(size_t)w->resource_count,file)==(size_t)w->resource_count &&
        fread(w->buildings,sizeof(*w->buildings),(size_t)w->building_count,file)==(size_t)w->building_count &&
        fread(w->nests,sizeof(*w->nests),(size_t)w->nest_count,file)==(size_t)w->nest_count &&
        fread(w->enemies,sizeof(*w->enemies),(size_t)w->enemy_count,file)==(size_t)w->enemy_count;
    fclose(file);
    if(ok && h.checksum!=ar_world_checksum(h,w))ok=0;
    for(int i=0;ok && i<w->chunk_count;i++) {
        if(w->chunks[i].x<-6250002 || w->chunks[i].x>6250002 || w->chunks[i].y<-6250002 || w->chunks[i].y>6250002)ok=0;
        for(int t=0;t<AR_CHUNK_CELLS;t++)if(w->chunks[i].tiles[t]>=AR_TILE_COUNT)ok=0;
    }
    for(int i=0;ok && i<w->resource_count;i++)ok=ar_world_entity_valid(w->resources[i],1);
    for(int i=0;ok && i<w->building_count;i++)ok=ar_world_entity_valid(w->buildings[i],AR_BUILD_KIND_COUNT);
    for(int i=0;ok && i<w->nest_count;i++)ok=ar_world_entity_valid(w->nests[i],1);
    for(int i=0;ok && i<w->enemy_count;i++)ok=ar_world_entity_valid(w->enemies[i],AR_ENEMY_KIND_COUNT);
    for(int p=0;ok && p<AR_MAX_PETS;p++) {
        ARWorldPet r=w->pets[p];
        ok=h.task_override[p]>=-1&&h.task_override[p]<AR_PET_TASK_COUNT&&(r.active==0||r.active==1)&&r.kind>=0&&r.kind<AR_PET_CLASS_COUNT&&r.command>=0&&r.command<=AR_CMD_WORK&&r.task>=0&&r.task<AR_PET_TASK_COUNT&&
            isfinite(r.x)&&isfinite(r.y)&&isfinite(r.goal_x)&&isfinite(r.goal_y)&&isfinite(r.hp)&&isfinite(r.cd)&&isfinite(r.work_cd);
    }
    if(!ok) {
        free(w->chunks);free(w->resources);free(w->buildings);free(w->nests);free(w->enemies);free(w);return -1;
    }
    ar_world_close(e);e->campaign=w;ar_world_ids_clear(w);e->dungeon_seed=h.seed;e->rng=h.rng;e->tick=h.tick;
    e->hp=h.hp;e->shards=h.shards;e->cores=h.cores;e->harvested=h.harvested;e->camps_cleared=h.camps;e->order=h.order;
    e->summon_cd=h.summon_cd;e->dash_cd=h.dash_cd;e->nova_cd=h.nova_cd;e->frost_cd=h.frost_cd;
    e->px=(float)(h.player_x-ar_world_ox(e,w));e->py=(float)(h.player_y-ar_world_oy(e,w));
    e->rally_active=h.rally_active;e->rally_x=(float)(h.rally_x-ar_world_ox(e,w));e->rally_y=(float)(h.rally_y-ar_world_oy(e,w));
    e->fx_blast=e->fx_nova=e->fx_frost=e->fx_dash=0;
    ar_world_activate(e);return 1;
}
#endif
