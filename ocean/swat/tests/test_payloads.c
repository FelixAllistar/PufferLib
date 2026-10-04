#include "protocol.h"
#include <assert.h>
#include <stdio.h>
static SwatSim sim;
static void fixture(void) {
    SwatConfig c=swat_default_config(); c.mission=SWAT_RANGE; c.hostile_fire=false; c.max_ticks=10000;
    swat_sim_init(&sim,c,71); swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,(b3Pos){3,0,0},SWAT_PI); sim.actor_count=3;
}
static void ticks(int count) { SwatInput in=swat_neutral_input(); for(int t=0;t<count;t++) swat_sim_step(&sim,&in); }
static void physical_payloads(void) {
    fixture(); assert(swat_launch(&sim,0,SWAT_IMPACT_ROUND,(b3Pos){.6f,1.2f,0},swat_v(1,0,0),6,18));
    ticks(1); assert(sim.actors[1].health==100 && !sim.actors[1].gear.surrendered);
    ticks(10); assert(sim.actors[1].health<100 && sim.actors[1].health>90 && sim.actors[1].gear.surrendered);
    swat_sim_close(&sim);
    fixture(); swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.0125f,1.5f,2),SWAT_DRYWALL,100);
    assert(swat_launch(&sim,0,SWAT_IMPACT_ROUND,(b3Pos){.6f,1.2f,0},swat_v(1,0,0),6,18)); ticks(15);
    assert(sim.actors[1].health==100 && !sim.actors[1].gear.surrendered && sim.projectiles[0].position.x<2);
    swat_sim_close(&sim);
    fixture(); assert(swat_launch(&sim,0,SWAT_PEPPERBALL,(b3Pos){.6f,1.2f,0},swat_v(1,0,0),1,22)); ticks(35);
    assert(sim.actors[1].gear.gas_ticks>30 && sim.actors[1].gear.surrendered && sim.actors[1].health>98);
    swat_sim_close(&sim);
    for(int kind=SWAT_LAUNCH_CS;kind<=SWAT_LAUNCH_FLASH;kind++) {
        fixture(); assert(swat_launch(&sim,0,(SwatProjectileKind)kind,(b3Pos){.6f,1.2f,0},swat_v(1,0,0),0,18)); ticks(12);
        assert(sim.projectiles[0].detonated && sim.projectiles[0].kind==(SwatProjectileKind)kind);
        if(kind==SWAT_LAUNCH_CS) { ticks(30); assert(sim.actors[1].gear.gas_ticks>0); }
        else assert(sim.actors[1].gear.flash_ticks>0);
        swat_sim_close(&sim);
    }
    puts("PASS physical payloads: delayed flight/region injury, thin-wall interception, breakable irritant projectiles and distinct CS/flash effects");
}
static void probes_and_tether(void) {
    fixture(); assert(swat_launch(&sim,0,SWAT_PROBE,(b3Pos){.6f,1.4f,0},swat_v(1,0,0),0,10)); ticks(5);
    assert(sim.projectiles[0].target==1 && !sim.actors[1].gear.stunned_ticks && !sim.actors[1].gear.surrendered);
    assert(swat_launch(&sim,0,SWAT_PROBE,(b3Pos){.6f,.9f,0},swat_v(1,0,0),0,10)); ticks(5);
    assert(sim.projectiles[1].target==1 && sim.actors[1].gear.stunned_ticks>0 && sim.actors[1].gear.surrendered && sim.actors[1].health==100);
    swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.05f,1.5f,2),SWAT_CONCRETE,0);
    sim.actors[1].gear.stunned_ticks=0; sim.actors[1].gear.surrendered=false; ticks(3); assert(!sim.actors[1].gear.stunned_ticks);
    SwatArsenal a; swat_weapons_init(&a,1); swat_weapons_primary(&a,10); assert(swat_weapon_rounds(&a.slots[0])==10 && !a.slots[0].reserve);
    swat_sim_close(&sim);
    fixture(); assert(swat_launch(&sim,0,SWAT_BOLA,(b3Pos){.6f,.55f,0},swat_v(1,0,0),0,6)); ticks(10);
    assert(sim.actors[1].gear.tether_ticks>0 && sim.actors[1].gear.surrendered && !sim.actors[1].gear.restrained && sim.actors[1].health==100);
    swat_sim_close(&sim);
    puts("PASS probes/tether: physical single-contact failure, two separated contacts, occluded connection break, ten finite probes, temporary tether then normal cuffs");
}
int main(void) { physical_payloads(); probes_and_tether(); return 0; }
