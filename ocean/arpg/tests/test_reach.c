#define AR_HEADLESS_BINDING
#include "../arpg.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    Env e;
    float obs[AR_OBS_SIZE],actions[NUM_ATNS],reward,terminal;
} Fixture;

static void setup(Fixture* f,Dict* cfg) {
    memset(f,0,sizeof(*f));puf_init(&f->e,cfg);f->e.rng=42;
    f->e.cfg.enemy_cap=0;f->e.cfg.max_steps=90000;
    f->e.agents[0].observations=f->obs;f->e.agents[0].actions=f->actions;
    f->e.agents[0].rewards=&f->reward;f->e.agents[0].terminals=&f->terminal;
    c_reset(&f->e);
}

static void test_direct_control(Dict* cfg) {
    Fixture f;setup(&f,cfg);Env* e=&f.e;
    memset(e->dungeon,AR_TILE_GRASS,sizeof(e->dungeon));ar_world_begin(e);
    ARWorld* w=e->campaign;
    // A controlled, clear corridor isolates streaming/control from combat and
    // random river placement; the actual physics and game ticks are unchanged.
    for(int cy=-1;cy<=1;cy++)for(int cx=-2;cx<=13;cx++)
        memset(ar_world_chunk(e,w,cx,cy,1)->tiles,AR_TILE_GRASS,AR_CHUNK_CELLS);
    ar_world_activate(e);
    ar_command_pet(e,0,1,AR_CMD_GATHER,e->shard_x[0],e->shard_y[0]);
    e->pets.x[1]=e->shard_x[0]+.7f;e->pets.y[1]=e->shard_y[0];
    ar_phys_teleport(e->pet_body[1],e->pets.x[1],e->pets.y[1]);
    double keeper_x=e->px,keeper_y=e->py,worker_x=e->pets.x[1],worker_y=e->pets.y[1];
    float produced=e->harvested;
    assert(!ar_world_drive(e,7) && !ar_world_drive(e,-2));
    assert(ar_world_drive(e,0));e->direct_dx=1;e->direct_dy=0;
    for(int i=0;i<2300;i++)c_step(e);
    double scout_x=w->origin_x*ar_world_cell(e)+e->pets.x[0];
    double scout_y=w->origin_y*ar_world_cell(e)+e->pets.y[0];
    assert(scout_x>100 && w->origin_x>=96 && e->direct_pet==0);
    assert(e->keeper_dormant && B3_IS_NULL(e->player_body));
    assert(ar_summon_pet(e,0,AR_PET_FANG)==-1);
    assert(fabs(w->origin_x*ar_world_cell(e)+e->px-keeper_x)<.001);
    assert(fabs(w->origin_y*ar_world_cell(e)+e->py-keeper_y)<.001);
    assert(e->pets.dormant[1] && e->pets.command[1]==AR_CMD_GATHER && e->harvested>produced);
    assert(hypot(w->pets[1].x-worker_x,w->pets[1].y-worker_y)<2);
    assert(fabs(w->pets[1].goal_x-(worker_x-.7))<.01 && fabs(w->pets[1].goal_y-worker_y)<.01);
    // Stopping input stops the selected pet immediately, not a keeper/mass order.
    e->direct_dx=0;c_step(e);assert(e->pets.vx[0]==0 && e->pets.vy[0]==0);
    // Saving while the keeper is unloaded must preserve both actors and reopen
    // around the keeper, without rebasing the live world merely to save it.
    char path[]="/tmp/arpg-reach-save.XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    int origin=w->origin_x;assert(ar_world_save(e,path));assert(w->origin_x==origin);
    assert(ar_world_load(e,path)==1);w=e->campaign;
    assert(w->terrain_version==2 && e->terrain_version==2 && e->direct_pet==-1);
    assert(!e->keeper_dormant && !B3_IS_NULL(e->player_body) && w->origin_x==0);
    assert(fabs(w->pets[0].x-scout_x)<.01 && fabs(w->pets[0].y-scout_y)<.01);
    assert(e->pets.dormant[0]);
    assert(ar_world_drive(e,0) && !e->pets.dormant[0] && e->keeper_dormant);
    assert(ar_world_drive(e,-1) && !e->keeper_dormant && e->pets.dormant[0]);
    assert(fabs(e->px-keeper_x)<.001 && fabs(e->py-keeper_y)<.001);
    // A driven unit can die or be released without an invalid body or focus.
    assert(ar_world_drive(e,0));ar_free_pet(e,0,0);c_step(e);
    assert(e->direct_pet==-1 && !e->keeper_dormant && w->origin_x==0);
    unlink(path);puf_close(e);
}

