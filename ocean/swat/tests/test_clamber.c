// Clamber: jump while moving into a waist-high obstacle vaults over it or
// climbs onto it; anything else is an ordinary jump. Hands are busy throughout;
// the state survives encoded snapshots.
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static void step(SwatInput in,int ticks) { for(int i=0;i<ticks;i++) swat_sim_step(&sim,&in); }
static void fixture(void) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.max_ticks=10000; sim.config.hostile_fire=false;
    swat_world_init(&sim.world); sim.extraction=(b3Pos){-15,0,0}; sim.actor_count=1;
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(20,.5f,20),SWAT_CONCRETE,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-2,0,0},0);
    step(swat_neutral_input(),20);
}
static b3Pos feet(void) { return swat_body_feet_position(&sim.actors[0].controller.body); }
// Walk +X, press jump once at `at` metres from the start, then keep walking.
static int run(float jump_x,int ticks,bool fire) {
    SwatInput in=swat_neutral_input(); in.forward=1; int started=-1;
    for(int t=0;t<ticks;t++) {
        in.jump=feet().x>=jump_x && started<0; in.fire=fire && t%2;
        swat_sim_step(&sim,&in);
        if(started<0 && swat_controller_mantling(&sim.actors[0].controller)) started=t;
    }
    return started;
}
int main(void) {
    // 1. Vault a 0.95 m, 0.20 m thick wall: land on the far side at floor level.
    fixture(); swat_world_box(&sim.world,(b3Pos){0,.475f,0},swat_v(.10f,.475f,1.5f),SWAT_WOOD,0);
    float stamina=sim.actors[0].controller.stamina;
    int started=run(-.75f,150,false);
    printf("vault: started tick %d, feet %.2f %.2f %.2f\n",started,feet().x,feet().y,feet().z);
    assert(started>=0 && feet().x>.35f && fabsf(feet().y)<.06f && sim.actors[0].controller.stamina<stamina);
    // 2. Climb a 0.70 m, 1.6 m deep crate: stand on top.
    fixture(); swat_world_box(&sim.world,(b3Pos){.8f,.35f,0},swat_v(.8f,.35f,1),SWAT_WOOD,0);
    started=run(-.75f,40,false); step(swat_neutral_input(),40);
    printf("climb: started tick %d, feet %.2f %.2f %.2f\n",started,feet().x,feet().y,feet().z);
    assert(started>=0 && feet().y>.62f && feet().x>.05f);
    // 3. A 1.6 m wall is too tall: ordinary jump, never through it.
    fixture(); swat_world_box(&sim.world,(b3Pos){0,.8f,0},swat_v(.10f,.8f,1.5f),SWAT_WOOD,0);
    started=run(-.75f,120,false);
    assert(started<0 && feet().x<-.2f);
    // 4. A 0.25 m curb is a step, not a clamber.
    fixture(); swat_world_box(&sim.world,(b3Pos){0,.125f,0},swat_v(.10f,.125f,1.5f),SWAT_WOOD,0);
    started=run(-.75f,120,false);
    assert(started<0 && feet().x>.2f);
    // 5. No headroom: a beam 1.45 m up above a 0.95 m wall leaves no crouched
    // clearance over it, so the move is refused.
    fixture(); swat_world_box(&sim.world,(b3Pos){0,.475f,0},swat_v(.10f,.475f,1.5f),SWAT_WOOD,0);
    swat_world_box(&sim.world,(b3Pos){0,1.55f,0},swat_v(.6f,.10f,1.5f),SWAT_WOOD,0);
    started=run(-.75f,150,false);
    assert(started<0 && feet().x<0);
    // 6. Hands busy: holding fire through a vault fires no rounds while mantling.
    fixture(); swat_world_box(&sim.world,(b3Pos){0,.475f,0},swat_v(.10f,.475f,1.5f),SWAT_WOOD,0);
    SwatInput in=swat_neutral_input(); in.forward=1;
    while(feet().x<-.75f) swat_sim_step(&sim,&in);
    in.jump=true; swat_sim_step(&sim,&in); assert(swat_controller_mantling(&sim.actors[0].controller));
    int shots=sim.actors[0].arsenal.shots; in.jump=false;
    for(int t=0;t<60 && swat_controller_mantling(&sim.actors[0].controller);t++){in.fire=t%2;swat_sim_step(&sim,&in);}
    assert(sim.actors[0].arsenal.shots==shots);
    // 7. Mid-clamber state survives encoding and late-join application.
    fixture(); swat_world_box(&sim.world,(b3Pos){0,.475f,0},swat_v(.10f,.475f,1.5f),SWAT_WOOD,0);
    in=swat_neutral_input(); in.forward=1; while(feet().x<-.75f) swat_sim_step(&sim,&in);
    in.jump=true; swat_sim_step(&sim,&in); in.jump=false; swat_sim_step(&sim,&in); swat_sim_step(&sim,&in);
    assert(swat_controller_mantling(&sim.actors[0].controller));
    static SwatMap map; static SwatSnapshot state,decoded; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&state); size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&replica,&decoded));
    const SwatMantle* a=&sim.actors[0].controller.mantle,*b=&replica.actors[0].controller.mantle;
    assert(b->active && b->tick==a->tick && b->total==a->total && b3Distance(b->path[3],a->path[3])<1e-5f);
    SwatSnapshot bad=state; bad.actors[0].mantle.path[3].x+=10; n=swat_encode_snapshot(bytes,sizeof(bytes),&bad);
    assert(!n || !swat_decode_snapshot(&decoded,bytes,n));
    swat_sim_close(&replica);
    // 8. Open ground: jump is still a jump.
    fixture(); in=swat_neutral_input(); in.forward=1; step(in,10); in.jump=true; swat_sim_step(&sim,&in);
    assert(!swat_controller_mantling(&sim.actors[0].controller) && sim.actors[0].controller.jumped);
    puts("PASS clamber: vault, climb, too-tall and curb refusals, headroom refusal, hands busy, snapshot round trip and plain jump");
    return 0;
}
