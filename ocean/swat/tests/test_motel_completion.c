#include "replay.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This driver knows the level plan, like a repeatable human QA route. It only
// sends ordinary player inputs: no actor teleports, forced surrender, evidence
// collection, door state edits or scripted success conditions.
static SwatSim sim;
static SwatReplay journal;
static int watch_actor=-1;
static b3Pos feet(int actor) { return swat_body_feet_position(&sim.actors[actor].controller.body); }
static void fail(const char* task) {
    fprintf(stderr,"FAIL %s at tick %d end %d\n",task,sim.tick,sim.end);
    SwatHit h=swat_context_hit(&sim,0,2.2f);SwatController* c=&sim.actors[0].controller;
    fprintf(stderr," context kind%d index%d distance%.3f yaw%.3f pitch%.3f cuffticks%d last%d\n",h.kind,h.index,h.distance,c->yaw,c->pitch,sim.actors[0].gear.cuff_ticks,sim.actors[0].last_interact);
    for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present) {
        b3Pos p=feet(i);SwatActor* a=&sim.actors[i];
        fprintf(stderr," actor %d %.3f %.3f %.3f health%.1f surrender%d cuff%d escort%d rescued%d\n",i,(float)p.x,(float)p.y,(float)p.z,a->health,a->gear.surrendered,a->gear.restrained,a->mind.escort_owner,a->rescued);
        fprintf(stderr,"  waypoint %.3f %.3f %.3f replan%d yaw%.3f\n",(float)a->mind.waypoint.x,(float)a->mind.waypoint.y,(float)a->mind.waypoint.z,a->mind.replan_tick,a->controller.yaw);
    }
    (void)swat_replay_close(&journal);exit(1);
}
static void step(SwatInput in) {
    if(sim.end!=SWAT_RUNNING)fail("unexpected scenario end");
    swat_sim_step(&sim,&in);assert(swat_replay_append(&journal,&in,&sim));
}
static void idle(int ticks) { for(int i=0;i<ticks;i++)step(swat_neutral_input()); }
static SwatInput look(b3Pos point) {
    SwatInput in=swat_neutral_input();SwatController* c=&sim.actors[0].controller;
    b3Vec3 d=b3SubPos(point,swat_controller_eye(c));
    in.yaw_delta=swat_clamp(swat_angle(atan2f(d.z,d.x)-c->yaw),-6*SWAT_RAD,6*SWAT_RAD);
    in.pitch_delta=swat_clamp(atan2f(d.y,hypotf(d.x,d.z))-c->pitch,-6*SWAT_RAD,6*SWAT_RAD);
    return in;
}
static b3Pos chest(int actor) { return b3OffsetPos(swat_controller_eye(&sim.actors[actor].controller),swat_v(0,-.2f,0)); }
static void walk(b3Pos goal,float radius) {
    b3Pos waypoint=goal;int replan=0;
    for(int t=0;t<3000;t++) {
        b3Pos p=feet(0);b3Vec3 d=b3SubPos(goal,p);
        if(hypotf(d.x,d.z)<radius && fabsf(d.y)<.4f)return;
        if(t>=replan || b3Distance(p,waypoint)<.3f) {
            if(!swat_navigation_next(&sim,p,goal,&waypoint))fail("no walking route");
            replan=t+30;
        }
        SwatInput in=look(b3OffsetPos(waypoint,swat_v(0,1.65f,0)));
        float yaw=atan2f(waypoint.z-p.z,waypoint.x-p.x);
        if(fabsf(swat_angle(yaw-sim.actors[0].controller.yaw))<35*SWAT_RAD)in.forward=.8f;
        in.gait=SWAT_WALK;in.crouch=swat_navigation_crouch(&sim,p)||swat_navigation_crouch(&sim,waypoint);
        if(watch_actor>=0 && !sim.actors[watch_actor].gear.surrendered) {
            b3Pos eye=swat_controller_eye(&sim.actors[0].controller);b3Vec3 to=b3SubPos(chest(watch_actor),eye);
            SwatHit visible=swat_world_ray(&sim.world,eye,swat_normalize(to),b3Length(to)+.1f,sim.actors[0].controller.body.body);
            if(visible.kind==SWAT_HIT_ACTOR && visible.index==watch_actor) {
                in=look(chest(watch_actor)); // Stop and cover the exposed occupant before advancing.
                SwatHit aimed=swat_world_ray(&sim.world,eye,swat_controller_aim(&sim.actors[0].controller),7,sim.actors[0].controller.body.body);
                if(aimed.kind==SWAT_HIT_ACTOR && aimed.index==watch_actor) {
                    in.command=t%15==0;
                    in.taser=sim.actors[watch_actor].role==SWAT_SUSPECT && !sim.actors[0].gear.taser_cooldown && !sim.actors[0].gear.last_taser;
                }
            }
        }
        SwatHit hit=swat_context_hit(&sim,0,1.7f);
        if(hit.kind==SWAT_HIT_WORLD && hit.index>=0 && sim.world.objects[hit.index].door) {
            SwatObject* door=&sim.world.objects[hit.index];
            if(door->locked) {in.forward=0;in.door_tool=SWAT_LOCKPICK;}
            else if(!door->door_open) {in.forward=0;in.interact=!sim.actors[0].last_interact;}
        }
        step(in);
    }
    fprintf(stderr," walk goal %.3f %.3f %.3f waypoint %.3f %.3f %.3f\n",(float)goal.x,(float)goal.y,(float)goal.z,(float)waypoint.x,(float)waypoint.y,(float)waypoint.z);
    fail("walking timeout");
}
static void secure(int actor) {
    for(int t=0;t<600 && !sim.actors[actor].gear.surrendered;t++) {
        SwatInput in=look(chest(actor));SwatHit hit=swat_context_hit(&sim,0,9);
        if(hit.kind==SWAT_HIT_ACTOR && hit.index==actor) {
            in.command=t%30==0;
            in.taser=sim.actors[actor].role==SWAT_SUSPECT && t%30==15;
        }
        step(in);
    }
    if(!sim.actors[actor].gear.surrendered)fail("compliance");
    walk(feet(actor),1.15f);
    for(int t=0;t<60;t++)step(look(chest(actor)));
    for(int t=0;t<180 && !sim.actors[actor].gear.restrained;t++) {
        SwatInput in=look(chest(actor));SwatHit hit=swat_context_hit(&sim,0,1.7f);
        in.interact=hit.kind==SWAT_HIT_ACTOR && hit.index==actor;step(in);
    }
    if(!sim.actors[actor].gear.restrained)fail("cuffing");
    idle(1);
    printf("Secured actor %d at tick %d\n",actor,sim.tick);fflush(stdout);
}
static void collect(int actor) {
    for(int t=0;t<120 && !sim.evidence[actor].collected;t++) {
        SwatInput in=look(sim.evidence[actor].position);in.interact=t%2==0;step(in);
    }
    if(!sim.evidence[actor].collected)fail("collect evidence");
}
static void evacuate(int actor) {
    for(int t=0;t<120 && sim.actors[actor].mind.escort_owner!=0;t++) {
        SwatInput in=look(chest(actor));in.interact=t%2==0;step(in);
    }
    if(sim.actors[actor].mind.escort_owner!=0)fail("start escort");
    walk((b3Pos){feet(0).x,0,2.5f},.3f);
    walk(sim.extraction,.35f);
    for(int t=0;t<3000 && !sim.actors[actor].rescued;t++)idle(1);
    if(!sim.actors[actor].rescued)fail("evacuation");
    printf("Evacuated actor %d at tick %d\n",actor,sim.tick);fflush(stdout);
}
int main(int argc,char** argv) {
    assert(argc==2 || (argc==3 && !strcmp(argv[2],"combat")));
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;swat_config_human(&cfg);
    cfg.randomize=false;cfg.hostile_fire=argc==3;cfg.max_ticks=60000;
    char save[1024];assert(snprintf(save,sizeof(save),"%s.save",argv[1])<(int)sizeof(save));
    int saved_tick=0;uint32_t saved_hash=0;
    swat_sim_init(&sim,cfg,81);assert(swat_replay_record(&journal,argv[1],&sim));
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_HOLD;step(in);idle(30);
    const int occupants[]={7,2,1,6,8};const float doors[]={-10.9f,-7.2f,-3.2f,.8f,4.8f};
    for(int i=0;i<5;i++) {
        watch_actor=occupants[i];
        walk((b3Pos){doors[i],0,2.5f},.3f);walk((b3Pos){doors[i],0,-.85f},.3f);
        secure(occupants[i]);
        if(sim.actors[occupants[i]].role==SWAT_CIVILIAN)evacuate(occupants[i]);
        else {collect(occupants[i]);walk((b3Pos){doors[i],0,2.5f},.3f);}
        if(i==2) {assert(swat_replay_checkpoint(&journal,save));saved_tick=sim.tick;saved_hash=swat_replay_digest(&sim);}
    }
    walk(sim.extraction,.35f);in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;step(in);
    for(int t=0;t<3000 && sim.end==SWAT_RUNNING;t++)idle(1);
    if(sim.end!=SWAT_SUCCESS)fail("completion");
    assert(sim.debrief.arrests==2 && sim.debrief.rescued==3 && sim.debrief.evidence==2 && !sim.debrief.roe_violations);
    for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present)assert(sim.actors[i].alive);
    assert(swat_replay_close(&journal));
    printf("PASS motel complete player-input run (%s): %d ticks, two arrests, three evacuations, two weapons, squad regroup, no casualties/unlawful force\n",cfg.hostile_fire?"hostile fire enabled":"interaction isolation",sim.tick);fflush(stdout);
    static SwatSim replica;static SwatMap map;static SwatSnapshot snapshot,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);swat_capture_snapshot(&sim,1,&snapshot);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);assert(size && swat_decode_snapshot(&decoded,bytes,size));
    assert(swat_apply_snapshot(&replica,&decoded));
    assert(replica.end==SWAT_SUCCESS && !memcmp(&replica.debrief,&sim.debrief,sizeof(sim.debrief)));
    assert(swat_sim_progress(&replica).secured && !swat_sim_progress(&replica).officers_away);
    swat_sim_close(&replica);swat_sim_close(&sim);
    SwatReplay reader;assert(swat_replay_open(&reader,argv[1]));swat_sim_init(&sim,reader.config,reader.seed);
    uint32_t expected;int status;
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        swat_sim_step(&sim,&in);
        if(swat_replay_digest(&sim)!=expected)fail("input replay divergence");
    }
    assert(status==0 && sim.end==SWAT_SUCCESS && swat_replay_close(&reader));swat_sim_close(&sim);
    char error[256];assert(swat_replay_restore(&sim,save,error,sizeof(error)));
    assert(sim.tick==saved_tick && swat_replay_digest(&sim)==saved_hash);
    assert(swat_replay_open(&reader,argv[1]));
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        if((int)reader.frame<=saved_tick)continue;
        swat_sim_step(&sim,&in);
        if(swat_replay_digest(&sim)!=expected)fail("saved continuation divergence");
    }
    assert(status==0 && sim.end==SWAT_SUCCESS && swat_replay_close(&reader));
    swat_sim_close(&sim);assert(!remove(save));
    puts("PASS full completion: exact every-tick replay, mid-run save/resume, fresh replica geometry/state and truthful final debrief");return 0;
}
