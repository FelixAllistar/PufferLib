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
static int contact_overflows;
static bool track_breach,crossed_breach;
static float breach_plane;
static void physics_log(const char* message) {
    if(strstr(message,"triangle buffer capacity")) {
        contact_overflows++;
        fprintf(stderr,"Contact overflow at simulation tick %d\n",sim.tick);
    }
    puts(message);
}
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
    b3Pos before=feet(0);
    swat_sim_step(&sim,&in);assert(swat_replay_append(&journal,&in,&sim));
    b3Pos after=feet(0);
    if(track_breach && before.x<breach_plane && after.x>=breach_plane && fabsf(after.z+1.17f)<.8f)crossed_breach=true;
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
    // A 60 cm cell centre can be sqrt(2)*30 cm from the requested point.
    // Breach plane crossing, cuffs and extraction are checked independently.
    radius=fmaxf(radius,.43f);
    b3Pos waypoint=goal;int replan=0;
    for(int t=0;t<3000;t++) {
        b3Pos p=feet(0);b3Vec3 d=b3SubPos(goal,p);
        if(hypotf(d.x,d.z)<radius && fabsf(d.y)<.4f)return;
        SwatInput yield=swat_neutral_input();
        if(swat_navigation_yield_door(&sim,0,&yield)) {step(yield);replan=0;continue;}
        if(t>=replan || b3Distance(p,waypoint)<.3f) {
            if(!swat_navigation_next_for_actor(&sim,0,goal,&waypoint)) {
                // Wait for a moving door/person to clear before replanning.
                step(swat_neutral_input());continue;
            }
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
        if(!track_breach && hit.kind==SWAT_HIT_WORLD && hit.index>=0 && sim.world.objects[hit.index].door) {
            SwatObject* door=&sim.world.objects[hit.index];
            if(door->locked) {in.forward=0;in.door_tool=SWAT_LOCKPICK;}
            else if(!door->door_open) {in.forward=0;in.interact=!sim.actors[0].last_interact;}
        }
        step(in);
    }
    fprintf(stderr," walk goal %.3f %.3f %.3f waypoint %.3f %.3f %.3f\n",(float)goal.x,(float)goal.y,(float)goal.z,(float)waypoint.x,(float)waypoint.y,(float)waypoint.z);
    SwatTraceResult trace=swat_body_trace_body(&sim.actors[0].controller.body,feet(0),waypoint,1,1);
    if(trace.hit) {SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(trace.shapeId));if(tag)fprintf(stderr," blocked trace kind%d owner%d normal %.2f %.2f %.2f\n",tag->kind,tag->index,trace.normal.x,trace.normal.y,trace.normal.z);}
    for(int h=0;h<4;h++) {
        SwatHit ray=swat_world_ray(&sim.world,b3OffsetPos(feet(0),swat_v(0,.2f+h*.4f,0)),swat_direction(sim.actors[0].controller.yaw,0),1,sim.actors[0].controller.body.body);
        fprintf(stderr," forward y%.2f kind%d owner%d distance%.3f\n",.2f+h*.4f,ray.kind,ray.index,ray.distance);
    }
    fail("walking timeout");
}
static void secure(int actor) {
    if(!sim.actors[actor].alive) {
        if(sim.actors[actor].role!=SWAT_SUSPECT)fail("protected occupant killed");
        return; // A lawful squad response can end an armed threat before cuffing.
    }
    if(sim.actors[actor].gear.restrained)return;
    for(int t=0;t<600 && !sim.actors[actor].gear.surrendered;t++) {
        SwatInput in=look(chest(actor));SwatHit hit=swat_context_hit(&sim,0,9);
        if(hit.kind==SWAT_HIT_ACTOR && hit.index==actor) {
            in.command=t%30==0;
            in.taser=sim.actors[actor].role==SWAT_SUSPECT && t%30==15;
        }
        step(in);
    }
    if(!sim.actors[actor].gear.surrendered)fail("compliance");
    walk(feet(actor),1.45f);
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
    for(int t=0;t<240 && !sim.evidence[actor].collected;t++) {
        if(t==60) {
            b3Pos evidence=sim.evidence[actor].position;
            walk((b3Pos){evidence.x,feet(0).y,evidence.z+1.1f},.43f);
        }
        SwatInput in=look(sim.evidence[actor].position);SwatContext context=swat_context(&sim,0);
        in.interact=context.action==SWAT_CONTEXT_EVIDENCE && context.hit.index==actor && t%2==0;step(in);
    }
    if(!sim.evidence[actor].collected)fail("collect evidence");
}
static void evacuate(int actor,bool exterior) {
    for(int t=0;t<120 && sim.actors[actor].mind.escort_owner!=0;t++) {
        SwatInput in=look(chest(actor));in.interact=t%2==0;step(in);
    }
    if(sim.actors[actor].mind.escort_owner!=0)fail("start escort");
    if(exterior) {
        walk((b3Pos){-14,0,-1.17f},.4f);
        for(int t=0;t<1800 && feet(actor).x>=-12.2f;t++)idle(1);
        if(feet(actor).x>=-12.2f)fail("civilian did not follow through exterior breach");
        walk((b3Pos){-13,0,2.5f},.4f);
    } else walk((b3Pos){feet(0).x,0,2.5f},.3f);
    walk(sim.extraction,.35f);
    for(int t=0;t<3000 && !sim.actors[actor].rescued;t++)idle(1);
    if(!sim.actors[actor].rescued)fail("evacuation");
    printf("Evacuated actor %d at tick %d\n",actor,sim.tick);fflush(stdout);
}
static void enter_breach(float wall,b3Pos retreat) {
    // The route helper returns 60 cm cell centres. Arrival within 40 cm is
    // sufficient here; actual wall-plane crossing is checked independently.
    b3Pos start={wall-.8f,0,-1.17f},aim={wall,1,-1.17f};walk(start,.4f);
    for(int i=0;i<35;i++)step(look(aim));
    SwatHit hit=swat_context_hit(&sim,0,1.7f);
    if(hit.kind!=SWAT_HIT_WORLD || !swat_world_breachable(&sim.world.objects[hit.index]) || sim.world.objects[hit.index].door)fail("select masonry charge surface");
    int charges=sim.actors[0].gear.breaching_charges,destroyed=sim.totals.destroyed;
    for(int t=0;t<180 && sim.actors[0].gear.breaching_charges==charges;t++) {
        SwatInput in=look(aim);in.door_tool=SWAT_PLACE_CHARGE;step(in);
    }
    if(sim.actors[0].gear.breaching_charges!=charges-1)fail("mount finite charge");
    idle(1);walk(retreat,.4f);
    SwatInput detonate=swat_neutral_input();detonate.door_tool=SWAT_DETONATE_CHARGE;step(detonate);idle(180);
    if(sim.totals.destroyed<=destroyed)fail("remote masonry breach");
    walk(start,.4f);
    if(wall>-10 && sim.actors[watch_actor].gear.surrendered && b3Distance(feet(0),feet(watch_actor))<2) {
        secure(watch_actor);
        for(int t=0;t<120 && sim.actors[watch_actor].mind.escort_owner!=0;t++) {
            SwatInput follow=look(chest(watch_actor));follow.interact=t%2==0;step(follow);
        }
        if(sim.actors[watch_actor].mind.escort_owner!=0)fail("start arrested suspect escort");
        walk((b3Pos){wall-3.2f,0,2.5f},.43f);
        walk((b3Pos){wall-2,0,3.7f},.43f);
        for(int t=0;t<900 && b3Distance(feet(0),feet(watch_actor))>.85f;t++)idle(1);
        for(int t=0;t<120 && sim.actors[watch_actor].mind.escort_owner==0;t++) {
            SwatInput hold=look(chest(watch_actor));hold.interact=t%2==0;step(hold);
        }
        if(sim.actors[watch_actor].mind.escort_owner==0)fail("hold arrested suspect clear of breach");
        walk((b3Pos){wall-3.2f,0,2.5f},.43f);
        walk((b3Pos){wall-3.2f,0,-.85f},.43f);
        walk(start,.4f);
    }
    if(wall>-10) {
        // An occupant can swing the front leaf across this side-wall aperture.
        // Close that real, reachable obstruction before choosing this entry.
        b3Pos leaf={wall+.26f,1,-.54f};
        for(int t=0;t<35;t++)step(look(leaf));
        SwatHit obstruction=swat_context_hit(&sim,0,1.7f);
        if(obstruction.kind==SWAT_HIT_WORLD && sim.world.objects[obstruction.index].door && sim.world.objects[obstruction.index].door_open) {
            SwatInput close=look(leaf);close.interact=true;step(close);idle(120);
            printf("Closed obstructing leaf %d through breach at tick %d\n",obstruction.index,sim.tick);fflush(stdout);
        }
    }
    bool door_open[SWAT_MAX_OBJECTS];
    for(int i=0;i<sim.world.count;i++)door_open[i]=sim.world.objects[i].door_open;
    track_breach=true;crossed_breach=false;breach_plane=wall;
    walk((b3Pos){wall+.9f,0,-1.17f},.4f);track_breach=false;
    if(!crossed_breach)fail("entry detoured around breach");
    for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].door && !door_open[i] && sim.world.objects[i].door_open)fail("breach entry opened a door detour");
    printf("PASS actual player charge entry through x=%.1f at tick %d: placement, retreat, remote detonation and physical aperture crossing\n",wall,sim.tick);fflush(stdout);
}
int main(int argc,char** argv) {
    b3SetLogFcn(physics_log);
    assert(argc>=2 && argc<=4);bool combat=false;const char* route="front";
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"combat"))combat=true;
        else if(!strcmp(argv[i],"exterior")||!strcmp(argv[i],"interroom"))route=argv[i];
        else assert(false);
    }
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;swat_config_human(&cfg);
    cfg.randomize=false;cfg.hostile_fire=combat;cfg.max_ticks=60000;
    char save[1024];assert(snprintf(save,sizeof(save),"%s.save",argv[1])<(int)sizeof(save));
    int saved_tick=0;uint32_t saved_hash=0;
    swat_sim_init(&sim,cfg,81);assert(swat_replay_record(&journal,argv[1],&sim));
    SwatInput in=swat_neutral_input();in.squad_order=SWAT_ORDER_HOLD;
    if(strcmp(route,"front"))in.loadout=2; // Select the finite-charge Control kit at staging.
    step(in);idle(30);
    const int occupants[]={7,2,1,6,8};const float doors[]={-10.9f,-7.2f,-3.2f,.8f,4.8f};
    for(int i=0;i<5;i++) {
        watch_actor=occupants[i];
        if(i==0 && !strcmp(route,"exterior")) {
            walk((b3Pos){-13,0,2.5f},.3f);enter_breach(-12,(b3Pos){-15.5f,0,-1.17f});
        } else if(i==2 && !strcmp(route,"interroom")) {
            walk((b3Pos){doors[1],0,2.5f},.3f);walk((b3Pos){doors[1],0,-.85f},.3f);
            enter_breach(-4,(b3Pos){-7.2f,0,2.5f});
        } else {walk((b3Pos){doors[i],0,2.5f},.3f);walk((b3Pos){doors[i],0,-.85f},.3f);}
        secure(occupants[i]);
        if(sim.actors[occupants[i]].role==SWAT_CIVILIAN)evacuate(occupants[i],i==0&&!strcmp(route,"exterior"));
        else {collect(occupants[i]);walk((b3Pos){doors[i],0,2.5f},.3f);}
        if(i==2) {assert(swat_replay_checkpoint(&journal,save));saved_tick=sim.tick;saved_hash=swat_replay_digest(&sim);}
    }
    walk(sim.extraction,.35f);in=swat_neutral_input();in.squad_order=SWAT_ORDER_FALL_IN;step(in);
    for(int t=0;t<3000 && sim.end==SWAT_RUNNING;t++)idle(1);
    if(sim.end!=SWAT_SUCCESS)fail("completion");
    int dead_suspects=0;
    for(int i=0;i<sim.actor_count;i++)if(sim.actors[i].present && !sim.actors[i].alive) {
        assert(sim.actors[i].role==SWAT_SUSPECT && !sim.actors[i].gear.restrained);dead_suspects++;
    }
    assert(sim.debrief.arrests+dead_suspects==2 && sim.debrief.rescued==3 && sim.debrief.evidence==2 && !sim.debrief.roe_violations);
    // Entry choice does not script the encounter outcome. Hostile runs permit
    // lawful squad return fire on any approach; the checks above still reject
    // protected deaths, restrained deaths, unlawful force and missing evidence.
    if(!combat)assert(!dead_suspects);
    assert(swat_replay_close(&journal));
    printf("PASS motel complete player-input run (%s, %s route): %d ticks, %d arrests, %d armed suspects killed, three evacuations, two weapons, squad regroup, no civilian/officer deaths or unlawful force\n",cfg.hostile_fire?"hostile fire enabled":"interaction isolation",route,sim.tick,sim.debrief.arrests,dead_suspects);fflush(stdout);
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
    assert(!contact_overflows);
    puts("PASS full completion: exact every-tick replay, mid-run save/resume, fresh replica geometry/state, truthful final debrief and no contact-buffer overflow");return 0;
}
