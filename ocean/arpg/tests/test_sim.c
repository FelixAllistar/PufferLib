#define AR_HEADLESS_BINDING
#include "../arpg.h"
#include <assert.h>
#include <stdio.h>

static void init_env(Env* e, Dict* cfg, uint32_t seed, float* obs, float* actions, float* reward, float* terminal) {
    memset(e,0,sizeof(*e));
    puf_init(e,cfg);e->rng=seed;
    e->agents[0].observations=obs;e->agents[0].actions=actions;
    e->agents[0].rewards=reward;e->agents[0].terminals=terminal;
    puf_reset(e);
}

int main(void) {
    Ini ini={0};puf_ini_load_env(&ini,"arpg",0,NULL);
    Dict* cfg=puf_ini_section(&ini,"env",0);
    float obs[AR_OBS_SIZE],actions[NUM_ATNS]={0},reward=0,terminal=0;
    Env e;
    for(uint32_t seed=1;seed<=12;seed++) {
        init_env(&e,cfg,seed,obs,actions,&reward,&terminal);
        assert(e.pets_alive==2 && e.enemy_count==0 && e.shards==e.cfg.start_shards);
        assert(e.pets.kind[0]==AR_PET_WISP && e.pets.kind[1]==AR_PET_MULE);
        for(int n=0;n<AR_MAX_NESTS;n++)if(e.nest_active[n])
            assert(ar_geometry_dist2(e.home_x,e.home_y,e.nest_x[n],e.nest_y[n])
                >= (e.cfg.home_radius+11)*(e.cfg.home_radius+11));
        for(int t=0;t<3600;t++) {
            c_step(&e);
            assert(e.hp==e.max_hp && e.enemy_count==0 && !terminal);
            for(int o=0;o<AR_OBS_SIZE;o++)assert(isfinite(obs[o]));
            for(int p=0;p<AR_MAX_PETS;p++)if(e.pets.active[p])
                assert(isfinite(e.pets.x[p]) && isfinite(e.pets.y[p]));
        }
        assert(e.camps_cleared==0 && e.nests_alive==e.cfg.nest_count);
        assert(e.harvested>=4 && e.shards>e.cfg.start_shards);
        puf_close(&e);
    }
    init_env(&e,cfg,42,obs,actions,&reward,&terminal);
    // Model task heads independently select all six jobs.
    for(int task=0;task<AR_PET_TASK_COUNT;task++) {
        actions[5]=task;actions[6]=AR_TASK_HOME;c_step(&e);
        assert(e.pets.task[0]==task && e.pets.task[1]==AR_TASK_HOME);
        assert(obs[AR_OBS_TASK_BASE]==(float)task/(AR_PET_TASK_COUNT-1));
    }
    memset(actions,0,sizeof(actions));
    // Place an extractor near the first deposit; overlapping structures are rejected.
    float x=e.shard_x[0]+1.5f,y=e.shard_y[0];
    int building=ar_build_at(&e,0,AR_BUILD_HARVESTER,x,y);
    assert(building>=0);
    float currency=e.shards;
    assert(ar_build_at(&e,0,AR_BUILD_HARVESTER,x,y)<0 && e.shards==currency);
    float produced=e.harvested;
    for(int t=0;t<1200;t++)c_step(&e);
    assert(e.harvested>produced);
    // Wake a camp: no ambient waves; the camp's defender budget remains bounded.
    e.rally_active=0;
    int camp=0;while(camp<AR_MAX_NESTS && !e.nest_active[camp])camp++;
    assert(camp<AR_MAX_NESTS);
    e.px=e.nest_x[camp]+5;e.py=e.nest_y[camp];
    e.nest_cd[camp]=0;
    ar_nest_spawning(&e,0);assert(e.enemy_count==1);
    for(int t=0;t<10;t++){e.nest_cd[camp]=0;ar_nest_spawning(&e,0);}
    assert(e.enemy_count<=3);
    int foe=e.enemies.dense[0];
    e.px=e.home_x;e.py=e.home_y;
    ar_steer_enemies(&e,0);
    assert(fabsf(e.enemies.vx[foe])<0.001f && fabsf(e.enemies.vy[foe])<0.001f);
    int level=ar_tech_level(&e,0);
    ar_damage_nest(&e,0,camp,e.cfg.nest_hp*4);
    assert(e.camps_cleared==1 && ar_tech_level(&e,0)==level+1);
    assert(e.episode_damage_dealt==e.cfg.nest_hp); // no overkill reward
    // Adapter auto-reset must retain terminal outputs and clear old state.
    e.cfg.max_steps=e.tick+1;puf_step(&e);
    assert(terminal==1 && e.tick==0 && e.enemy_count==0 && e.pets_alive==2);
    assert(e.log.harvested>0 && e.log.buildings==1 && e.log.camps_cleared==1);
    float components=e.log.reward_survival+e.log.reward_kill+e.log.reward_damage+
        e.log.reward_hurt+e.log.reward_summon+e.log.reward_economy+e.log.reward_terminal;
    assert(fabsf(e.log.episode_return-components)<0.001f);
    assert(e.camps_cleared==0 && reward>=e.cfg.reward_success);
    puf_close(&e);
    // Outposts provoke camps too, and collision separation must not prevent damage.
    init_env(&e,cfg,42,obs,actions,&reward,&terminal);
    int outpost=-1;
    for(int attempt=0;attempt<32 && outpost<0;attempt++) {
        ar_floor_near(&e,0,e.nest_x[0],e.nest_y[0],3,e.cfg.build_radius[0],&x,&y);
        outpost=ar_build_at(&e,0,AR_BUILD_TOTEM,x,y);
    }
    assert(outpost>=0);
    e.nest_cd[0]=0;ar_nest_spawning(&e,0);assert(e.enemy_count>=1);
    foe=e.enemies.dense[0];
    e.enemies.x[foe]=x+e.build_rad[outpost]+e.enemies.radius[foe];e.enemies.y[foe]=y;
    ar_steer_enemies(&e,0);assert(e.enemies.vx[foe]<0);
    ar_building_contact(&e,0);assert(e.build_hp[outpost]<e.build_max_hp[outpost]);
    puf_close(&e);
    // Long mixed-action rollouts exercise construction, abilities, combat, and resets.
    int sizes[]=ACT_SIZES;uint32_t rng=17;
    for(int seed=90;seed<94;seed++) {
        memset(actions,0,sizeof(actions));
        init_env(&e,cfg,seed,obs,actions,&reward,&terminal);
        for(int t=0;t<12000;t++) {
            if(t%30==0)for(int h=0;h<NUM_ATNS;h++) {
                rng=rng*1664525u+1013904223u;actions[h]=(float)((rng>>8)%sizes[h]);
            }
            puf_step(&e);
            for(int o=0;o<AR_OBS_SIZE;o++)assert(isfinite(obs[o]));
            assert(isfinite(reward) && e.shards>=0);
            int pets=0,enemies=0,builds=0;
            for(int p=0;p<AR_MAX_PETS;p++)pets+=e.pets.active[p];
            for(int i=0;i<e.cfg.enemy_cap;i++)enemies+=e.enemies.active[i];
            for(int b=0;b<AR_MAX_BUILDINGS;b++)builds+=e.build_active[b];
            assert(pets==e.pets_alive && enemies==e.enemy_count && builds==e.builds_alive);
            for(int k=0;k<e.enemy_count;k++) {
                int i=e.enemies.dense[k];
                assert(e.enemies.active[i] && e.enemies.dense_pos[i]==k);
            }
        }
        puf_close(&e);
    }
    puf_ini_free(&ini);
    puts("ARPG homestead / pet tasks / production / territorial camps / reset: PASS");
}
