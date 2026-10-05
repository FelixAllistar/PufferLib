#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded_map;
static SwatSnapshot snapshot,decoded;
static unsigned char packet[SWAT_NET_PACKET_MAX];
static void sync(void) {
    swat_capture_map(&sim,1,&map); size_t size=swat_encode_map(packet,sizeof(packet),&map);
    assert(size && swat_decode_map(&decoded_map,packet,size)); swat_apply_map(&replica,&decoded_map);
    swat_capture_snapshot(&sim,1,&snapshot); size=swat_encode_snapshot(packet,sizeof(packet),&snapshot);
    assert(size && swat_decode_snapshot(&decoded,packet,size) && swat_apply_snapshot(&replica,&decoded));
}
static void step(SwatInput in,int ticks) {
    SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[0]=in; for(int t=0;t<ticks;t++) swat_sim_step_inputs(&sim,inputs);
}
static void walls(void) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.mission=SWAT_RANGE; sim.config.hostile_fire=false; sim.config.max_ticks=2000;
    swat_world_init(&sim.world); swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(6,.5f,6),SWAT_CONCRETE,0);
    swat_build_framed_wall(&sim.world,(b3Pos){3,0,0},0,12,2.8f,0,0,0,0,false);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){1.5f,0,0},0); sim.actor_count=1;
    sim.actors[0].gear.breaching_charges=1;
    step(swat_neutral_input(),12);
    b3Pos next; assert(!swat_navigation_next(&sim,(b3Pos){1,0,0},(b3Pos){4.5f,0,0},&next));
    SwatHit hit=swat_context_hit(&sim,0,1.7f); assert(hit.kind==SWAT_HIT_WORLD && swat_world_breachable(&sim.world.objects[hit.index]));
    int id=hit.index,group=sim.world.objects[id].wall_group;
    assert(sim.world.objects[id].fractured); assert(swat_context(&sim,0).action==SWAT_CONTEXT_WALL);
    SwatInput in=swat_neutral_input(); in.door_tool=SWAT_PLACE_CHARGE; in.fire=true;
    step(in,30); assert(sim.actors[0].gear.breaching_charges==1 && sim.totals.shots==0);
    step(swat_neutral_input(),1); assert(sim.actors[0].gear.door_ticks==0);
    step(in,90); assert(sim.actors[0].gear.breaching_charges==0 && sim.world.objects[id].breach_owner==0);
    sync(); assert(replica.world.objects[id].breach_owner==0 && replica.world.objects[id].fractured);
    assert(b3Distance(sim.world.objects[id].breach_position,replica.world.objects[id].breach_position)<1e-5f);
    decoded.objects[id].active=false; assert(!swat_apply_snapshot(&replica,&decoded));
    decoded.objects[id].active=true; decoded.objects[id].breach_position.x+=10; assert(!swat_apply_snapshot(&replica,&decoded));
    assert(replica.world.objects[id].active && replica.world.objects[id].breach_owner==0);
    swat_sim_close(&replica);
    // A second actor cannot remotely detonate someone else's charge.
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,(b3Pos){-4,0,4},0); sim.actor_count=4;
    SwatInput all[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) all[i]=swat_neutral_input();
    all[3].door_tool=SWAT_DETONATE_CHARGE; swat_sim_step_inputs(&sim,all); assert(sim.world.objects[id].active);
    in=swat_neutral_input(); in.door_tool=SWAT_DETONATE_CHARGE; step(in,1);
    int skins=0,studs=0,intact=0;
    for(int i=0;i<sim.world.count;i++) if(sim.world.objects[i].wall_group==group) {
        SwatObject* o=&sim.world.objects[i]; if(o->active) intact++;
        else if(o->part==SWAT_PART_SKIN) skins++;
        else if(o->part==SWAT_PART_FRAME) studs++;
    }
    assert(skins>=4 && studs>=2 && intact>0 && sim.totals.destroyed>=skins+studs);
    assert(swat_world_visible(&sim.world,(b3Pos){2,1.2f,0},(b3Pos){4,1.2f,0}));
    assert(swat_navigation_next(&sim,(b3Pos){1,0,0},(b3Pos){4.5f,0,0},&next));
    assert(sim.navigation.updated_cells>0 && sim.navigation.updated_cells<SWAT_NAV_CELLS/4);
    static SwatNavigation incremental;
    incremental=sim.navigation; sim.navigation.built=false;
    assert(swat_navigation_next(&sim,(b3Pos){1,0,0},(b3Pos){4.5f,0,0},&next));
    assert(!memcmp(incremental.walkable,sim.navigation.walkable,sizeof(incremental.walkable)));
    assert(!memcmp(incremental.links,sim.navigation.links,sizeof(incremental.links)));
    // The human controller, rather than a teleported nav marker, crosses it.
    in=swat_neutral_input(); in.forward=1; step(in,180);
    assert(swat_body_feet_position(&sim.actors[0].controller.body).x>3.7f);
    sync(); assert(swat_world_visible(&replica.world,(b3Pos){2,1.2f,0},(b3Pos){4,1.2f,0}));
    assert(swat_navigation_next(&replica,(b3Pos){1,0,0},(b3Pos){4.5f,0,0},&next));
    swat_sim_close(&replica); swat_sim_close(&sim);
    puts("PASS scenario breach: interruptible finite wall charge, owner-only remote, both convex faces and segmented studs, actual controller/LOS/navigation traversal and replicated aperture");
}
static void buildings(void) {
    SwatConfig c=swat_default_config(); c.mission=SWAT_BUILDING; c.hostile_fire=false; c.tactical_rules=true; c.max_ticks=10000;
    unsigned identities=0,floors=0; int largest=0;
    for(unsigned seed=0;seed<10000;seed++) {
        SwatLayout candidate; assert(swat_building_plan(&candidate,seed,seed%3));
        assert(candidate.object_count<SWAT_MAX_OBJECTS && candidate.spawn_count==4+(int)(seed%3));
    }
    for(unsigned seed=0;seed<32;seed++) {
        SwatLayout a,b; assert(swat_building_plan(&a,seed,seed%3) && swat_building_plan(&b,seed,seed%3));
        assert(!memcmp(&a,&b,sizeof(a))); c.layout_seed=seed; c.difficulty=seed%3;
        swat_sim_init(&sim,c,42); assert(sim.world.room_count==6 && sim.world.count<SWAT_MAX_OBJECTS);
        largest=sim.world.count>largest ? sim.world.count : largest;
        assert(swat_sim_hostiles(&sim)==1+c.difficulty && swat_sim_unsecured(&sim)==3);
        for(int i=0;i<sim.layout.room_count;i++) if(!sim.layout.rooms[i].hall) identities|=1u<<sim.layout.rooms[i].identity;
        for(int i=0;i<sim.layout.spawn_count;i++) {
            const SwatPlanSpawn* p=&sim.layout.spawns[i]; b3Pos feet=swat_body_feet_position(&sim.actors[p->actor].controller.body);
            assert(fabs(feet.y-p->feet.y)<.15); floors|=1u<<(p->feet.y>1);
        }
        if(seed==0) {
            sync(); assert(replica.layout.fingerprint==sim.layout.fingerprint && replica.layout.furniture_count==sim.layout.furniture_count);
            for(int i=0;i<sim.world.count;i++) assert(sim.world.objects[i].fractured==replica.world.objects[i].fractured && sim.world.objects[i].wall_group==replica.world.objects[i].wall_group);
            swat_sim_close(&replica);
        }
        swat_sim_close(&sim);
    }
    assert(identities==7 && floors==3);
    printf("PASS scenario generation: 32 deterministic furnished two-floor seeds, three room identities, occupants on both floors, exact replication; largest %d objects\n",largest);
    c.layout_seed=0; swat_sim_init(&sim,c,42);
    for(int i=1;i<sim.actor_count;i++) if(sim.actors[i].present) { b3DestroyBody(sim.actors[i].controller.body.body); memset(&sim.actors[i],0,sizeof(sim.actors[i])); }
    sim.actor_count=1;
    b3Pos start={5.6f,0,-3.6f},goal={7.1f,3,3.2f},next;
    bool route=swat_navigation_next(&sim,start,goal,&next);
    if(!route) for(int n=0;n<SWAT_NAV_NODES;n++) if(fabsf(sim.navigation.x[n]-5.5f)<.05f && sim.navigation.z[n]>-4 && sim.navigation.z[n]<4)
        fprintf(stderr,"nav x%.1f z%.1f y%.2f clear%d links %x %x %x %x\n",sim.navigation.x[n],sim.navigation.z[n],sim.navigation.height[n],sim.navigation.walkable[n],sim.navigation.links[n][0],sim.navigation.links[n][1],sim.navigation.links[n][2],sim.navigation.links[n][3]);
    assert(route);
    SwatController* actor=&sim.actors[0].controller;
    b3Body_SetTransform(actor->body.body,b3OffsetPos(start,swat_v(0,actor->body.totalHeight*.5f+.02f,0)),b3Quat_identity);
    actor->yaw=SWAT_PI*.5f; SwatInput in=swat_neutral_input(); in.forward=1; in.gait=SWAT_SLOW;
    for(int t=0;t<340;t++) { step(in,1); if(swat_body_feet_position(&actor->body).z>2.9f) break; }
    b3Pos feet=swat_body_feet_position(&actor->body);
    printf("Stair traversal feet %.3f %.3f %.3f\n",(float)feet.x,(float)feet.y,(float)feet.z);
    assert(feet.y>2.85f && feet.z>2.7f);
    // Squad bot uses the same layered route and controller to ascend.
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,start,SWAT_PI*.5f); sim.actor_count=4;
    sim.actors[3].mind.bot=true; sim.actors[3].mind.order=SWAT_ORDER_MOVE; sim.actors[3].mind.goal=goal;
    b3Body_SetTransform(actor->body.body,(b3Pos){0,.95f,8},b3Quat_identity);
    for(int t=0;t<1200;t++) { swat_sim_step(&sim,&(SwatInput){0}); if(b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),goal)<.7f) break; }
    feet=swat_body_feet_position(&sim.actors[3].controller.body);
    printf("Squad upstairs feet %.3f %.3f %.3f\n",(float)feet.x,(float)feet.y,(float)feet.z);
    assert(feet.y>2.85f && b3Distance(feet,goal)<.8f);
    swat_sim_close(&sim); puts("PASS 3D navigation: real human stair ascent and commanded squad ascent into the upper storey");
}
int main(void) { setvbuf(stdout,NULL,_IOLBF,0); walls(); buildings(); return 0; }