static void test_index_and_legacy(Dict* cfg) {
    Fixture f;setup(&f,cfg);Env* e=&f.e;e->terrain_version=1;
    for(int y=0;y<AR_DUN_H;y++)for(int x=0;x<AR_DUN_W;x++)
        e->dungeon[y*AR_DUN_W+x]=ar_terrain_tile_v1(e->dungeon_seed,x-AR_DUN_W/2,y-AR_DUN_H/2);
    ar_world_begin(e);ARWorld* w=e->campaign;
    for(int i=0;i<450;i++) {
        int cx=i-225,cy=i%17-8;
        ARWorldChunk* chunk=ar_world_chunk(e,w,cx,cy,1);assert(chunk);
        int id=ar_world_chunk_id(w,cx,cy);assert(id>=0 && w->chunks[id].x==cx && w->chunks[id].y==cy);
    }
    for(int i=0;i<450;i++)assert(ar_world_chunk_id(w,i-225,i%17-8)>=0);
    assert(ar_world_chunk_id(w,999999,-999999)==-1);
    int chunks=w->chunk_count;
    for(int y=-800;y<800;y+=27)for(int x=-900;x<900;x+=29)
        assert(ar_world_global_tile(e,w,x,y)==ar_terrain_tile_v1(w->seed,x,y));
    assert(w->chunk_count==chunks); // Previewing uncharted terrain is read-only.
    char path[]="/tmp/arpg-reach-legacy.XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    assert(ar_world_save(e,path) && ar_world_load(e,path)==1);w=e->campaign;
    assert(e->terrain_version==1 && w->terrain_version==1 && w->chunk_count==chunks);
    for(int i=0;i<450;i++)assert(ar_world_chunk_id(w,i-225,i%17-8)>=0);
    assert(ar_world_global_tile(e,w,8000,-7000)==ar_terrain_tile_v1(w->seed,8000,-7000));
    unlink(path);puf_close(e);
}

static void test_direct_work(Dict* cfg) {
    Fixture f;setup(&f,cfg);Env* e=&f.e;
    memset(e->dungeon,AR_TILE_GRASS,sizeof(e->dungeon));
    int digger=ar_summon_pet(e,0,AR_PET_BURROWER);assert(digger>=0);
    e->pets.x[digger]=.5f;e->pets.y[digger]=.5f;ar_phys_teleport(e->pet_body[digger],.5f,.5f);
    e->dungeon[32*AR_DUN_W+34]=AR_TILE_ROCK;
    assert(ar_world_drive(e,digger));e->direct_aim_x=1;e->direct_aim_y=0;e->direct_work=1;
    c_step(e);assert(e->dungeon[32*AR_DUN_W+34]!=AR_TILE_ROCK && e->pets.work_cd[digger]>0);
    ar_world_drive(e,-1);ar_free_pet(e,0,digger);
    e->shards=200;e->harvested=100;
    assert(ar_build_at(e,0,AR_BUILD_HARVESTER,5.5f,0.5f)>=0);
    int ember=ar_summon_pet(e,0,AR_PET_EMBER);assert(ember>=0);
    e->pets.x[ember]=3.5f;e->pets.y[ember]=.5f;ar_phys_teleport(e->pet_body[ember],3.5f,.5f);
    assert(ar_world_drive(e,ember));float cores=e->cores;
    c_step(e);assert(e->cores==cores+1 && e->pets.work_cd[ember]>0);
    puf_close(e);
}

static void test_regions(void) {
    int counts[AR_BIOME_COUNT]={0},tiles[AR_TILE_COUNT]={0};
    for(int seed=1;seed<=4;seed++)for(int y=-768;y<=768;y+=12)for(int x=-768;x<=768;x+=12) {
        int biome=ar_biome((uint32_t)seed,x,y),tile=ar_terrain_tile((uint32_t)seed,x,y);
        assert(biome>=0 && biome<AR_BIOME_COUNT && tile>=0 && tile<AR_TILE_COUNT);
        counts[biome]++;tiles[tile]++;
        assert(tile==ar_terrain_tile((uint32_t)seed,x,y));
    }
    for(int b=0;b<AR_BIOME_COUNT;b++)assert(counts[b]>200);
    assert(tiles[AR_TILE_ROCK]>0 && tiles[AR_TILE_FOREST]>0 && tiles[AR_TILE_DEEP]>0 && tiles[AR_TILE_SHALLOW]>0);
    for(int action=1;action<AR_MOVE_ACTION_COUNT;action++) {
        float x,y;ar_move_dir(action,&x,&y);assert(fabsf(x*x+y*y-1)<.00001f);
        float sx=x-y,sy=(x+y)*.5f;
        if(action>=5)assert(fabsf(fabsf(sx)-fabsf(sy))<.00001f);
    }
}

int main(void) {
    Ini ini={0};puf_ini_load_env(&ini,"arpg",0,NULL);Dict* cfg=puf_ini_section(&ini,"env",0);
    test_direct_control(cfg);test_direct_work(cfg);test_index_and_legacy(cfg);test_regions();puf_ini_free(&ini);
    puts("ARPG Reach: isolated direct control, long-distance pet streaming, remote workers, possession saves, legacy terrain, chunk index, six biomes, movement vectors: PASS");
}
