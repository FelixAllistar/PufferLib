#define AR_HEADLESS_BINDING
#include "../arpg.h"
#include <assert.h>
#include <stdio.h>

static void setup(Env* e,Dict* config,float* obs,float* actions,float* reward,float* terminal,uint32_t seed) {
    memset(e,0,sizeof(*e));puf_init(e,config);e->rng=seed;
    e->agents[0].observations=obs;e->agents[0].actions=actions;
    e->agents[0].rewards=reward;e->agents[0].terminals=terminal;c_reset(e);
}

int main(void) {
    Ini ini={0};puf_ini_load_env(&ini,"arpg",0,NULL);Dict* cfg=puf_ini_section(&ini,"env",0);
    float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;Env e;
    setup(&e,cfg,obs,actions,&reward,&terminal,42);
    // Rock and deep water block movers; a negative out-of-bounds sample is solid.
    uint8_t tiles[AR_DUN_CELLS];memset(tiles,AR_TILE_GRASS,sizeof(tiles));
    tiles[32*AR_DUN_W+32]=AR_TILE_DEEP;float x=0.5f,y=0.5f;
    assert(ar_geometry_collide_dungeon(tiles,64,&x,&y,0.35f));
    assert(!ar_geometry_floor(tiles,64,-32.1f,0));
    // Pathing routes around a long wall, not into a local steering minimum.
    memset(tiles,AR_TILE_GRASS,sizeof(tiles));
    for(int yy=8;yy<54;yy++)tiles[yy*AR_DUN_W+32]=AR_TILE_ROCK;
    x=-5.5f;y=0.5f;float nx,ny;
    for(int i=0;i<150 && x<4;i++) {
        assert(ar_nav_next(tiles,64,x,y,5.5f,0.5f,&nx,&ny));
        assert(ar_geometry_floor(tiles,64,nx,ny));x=nx;y=ny;
    }
    assert(x>4);
    // Digging is a sustained tunnel order through a ridge, not a cosmetic hit.
    memcpy(e.dungeon,tiles,sizeof(tiles));
    int burrower=ar_summon_pet(&e,0,AR_PET_BURROWER);assert(burrower>=0);
    memset(e.dungeon,AR_TILE_GRASS,sizeof(e.dungeon));
    for(int yy=29;yy<36;yy++)for(int xx=37;xx<45;xx++)e.dungeon[yy*AR_DUN_W+xx]=AR_TILE_ROCK;
    ar_command_pet(&e,0,burrower,AR_CMD_WORK,12.5f,0.5f);
    for(int t=0;t<1800;t++)c_step(&e);
    for(int xx=37;xx<45;xx++)assert(e.dungeon[32*AR_DUN_W+xx]!=AR_TILE_ROCK);
    // Automatic terrain work must not turn itself into a persistent manual order.
    e.pets.x[burrower]=2.5f;e.pets.y[burrower]=0.5f;
    ar_phys_teleport(e.pet_body[burrower],2.5f,0.5f);
    e.dungeon[32*AR_DUN_W+35]=AR_TILE_ROCK;
    ar_command_pet(&e,0,burrower,AR_CMD_AUTO,2.5f,0.5f);
    e.pets.task[burrower]=AR_TASK_WORK;ar_steer_pets(&e,0);
    assert(e.pets.command[burrower]==AR_CMD_AUTO);
    e.pets.task[burrower]=AR_TASK_ESCORT;ar_steer_pets(&e,0);
    assert(e.pets.command[burrower]==AR_CMD_AUTO);
    ar_free_pet(&e,0,burrower);
    // An explicit order takes precedence over an older high-level Home task.
    e.pets.task[0]=AR_TASK_HOME;ar_command_pet(&e,0,0,AR_CMD_MOVE,-4,0.5f);
    ar_steer_pets(&e,0);assert(e.pets.vx[0]<0);
    puf_close(&e);setup(&e,cfg,obs,actions,&reward,&terminal,42);
    // Two workers retain distinct destinations despite changing global orders.
    int porter=ar_summon_pet(&e,0,AR_PET_MULE);assert(porter>=0);
    ar_command_pet(&e,0,1,AR_CMD_GATHER,e.shard_x[0],e.shard_y[0]);
    ar_command_pet(&e,0,porter,AR_CMD_GATHER,e.shard_x[1],e.shard_y[1]);
    float goal1=e.pets.goal_x[1],goal2=e.pets.goal_x[porter];
    float produced=e.harvested;
    for(int t=0;t<1500;t++){actions[2]=(t/120)%4;c_step(&e);}
    assert(e.pets.command[1]==AR_CMD_GATHER && e.pets.command[porter]==AR_CMD_GATHER);
    assert(e.pets.goal_x[1]==goal1 && e.pets.goal_x[porter]==goal2 && e.harvested>produced);
    // Kill a genuine camp using only orders and regular simulation ticks.
    actions[2]=0;int fang=ar_summon_pet(&e,0,AR_PET_FANG);assert(fang>=0);
    int camp=0;while(camp<AR_MAX_NESTS && !e.nest_active[camp])camp++;
    assert(camp<AR_MAX_NESTS);
    ar_command_pet(&e,0,0,AR_CMD_ATTACK,e.nest_x[camp],e.nest_y[camp]);
    ar_command_pet(&e,0,fang,AR_CMD_ATTACK,e.nest_x[camp],e.nest_y[camp]);
    for(int t=0;t<3600 && e.nest_active[camp];t++)c_step(&e);
    if(e.nest_active[camp])fprintf(stderr,"Camp failure: HP %.1f, wisp %.1f %.1f / %.1f HP, fang %.1f %.1f / %.1f HP, camp %.1f %.1f\n",e.nest_hp[camp],e.pets.x[0],e.pets.y[0],e.pets.hp[0],e.pets.x[fang],e.pets.y[fang],e.pets.hp[fang],e.nest_x[camp],e.nest_y[camp]);
    assert(!e.nest_active[camp] && e.camps_cleared>=1);
    puf_close(&e);
    setup(&e,cfg,obs,actions,&reward,&terminal,42);
    // Terraforming changes the actual map. A bridge makes deep water walkable.
    int tile=35*AR_DUN_W+35;e.dungeon[tile]=AR_TILE_ROCK;
    assert(ar_terraform(&e,0,3.5f,3.5f,0.6f,0)==1 && e.dungeon[tile]!=AR_TILE_ROCK);
    e.dungeon[tile]=AR_TILE_DEEP;
    assert(ar_build_at(&e,0,AR_BUILD_BRIDGE,3.5f,3.5f)>=0);
    assert(e.dungeon[tile]==AR_TILE_BRIDGE && ar_geometry_floor(e.dungeon,64,3.5f,3.5f));
    // The refinery consumes aether, yields cores and enables a real terrain blast.
    e.shards=200;e.harvested=100;
    int extractor=ar_build_at(&e,0,AR_BUILD_HARVESTER,-5.5f,0.5f);assert(extractor>=0);
    int ember=ar_summon_pet(&e,0,AR_PET_EMBER);assert(ember>=0);
    e.pets.x[ember]=-4;e.pets.y[ember]=0.5f;ar_phys_teleport(e.pet_body[ember],-4,0.5f);
    ar_command_pet(&e,0,ember,AR_CMD_HOLD,-4,0.5f);
    for(int t=0;t<3900;t++)c_step(&e);
    assert(e.cores>=8);
    int launcher=ar_build_at(&e,0,AR_BUILD_ARTILLERY,-5.5f,-4);assert(launcher>=0);
    float before=e.cores;camp=0;while(!e.nest_active[camp])camp++;
    assert(ar_fire_artillery(&e,0,e.nest_x[camp],e.nest_y[camp]));
    assert(!e.nest_active[camp] && e.cores==before-8 && e.build_cd[launcher]>0 && e.fx_blast>0);
    assert(!ar_fire_artillery(&e,0,0,0));
    // Persist edits, buildings, camp destruction, orders and offscreen production.
    ar_world_begin(&e);ARWorld* w=(ARWorld*)e.campaign;
    // Unloaded strikes must neither spend ammo nor partially alter the frontier.
    e.cores=8;e.build_cd[launcher]=0;
    float bank=e.shards;
    assert(!ar_fire_artillery(&e,0,31,0));
    assert(e.cores==8 && e.shards==bank && e.build_cd[launcher]==0);
    int initial_chunks=w->chunk_count,initial_nests=w->nest_count,cleared=e.camps_cleared;
    ar_command_pet(&e,0,1,AR_CMD_GATHER,e.shard_x[0],e.shard_y[0]);
    e.pets.x[1]=e.shard_x[0]+0.7f;e.pets.y[1]=e.shard_y[0];
    ar_phys_teleport(e.pet_body[1],e.pets.x[1],e.pets.y[1]);
    double porter_x=e.pets.x[1],porter_y=e.pets.y[1];
    uint8_t saved_tiles[AR_DUN_CELLS];memcpy(saved_tiles,e.dungeon,sizeof(saved_tiles));
    for(int step=0;step<8;step++){e.px+=16;ar_world_shift(&e,16,0);}
    assert(w->origin_x==128 && w->chunk_count>initial_chunks && w->nest_count>initial_nests);
    assert(e.pets.active[1] && e.pets.dormant[1]);
    assert(fabs(w->pets[1].x-porter_x)<0.001 && fabs(w->pets[1].y-porter_y)<0.001);
    produced=e.harvested;float cores=e.cores;
    for(int t=0;t<1200;t++)c_step(&e);
    assert(e.harvested>produced && e.cores>cores);
    assert(e.pets.dormant[1] && fabs(w->pets[1].x-porter_x)<0.001);
    char save[]="/tmp/arpg-frontier-save.XXXXXX";int fd=mkstemp(save);assert(fd>=0);close(fd);
    assert(ar_world_save(&e,save));float saved_aether=e.shards,saved_cores=e.cores;
    int chunks=w->chunk_count,saved_origin_x=w->origin_x,saved_origin_y=w->origin_y;
    assert(ar_world_load(&e,save)==1);w=(ARWorld*)e.campaign;
    assert(w->origin_x==saved_origin_x && w->origin_y==saved_origin_y && w->chunk_count==chunks && e.shards==saved_aether && e.cores==saved_cores);
    e.px=0.5f-w->origin_x;e.py=0.5f-w->origin_y;ar_world_shift(&e,-w->origin_x,-w->origin_y);w=(ARWorld*)e.campaign;
    assert(!e.pets.dormant[1] && fabs(e.pets.x[1]-porter_x)<0.001);
    assert(!memcmp(saved_tiles,e.dungeon,sizeof(saved_tiles)) && e.camps_cleared==cleared);
    assert(e.builds_alive==3);
    // Dispatch and recall a unit beyond the active window without moving the keeper.
    int origin=w->origin_x;
    ar_command_pet(&e,0,1,AR_CMD_MOVE,-42.5f,0.5f);
    for(int t=0;t<2400;t++)c_step(&e);
    assert(w->origin_x==origin && e.pets.active[1] && e.pets.dormant[1]);
    ar_world_capture(&e);
    assert(w->pets[1].x<-35);
    ar_command_pet(&e,0,1,AR_CMD_MOVE,0.5f,0.5f);
    for(int t=0;t<2400;t++)c_step(&e);
    assert(!e.pets.dormant[1] && ar_geometry_dist2(e.pets.x[1],e.pets.y[1],0.5f,0.5f)<9);
    saved_aether=e.shards;
    // Reject corruption without touching the current running world.
    FILE* file=fopen(save,"r+b");assert(file);assert(fseek(file,40,SEEK_SET)==0);fputc(255,file);fclose(file);
    assert(ar_world_load(&e,save)==-1 && e.campaign==w && e.shards==saved_aether);
    unlink(save);puf_close(&e);puf_ini_free(&ini);
    puts("ARPG frontier: pathing, separate orders, combat clears, digging, bridges, refinery, Starfire, streaming, remote industry, safe saves: PASS");
}
