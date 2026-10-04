#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static void step(SwatInput in,int ticks) { for(int i=0;i<ticks;i++) swat_sim_step(&sim,&in); }
static void place(int actor,b3Pos feet,float yaw) {
    SwatController* c=&sim.actors[actor].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0)); c->yaw=yaw; c->pitch=0;
}
static void fixture(SwatMaterial floor) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.max_ticks=10000; sim.config.hostile_fire=false;
    swat_world_init(&sim.world); sim.extraction=(b3Pos){-15,0,0}; sim.actor_count=3;
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(20,.5f,20),floor,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-5,0,0},0);
    swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,(b3Pos){3,0,0},SWAT_PI);
    swat_sim_spawn_actor(&sim,2,SWAT_CIVILIAN,(b3Pos){10,0,5},SWAT_PI);
    step(swat_neutral_input(),15);
}
static float bounce(SwatMaterial material) {
    fixture(material); assert(swat_throw(&sim,0,SWAT_FLASHBANG));
    SwatProjectile* p=&sim.projectiles[0];
    assert(fabsf(b3Body_GetMass(p->body)-.42f)<.005f);
    b3Body_SetTransform(p->body,(b3Pos){0,2,0},b3Quat_identity);
    b3Body_SetLinearVelocity(p->body,swat_v(0,0,0)); b3Body_SetAngularVelocity(p->body,swat_v(0,0,0));
    float peak=0; bool touched=false;
    for(int i=0;i<150;i++) {
        b3World_Step(sim.world.id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);
        float y=(float)b3Body_GetPosition(p->body).y;
        if(y<.08f) touched=true;
        if(touched) peak=fmaxf(peak,y);
    }
    swat_sim_close(&sim); assert(touched); return peak;
}
static void physical_materials(void) {
    float hard=bounce(SWAT_TILE),soft=bounce(SWAT_CARPET);
    printf("Canister bounce: tile %.3f m / carpet %.3f m\n",hard,soft);
    assert(hard>soft+.25f && soft<.13f);
    fixture(SWAT_CONCRETE);
    swat_world_box(&sim.world,(b3Pos){1,2,0},swat_v(.00625f,2,2),SWAT_DRYWALL,50);
    assert(swat_throw(&sim,0,SWAT_FLASHBANG)); SwatProjectile* p=&sim.projectiles[0];
    b3Body_SetTransform(p->body,(b3Pos){0,1,0},b3Quat_identity);
    b3Body_SetLinearVelocity(p->body,swat_v(80,0,0));
    step(swat_neutral_input(),3);
    assert(p->position.x<1 && p->velocity.x<0);
    bool impact=false;
    for(int i=0;i<sim.sounds.count;i++) {
        const SwatSoundEvent* e=swat_sound_at(&sim.sounds,i);
        impact|=e->kind==SWAT_SOUND_IMPACT && e->material==SWAT_DRYWALL;
    }
    assert(impact); swat_sim_close(&sim);
    puts("PASS physical materials: mass, receiving-surface bounce, thin-wall collision and material impact event");
}
static void wand(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOUSE; config.hostile_fire=false;
    swat_sim_init(&sim,config,19); place(0,(b3Pos){3.3f,0,-2},0);
    SwatInput in=swat_neutral_input(); in.inspect=true; step(in,30);
    assert(sim.actors[0].gear.wand_mode==SWAT_WAND_UNDER);
    b3Pos inserted=swat_sim_inspection_camera(&sim,0);
    assert(inserted.x>4.1f && inserted.y<.075f);
    in.yaw_delta=.2f; in.pitch_delta=.08f; in.forward=1; in.fire=true;
    step(in,5); b3Pos turned=swat_sim_inspection_camera(&sim,0);
    assert(b3Distance(inserted,turned)<.01f && fabsf(sim.actors[0].controller.yaw)<.001f);
    assert(sim.actors[0].gear.wand_yaw>.9f && sim.actors[0].arsenal.shots==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); place(0,(b3Pos){.5f,0,0},0);
    swat_world_box(&sim.world,(b3Pos){1,1.3f,0},swat_v(.07f,1.3f,.35f),SWAT_CONCRETE,0);
    in=swat_neutral_input(); in.inspect=true; in.lean=-1; step(in,1);
    b3Pos corner=swat_sim_inspection_camera(&sim,0);
    assert(corner.x>1.1f && corner.z<-.8f);
    assert(swat_world_visible(&sim.world,corner,(b3Pos){2,1.7f,0}));
    swat_world_box(&sim.world,(b3Pos){.5f,1.3f,-.55f},swat_v(.3f,1.3f,.05f),SWAT_CONCRETE,0);
    b3Pos blocked=swat_sim_inspection_camera(&sim,0); assert(blocked.x<.6f && blocked.z>-.51f);
    swat_sim_close(&sim);
    puts("PASS optiwand: automatic closed-door insertion, independent lens rotation, body/fire lock and collision-limited corner reach");
}
static void throw_rules(void) {
    fixture(SWAT_CONCRETE); SwatInput in=swat_neutral_input(); in.throwable=1; in.fire=true; step(in,60);
    assert(sim.actors[0].gear.flashbangs==0 && sim.actors[0].arsenal.shots==1);
    int active=0; for(int i=0;i<SWAT_MAX_PROJECTILES;i++) active+=sim.projectiles[i].active;
    assert(active==1); assert(sim.actors[0].gear.used_tools);
    in=swat_neutral_input(); in.loadout=2; place(0,(b3Pos){0,0,0},0); step(in,1);
    assert(sim.actors[0].gear.kit==0 && sim.actors[0].gear.flashbangs==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE);
    swat_world_box(&sim.world,(b3Pos){-4.6f,1.3f,0},swat_v(.05f,1.3f,1),SWAT_CONCRETE,0);
    assert(!swat_throw(&sim,0,SWAT_FLASHBANG) && sim.actors[0].gear.flashbangs==1);
    swat_sim_close(&sim);
    puts("PASS throws: finite inventory, held-key edge, action recovery, no staging refill and hand clearance");
}
static void detonate_at(SwatProjectileKind kind,b3Pos p) {
    assert(swat_throw(&sim,0,kind));
    SwatProjectile* projectile=&sim.projectiles[0];
    b3Body_SetTransform(projectile->body,p,b3Quat_identity); b3Body_SetLinearVelocity(projectile->body,swat_v(0,0,0));
    projectile->remaining_ticks=1; projectile->age=89; step(swat_neutral_input(),1);
    assert(projectile->detonated);
}
static void effects(void) {
    fixture(SWAT_CONCRETE); int wall=swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.08f,1.5f,2),SWAT_WOOD,100);
    detonate_at(SWAT_FLASHBANG,(b3Pos){1,.06f,0}); assert(sim.actors[1].gear.flash_ticks==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); detonate_at(SWAT_FLASHBANG,(b3Pos){1,.06f,0});
    assert(sim.actors[1].gear.flash_ticks>80 && sim.actors[1].health==100 && !sim.actors[1].gear.surrendered);
    SwatInput command=swat_neutral_input(); command.command=true; step(command,1);
    assert(sim.actors[1].gear.surrendered); swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); wall=swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.08f,1.5f,2),SWAT_WOOD,100);
    detonate_at(SWAT_CS_GAS,(b3Pos){1,.06f,0}); step(swat_neutral_input(),300);
    assert(sim.actors[1].gear.gas_ticks==0 && !sim.actors[1].gear.surrendered);
    assert(swat_world_damage(&sim.world,wall,100)); step(swat_neutral_input(),30);
    assert(sim.actors[1].gear.gas_ticks>30 && sim.actors[1].gear.surrendered && sim.actors[1].health==100);
    place(0,(b3Pos){0,0,0},0); step(swat_neutral_input(),35); assert(sim.actors[0].gear.gas_ticks>30);
    swat_equipment_kit(&sim.actors[0].gear,1); sim.actors[0].gear.gas_ticks=0; step(swat_neutral_input(),35);
    assert(sim.actors[0].gear.gas_ticks==0);
    swat_sim_close(&sim);
    fixture(SWAT_CONCRETE); place(0,(b3Pos){-2,0,0},0);
    wall=swat_world_box(&sim.world,(b3Pos){1,1.5f,0},swat_v(.08f,1.5f,2),SWAT_WOOD,100);
    assert(swat_taser(&sim,0)); assert(sim.actors[1].gear.stunned_ticks==0);
    assert(sim.actors[0].gear.taser_charges==1 && !swat_taser(&sim,0));
    step(swat_neutral_input(),180); assert(swat_world_damage(&sim.world,wall,100));
    assert(swat_taser(&sim,0)); assert(sim.actors[1].gear.surrendered && sim.actors[1].health==100);
    assert(!swat_taser(&sim,0)); swat_sim_close(&sim);
    puts("PASS effects: flash occlusion/compliance, gas wall/destruction boundaries, masks, taser cover/range/recovery and zero damage");
}
static void network(void) {
    fixture(SWAT_CONCRETE); assert(swat_throw(&sim,0,SWAT_CS_GAS)); step(swat_neutral_input(),25);
    static SwatMap map; static SwatSnapshot snapshot,received; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&snapshot);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot); assert(size && swat_decode_snapshot(&received,bytes,size));
    assert(swat_apply_snapshot(&replica,&received));
    assert(replica.projectiles[0].active && replica.projectiles[0].kind==SWAT_CS_GAS);
    assert(b3Distance(sim.projectiles[0].position,replica.projectiles[0].position)<1e-5f);
    assert(replica.actors[0].gear.gas_grenades==0 && replica.actors[0].gear.used_tools);
    assert(B3_IS_NULL(replica.projectiles[0].body));
    for(size_t n=0;n<size;n++) assert(!swat_decode_snapshot(&received,bytes,n));
    SwatCommand in={.epoch=1,.sequence=1,.input={.taser=true,.throwable=2}},out;
    size=swat_encode_command(bytes,sizeof(bytes),&in); assert(size && swat_decode_command(&out,bytes,size));
    assert(out.input.taser && out.input.throwable==2);
    swat_sim_close(&sim); swat_sim_close(&replica);
    puts("PASS tactical protocol: flight, inventory, inputs, replica body isolation and every truncated packet rejected");
}
int main(void) { physical_materials(); wand(); throw_rules(); effects(); network(); return 0; }
