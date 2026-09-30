#include <assert.h>
#include <stdio.h>
#include "../abyss.h"
static void near(float a,float b){assert(fabsf(a-b)<.001f);}
int main(int argc,char**argv){
    Ini ini={0};puf_ini_load_file(&ini,"config/default.ini");puf_ini_load_file(&ini,"config/abyss.ini");puf_ini_put(&ini,"env.filament_tier","1");
    Env e={0};float obs[OBS_SIZE],actions[NUM_ATNS]={0},rewards[1],terminals[1];unsigned char mask[ABYSS_ACTION_MASK_SIZE];
    e.agents[0].observations=obs;e.agents[0].actions=actions;e.agents[0].rewards=rewards;e.agents[0].terminals=terminals;
    puf_init(&e,puf_ini_section(&ini,"env",0));e.agents[0].action_mask=mask;e.rng=17;
    if(argc>1){
        FILE*f=fopen(argv[1],"w");assert(f);fputs("[",f);int first=1;
        for(int type=0;type<GENERATED_NPC_COUNT;type++)for(int scenario=0;scenario<4;scenario++){
            e.entity_count=0;e.ship_pos=e.ship_vel=(Vec3){0};e.distance_observation_lag_ticks=0;
            float distance=scenario*15000.f;
            ab_add_generated_hostile(&e,(GeneratedSpawn){type,{distance,0,0}});
            ab_add_generated_hostile(&e,(GeneratedSpawn){(type+1)%GENERATED_NPC_COUNT,{distance/2,0,0}});
            e.unknown_npc_index=scenario==3?0:-1;float out[10];ab_estimate_ewar(&e,out);
            fprintf(f,"%s{\"type\":%d,\"other\":%d,\"distance\":%.0f,\"unknown\":%s,\"expected\":[",first?"":",",type,(type+1)%GENERATED_NPC_COUNT,distance,scenario==3?"true":"false");
            for(int c=0;c<10;c++)fprintf(f,"%s%.9g",c?",":"",out[c]);
            fputs("]}",f);first=0;
        }
        fputs("]\n",f);fclose(f);puf_ini_free(&ini);return 0;
    }
    assert(OBS_SIZE==1234);near(ab_effect_range_factor(15000,10000,5000),.5f);near(ab_effect_range_factor(10001,10000,0),0);
    float stack[]={-.2f,-.5f};near(ab_stack_effects(stack,2),.5f*(1-.2f*expf(-.140274f)));
    // Every sampled family, many compositions and both weather rolls. Verify
    // parent counts, capacity, finite observations and stable auxiliary slots.
    for(unsigned a=0;a<CALM_ARCHETYPE_COUNT;a++)for(int seed=0;seed<100;seed++){
        e.encounter_archetype=a;puf_reset(&e);int parents=0;
        for(int i=0;i<e.entity_count;i++)if(e.entities[i].kind==ENTITY_HOSTILE&&e.entities[i].parent_index<0)parents++;
        assert(parents>=CALM_ARCHETYPES[a].min&&parents<=CALM_ARCHETYPES[a].max);assert(e.entity_count<=64);
        ab_step_npc_drones(&e);compute_observations(&e);
        for(int i=0;i<OBS_SIZE;i++)assert(isfinite(obs[i]));
        for(int j=0;j<10;j++)puf_step(&e);
    }
    // Runtime tier selection in the same compiled environment. T0 retains its
    // empirical encounter templates but uses exactly the same combat mechanics.
    e.filament_tier=0;
    for(int scenario=0;scenario<GENERATED_EPISODE_COUNT;scenario++){
        e.configured_scenario_episode=scenario;puf_reset(&e);
        int count=0;for(int i=0;i<e.entity_count;i++)count+=e.entities[i].kind==ENTITY_HOSTILE;
        assert(count==GENERATED_ROOMS[scenario*3].hostile_count);
        assert(OBS_SIZE==1234);assert(e.unknown_npc_index==-1);
    }
    e.filament_tier=1;e.configured_scenario_episode=-1;
    // Isolated NPC for each EWAR family; estimator remains an estimate while
    // actual activation and strength can differ.
    e.encounter_archetype=0;e.ewar_sensor_dropout_probability=0;puf_reset(&e);e.entity_count=1;e.ship_pos=(Vec3){0};
    for(int type=0;type<GENERATED_NPC_COUNT;type++){
        e.entity_count=0;ab_add_generated_hostile(&e,(GeneratedSpawn){type,{0,0,0}});
        ab_assign_policy_slots(&e);compute_observations(&e);
        const GeneratedNpcDef*def=&GENERATED_NPCS[type];float maxhp[3]={def->shield,def->armor,def->hull};
        for(int layer=0;layer<3;layer++)near(obs[32+13+layer],maxhp[layer]>0?AB_NPC_MECHANICS[type].initial[layer]/maxhp[layer]:0);
        e.entities[0].locked=1;e.entities[0].shield=.2f*def->shield;
        compute_observations(&e);near(obs[32+13],def->shield>0?.2f:0);
        e.entities[0].locked=0;
        AbyssEntity*n=&e.entities[0];n->ewar_strength=1;e.ewar_activation_probability=1;e.capacitor=611;
        float expected=0;
        for(int k=0;k<AB_NPC_MECHANICS[type].effect_count;k++)expected+=AB_NPC_MECHANICS[type].effects[k].values[8]*(1-e.cap_neut_resistance);
        memset(n->ewar_cd,0,sizeof(n->ewar_cd));ab_step_ewar(&e);near(e.capacitor,fmaxf(0,611-expected));
        float estimate[10];ab_estimate_ewar(&e,estimate);assert(estimate[9]==0);
        for(int c=0;c<7;c++)near(estimate[c],e.ewar[c]);
        float scram=e.ewar[7];
        if(scram>0){
            e.prop_on=1;e.prop_is_mwd=0;ab_step_ewar(&e);assert(e.prop_on);
            e.prop_is_mwd=1;ab_step_ewar(&e);assert(!e.prop_on);e.prop_is_mwd=0;
        }
        if(e.ewar[5]<1){
            e.entity_count=2;e.entities[1]=(AbyssEntity){.kind=ENTITY_CACHE,.alive=1,.locked=1,.pos={e.lock_range,0,0}};
            ab_step_ewar(&e);assert(!e.entities[1].locked);e.entity_count=1;
        }
        // Repairs operate on their declared layer for every catalog type,
        // including damaged T1 bosses; never turn shield reps into armor reps.
        n->shield=n->armor=0;n->hull=n->hull_max;n->remote_repair=0;
        ab_step_npc_repairs(&e,1);
        near(n->shield,n->local_repair_layer==0?fminf(n->shield_max,n->local_repair):0);
        near(n->armor,n->local_repair_layer==1?fminf(n->armor_max,n->local_repair):0);
        memset(n->ewar_cd,0,sizeof(n->ewar_cd));ab_step_ewar(&e);
        float after=e.capacitor;ab_step_ewar(&e);near(e.capacitor,after); // no double cycle
        n->pos.x=1e7;memset(n->ewar_cd,0,sizeof(n->ewar_cd));e.capacitor=611;ab_step_ewar(&e);near(e.capacitor,611);
        for(int c=0;c<7;c++)near(e.ewar[c],1);
        if(AB_NPC_MECHANICS[type].spool_step>0){n->spool=1;n->spool_cd=0;float first=ab_npc_spool(n,0);near(first,1);
            for(int j=0;j<10000;j++)ab_npc_spool(n,0);
            near(n->spool,AB_NPC_MECHANICS[type].spool_max);
            near(ab_npc_spool(n,n->optimal+1),0);near(n->spool,1);}
        for(int layer=0;layer<3;layer++)assert(AB_NPC_MECHANICS[type].initial[layer]>=0);
    }
    // Killing a tender disables its swarm; suppressors can kill individual
    // drones and reserve stock replaces them without reusing dead slots.
    e.encounter_archetype=14;puf_reset(&e);ab_step_npc_drones(&e);int parent=-1;
    for(int i=0;i<e.entity_count;i++)if(e.entities[i].kind==ENTITY_HOSTILE&&e.entities[i].parent_index>=0){parent=e.entities[i].parent_index;assert(!e.entities[i].gate_required);}
    assert(parent>=0);e.entities[parent].alive=0;ab_step_npc_drones(&e);
    for(int i=0;i<e.entity_count;i++)if(e.entities[i].parent_index==parent)assert(!e.entities[i].alive&&!e.entities[i].drone_pending);
    puf_ini_free(&ini);
    puts("T1 mechanics and all archetype sampling checks passed");return 0;
}
