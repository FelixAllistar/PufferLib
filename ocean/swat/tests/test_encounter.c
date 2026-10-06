#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static void fixture(void) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.tactical_rules=true; sim.config.hostile_fire=false; sim.config.max_ticks=10000;
    sim.extraction=(b3Pos){-7,0,0}; sim.mission.staging=(b3Pos){0,0,0}; swat_world_init(&sim.world);
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(12,.5f,12),SWAT_CONCRETE,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){0,0,0},0);
    swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,(b3Pos){6,0,0},SWAT_PI);
    swat_sim_spawn_actor(&sim,2,SWAT_CIVILIAN,(b3Pos){1.2f,0,0},SWAT_PI);
    sim.actor_count=3;
    for(int t=0;t<10;t++) swat_sim_step(&sim,&(SwatInput){0});
}
static void navigation(void) {
    fixture(); int wall=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.1f,1.5f,12.5f),SWAT_DRYWALL,30);
    b3Pos next; assert(!swat_navigation_next(&sim,(b3Pos){0,0,0},(b3Pos){6,0,0},&next));
    assert(swat_world_damage(&sim.world,wall,100));
    assert(swat_navigation_next(&sim,(b3Pos){0,0,0},(b3Pos){6,0,0},&next));
    swat_sim_close(&sim);
    puts("PASS navigation: actual static clearance blocks a route; authoritative destruction invalidates the cache and opens it");
}
static void orders_and_escort(void) {
    fixture(); assert(swat_sim_set_player(&sim,1,true)); sim.actors[3].mind.bot=true; sim.actors[3].mind.team=1;
    SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[3].squad_order=SWAT_ORDER_HOLD; swat_encounter_orders(&sim,inputs); assert(!sim.actors[3].mind.pending_order);
    inputs[0].squad_order=SWAT_ORDER_HOLD; inputs[0].squad_queue=true;
    swat_encounter_orders(&sim,inputs); assert(sim.actors[3].mind.queued);
    sim.tick+=30; inputs[0].squad_order=0; swat_encounter_orders(&sim,inputs); assert(!sim.actors[3].mind.order);
    inputs[0].squad_execute=true; swat_encounter_orders(&sim,inputs); assert(sim.actors[3].mind.order==SWAT_ORDER_HOLD);
    assert(swat_sim_set_player(&sim,1,true)); assert(!sim.actors[3].mind.bot);
    inputs[0].squad_order=SWAT_ORDER_FALL_IN; swat_encounter_orders(&sim,inputs); assert(!sim.actors[3].mind.pending_order);
    SwatActor* civilian=&sim.actors[2]; civilian->gear.surrendered=civilian->gear.restrained=true;
    civilian->mind.escort_owner=0;
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){-4,.91f,0},b3Quat_identity);
    b3Pos before=swat_body_feet_position(&civilian->controller.body);
    for(int t=0;t<240;t++) swat_sim_step(&sim,&(SwatInput){0});
    assert(swat_body_feet_position(&civilian->controller.body).x<before.x-1);
    swat_sim_close(&sim);
    puts("PASS squad/escort: leader-only orders, delayed queue/execute, human replacement immune to bot orders, collision-based restrained civilian movement");
}
static void perception_evidence_roe(void) {
    fixture(); sim.actors[2].gear.surrendered=true;
    int wall=swat_world_box(&sim.world,(b3Pos){3,1.5f,0},swat_v(.1f,1.5f,12.5f),SWAT_CONCRETE,0);
    SwatInput inputs[SWAT_MAX_ACTORS]; swat_sim_bot_inputs(&sim,inputs); assert(sim.actors[1].mind.target==-1);
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){0,.9f,4},b3Quat_identity);
    swat_sim_bot_inputs(&sim,inputs); assert(sim.actors[1].mind.target==-1 && sim.actors[1].mind.memory_ticks==0);
    (void)wall;
    sim.actors[1].gear.surrendered=true; swat_encounter_step(&sim); assert(sim.evidence[1].dropped);
    swat_sim_damage_actor(&sim,1,0,10); assert(sim.debrief.roe_violations==1 && sim.debrief.unlawful_damage==10);
    sim.evidence[1].position=(b3Pos){.5f,.1f,0};
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){0,.9f,0},b3Quat_identity);
    sim.actors[0].controller.pitch=atan2f(.1f-swat_controller_eye(&sim.actors[0].controller).y,.5f);
    assert(swat_collect_evidence(&sim,0) && !swat_collect_evidence(&sim,0)); assert(sim.debrief.evidence==1);
    static SwatMap map; static SwatSnapshot state,decoded; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map); swat_capture_snapshot(&sim,1,&state);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&state); assert(size && swat_decode_snapshot(&decoded,bytes,size));
    assert(swat_apply_snapshot(&replica,&decoded));
    assert(replica.debrief.roe_violations==1 && replica.evidence[1].collected && replica.actors[1].mind.state==sim.actors[1].mind.state);
    for(size_t n=0;n<size;n++) assert(!swat_decode_snapshot(&decoded,bytes,n));
    swat_sim_close(&replica); swat_sim_close(&sim);
    puts("PASS encounter: no hidden-position pursuit, weapon evidence exactly once, unlawful force record, public state/debrief replication and truncation rejection");
}
int main(void) { navigation(); orders_and_escort(); perception_evidence_roe(); return 0; }
