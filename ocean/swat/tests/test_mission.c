#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static void ticks(SwatInput input,int n) { for(int i=0;i<n;i++) swat_sim_step(&sim,&input); }
static void place(int actor,b3Pos feet,float yaw) {
    SwatController* c=&sim.actors[actor].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
    c->yaw=yaw; c->pitch=0;
}
static void aim_at(int actor) {
    SwatController* c=&sim.actors[0].controller;
    b3Pos feet=swat_body_feet_position(&sim.actors[actor].controller.body);
    b3Vec3 delta=b3SubPos(b3OffsetPos(feet,swat_v(0,sim.actors[actor].controller.body.totalHeight*.7f,0)),swat_controller_eye(c));
    c->yaw=atan2f(delta.z,delta.x); c->pitch=atan2f(delta.y,hypotf(delta.x,delta.z));
}
static void cuff(int actor) {
    SwatInput in=swat_neutral_input(); in.interact=true;
    for(int i=0;i<74;i++) { aim_at(actor); ticks(in,1); }
    assert(sim.actors[actor].gear.restrained);
    ticks(swat_neutral_input(),1);
}
static void framed_wall(void) {
    static SwatWorld world; swat_world_init(&world);
    swat_build_framed_wall(&world,(b3Pos){5,0,0},0,4,2.7f,0,0,0,0,false);
    b3Pos from={0,1.3f,.17f}; b3Vec3 direction={1,0,0};
    SwatHit first=swat_world_ray(&world,from,direction,10,b3_nullBodyId);
    assert(first.hit && world.objects[first.index].part==SWAT_PART_SKIN);
    assert(fabsf(2*world.objects[first.index].half.x-.0125f)<1e-6f);
    assert(swat_world_damage(&world,first.index,1000));
    SwatHit second=swat_world_ray(&world,from,direction,10,b3_nullBodyId);
    assert(second.hit && second.index!=first.index && world.objects[second.index].part==SWAT_PART_SKIN);
    assert(swat_world_damage(&world,second.index,1000));
    assert(!swat_world_ray(&world,from,direction,10,b3_nullBodyId).hit);
    from.z=0;
    SwatHit stud=swat_world_ray(&world,from,direction,10,b3_nullBodyId);
    assert(stud.hit && world.objects[stud.index].part==SWAT_PART_FRAME);
    swat_world_close(&world);
    puts("PASS house wall: two independent 12.5 mm faces, open cavity after break, retained timber stud collision");
}
static void equipment_and_inspection(void) {
    SwatInput in=swat_neutral_input(); in.loadout=3; ticks(in,1);
    assert(sim.actors[0].gear.kit==2 && sim.actors[0].controller.mobility<.8f);
    in.loadout=2; ticks(in,1);
    assert(sim.actors[0].arsenal.primary==2 && sim.actors[0].arsenal.slots[0].magazine==5);
    in=swat_neutral_input(); ticks(in,35);
    place(0,(b3Pos){3.3f,0,-2},0);
    b3Pos blocked=swat_sim_inspection_camera(&sim,0);
    assert(blocked.x<4 && blocked.x>3.5f);
    in.inspect=true; in.crouch=true; ticks(in,30);
    b3Pos under=swat_sim_inspection_camera(&sim,0);
    assert(under.x>4.1f && under.y<.075f && sim.actors[0].gear.inspecting);
    int shots=sim.actors[0].arsenal.shots; in.fire=true; ticks(in,1);
    assert(sim.actors[0].arsenal.shots==shots);
    // Away from staging the authority rejects equipment changes.
    in.loadout=3; ticks(in,1); assert(sim.actors[0].gear.kit==1);
    in=swat_neutral_input(); ticks(in,30);
    SwatHit door=swat_world_ray(&sim.world,swat_controller_eye(&sim.actors[0].controller),swat_v(1,0,0),2,sim.actors[0].controller.body.body);
    assert(door.hit && sim.world.objects[door.index].door);
    place(1,(b3Pos){4.8f,0,-2},SWAT_PI); aim_at(1);
    SwatShot impact={.fired=true,.damage=6,.range=18,.energy=.14f};
    swat_sim_shoot(&sim,0,swat_controller_eye(&sim.actors[0].controller),swat_controller_aim(&sim.actors[0].controller),impact);
    assert(sim.actors[1].health==100 && sim.world.objects[door.index].health==120);
    place(1,(b3Pos){15,0,-2},SWAT_PI);
    place(0,swat_mission(SWAT_HOUSE)->staging,0); in.loadout=3; ticks(in,1);
    place(0,(b3Pos){3.3f,0,-2},0); in=swat_neutral_input(); in.melee=true; ticks(in,1);
    assert(!sim.world.objects[door.index].active);
    place(0,swat_mission(SWAT_HOUSE)->staging,0); in=swat_neutral_input(); in.loadout=2; ticks(in,1);
    b3Pos from={6.5f,1.4f,7}; b3Vec3 toward={0,0,-1};
    SwatHit rotated=swat_world_ray(&sim.world,from,toward,3,b3_nullBodyId);
    assert(rotated.hit && sim.world.objects[rotated.index].door);
    sim.world.objects[rotated.index].door_open=true;
    for(int i=0;i<60;i++) swat_world_step_doors(&sim.world);
    assert(!swat_world_ray(&sim.world,from,toward,3,b3_nullBodyId).hit);
    sim.world.objects[rotated.index].door_open=false;
    for(int i=0;i<60;i++) swat_world_step_doors(&sim.world);
    assert(swat_world_ray(&sim.world,from,toward,3,b3_nullBodyId).index==rotated.index);
    puts("PASS equipment: mass/mobility, impact primary, staging restriction, blocked lens, floor-gap optiwand and fire isolation");
    puts("PASS house doors: impact rounds stop at closed wood, ram removes the real leaf, rotated hinge opens/closes its collider");
}
static void arrests_and_injuries(void) {
    // Use deterministic fixture placement to test interactions independently
    // of navigation. No surrender/restraint fields are set by this driver.
    place(0,(b3Pos){13.7f,0,-2},0); aim_at(1);
    SwatShot impact={.fired=true,.damage=6,.range=18,.energy=.14f};
    swat_sim_shoot(&sim,0,swat_controller_eye(&sim.actors[0].controller),swat_controller_aim(&sim.actors[0].controller),impact);
    assert(sim.actors[1].alive && sim.actors[1].gear.surrendered && sim.actors[1].gear.stunned_ticks>0);
    SwatInput in=swat_neutral_input(); ticks(in,30); aim_at(1);
    int shots=sim.actors[0].arsenal.shots;
    in.interact=true; in.fire=true; ticks(in,25); assert(sim.actors[0].gear.cuff_ticks>0 && !sim.actors[1].gear.restrained);
    assert(sim.actors[0].arsenal.shots==shots);
    ticks(swat_neutral_input(),1); assert(sim.actors[0].gear.cuff_ticks==0);
    cuff(1);
    place(0,(b3Pos){11.7f,0,3.4f},0); aim_at(8);
    in=swat_neutral_input(); in.melee=true; ticks(in,1);
    assert(sim.actors[8].gear.surrendered); ticks(swat_neutral_input(),30); cuff(8);
    assert(swat_sim_hostiles(&sim)==0 && sim.end==SWAT_RUNNING);
    const int civilians[3]={2,6,7};
    for(int i=0;i<3;i++) {
        int id=civilians[i]; b3Pos feet=swat_body_feet_position(&sim.actors[id].controller.body);
        place(0,b3OffsetPos(feet,swat_v(0,0,.95f)),-SWAT_PI*.5f); aim_at(id);
        in=swat_neutral_input(); in.command=true; ticks(in,1);
        assert(sim.actors[id].gear.surrendered); ticks(swat_neutral_input(),30); cuff(id);
    }
    assert(swat_sim_unsecured(&sim)==0 && sim.totals.civilian_damage==0);
    float mobility=swat_equipment_mobility(&sim.actors[0].gear);
    swat_sim_damage_region(&sim,0,1,30,SWAT_LEGS,false); ticks(swat_neutral_input(),1);
    assert(sim.actors[0].gear.wounds[SWAT_LEGS]>0 && sim.actors[0].controller.mobility<mobility*.9f);
    place(0,sim.extraction,0); ticks(swat_neutral_input(),1); assert(sim.end==SWAT_SUCCESS);
    puts("PASS arrests: less-lethal surrender, interruptible cuff hold, melee, civilian compliance, injury mobility and secured-house extraction");
}
static void replication(void) {
    static SwatMap map,decoded;
    static SwatSnapshot snapshot,received;
    static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,7,&map);
    size_t size=swat_encode_map(bytes,sizeof(bytes),&map); assert(size && swat_decode_map(&decoded,bytes,size));
    swat_apply_map(&replica,&decoded); assert(replica.config.mission==SWAT_HOUSE && replica.world.room_count==3);
    swat_capture_snapshot(&sim,7,&snapshot); snapshot.player_mask=1;
    size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot); assert(size && swat_decode_snapshot(&received,bytes,size));
    assert(swat_apply_snapshot(&replica,&received));
    assert(replica.actors[0].gear.wounds[SWAT_LEGS]==sim.actors[0].gear.wounds[SWAT_LEGS]);
    assert(replica.actors[1].gear.restrained && replica.actors[7].gear.restrained && replica.actors[0].arsenal.primary==2);
    assert(replica.end==SWAT_SUCCESS && replica.world.count==sim.world.count);
    swat_sim_close(&replica);
    puts("PASS house protocol: full framed map, rooms, impact launcher, wounds, restraints and mission outcome round-trip");
}
int main(void) {
    framed_wall();
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOUSE; config.hostile_fire=false; config.randomize=false; config.max_ticks=10000;
    swat_sim_init(&sim,config,42);
    printf("House: %d physical objects, %d rooms, %d active threats, %d hostages\n",sim.world.count,sim.world.room_count,swat_sim_hostiles(&sim),swat_sim_unsecured(&sim));
    assert(sim.world.count<SWAT_MAX_OBJECTS && swat_sim_hostiles(&sim)==2 && swat_sim_unsecured(&sim)==3);
    equipment_and_inspection(); arrests_and_injuries(); replication();
    swat_sim_close(&sim); return 0;
}
