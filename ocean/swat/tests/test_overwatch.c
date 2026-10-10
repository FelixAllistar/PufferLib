#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica;
static void step(SwatInput in,int ticks) { for(int i=0;i<ticks;i++) swat_sim_step(&sim,&in); }
static void place(int actor,b3Pos feet) {
    SwatController* c=&sim.actors[actor].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f+.01f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
}
static void assign(int unit,int post,int rifle) {
    SwatInput in=swat_neutral_input(); in.sniper_order=SWAT_SNIPER_ASSIGN;
    in.sniper_unit=unit; in.sniper_post=post; in.sniper_rifle=rifle; step(in,1);
    step(swat_neutral_input(),1);
}
static void mark(int unit,int target) {
    SwatActor* sniper=&sim.actors[swat_sniper_actor(unit)]; SwatActor* person=&sim.actors[target];
    b3Pos aim=b3OffsetPos(swat_body_feet_position(&person->controller.body),swat_v(0,person->controller.body.totalHeight*.9f,0));
    b3Vec3 delta=b3SubPos(aim,swat_controller_eye(&sniper->controller));
    sniper->controller.yaw=atan2f(delta.z,delta.x); sniper->controller.pitch=atan2f(delta.y,hypotf(delta.x,delta.z));
    SwatInput in=swat_neutral_input(); in.sniper_control=true; in.sniper_unit=unit; in.sniper_order=SWAT_SNIPER_DESIGNATE;
    step(in,1);
    printf("Sniper %d mark: target=%d status=%s\n",unit,sim.snipers[unit].target,swat_sniper_status(sim.snipers[unit].status));
    assert(sim.snipers[unit].target==target);
    step(swat_neutral_input(),3);
}
static void layered_glass(void) {
    SwatConfig config=swat_default_config();config.mission=SWAT_HOUSE;config.hostile_fire=false;
    swat_sim_init(&sim,config,44);assign(0,2,0);step(swat_neutral_input(),100);
    b3Pos eye=swat_controller_eye(&sim.actors[9].controller);
    b3Pos head=b3OffsetPos(swat_body_feet_position(&sim.actors[1].controller.body),swat_v(0,sim.actors[1].controller.body.totalHeight*.9f,0));
    b3Vec3 delta=b3SubPos(head,eye);
    b3Vec3 half=fabsf(delta.x)>fabsf(delta.z) ? swat_v(.003f,1,1) : swat_v(1,1,.003f);
    // These are physical panes, not a patched sight result. Nine layers exceed
    // the former optical traversal cap while remaining visible through glass.
    for(int i=0;i<9;i++)swat_world_box(&sim.world,b3OffsetPos(eye,swat_mul(delta,.25f+.05f*i)),half,SWAT_GLASS,8);
    mark(0,1);assert(sim.actors[9].arsenal.shots==0);
    swat_world_box(&sim.world,b3OffsetPos(eye,swat_mul(delta,.70f)),half,SWAT_STEEL,0);
    SwatInput in=swat_neutral_input();in.sniper_control=true;in.sniper_order=SWAT_SNIPER_DESIGNATE;
    step(in,1);assert(sim.snipers[0].target==-1 && !sim.actors[9].arsenal.shots);
    swat_sim_close(&sim);
    puts("PASS overwatch sight: nine physical clear panes permit designation, opaque cover blocks it, no unsolicited shot");
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOUSE; config.hostile_fire=false; config.max_ticks=10000;
    swat_sim_init(&sim,config,44);
    assign(0,2,0); assign(1,1,1); step(swat_neutral_input(),34);
    // Equipping can finish before ADS settles. The scope must not report READY
    // or shoot with the much wider hip-fire spread during that transition.
    assert(sim.actors[9].controller.ads<.98f && sim.snipers[0].status==SWAT_SNIPER_STEADYING);
    SwatInput early=swat_neutral_input(); early.sniper_control=true; early.fire=true;
    step(early,1); assert(sim.actors[9].arsenal.shots==0);
    early.fire=false; early.sniper_order=SWAT_SNIPER_DESIGNATE; step(early,1);
    assert(sim.snipers[0].target==1);
    early.sniper_order=SWAT_SNIPER_NONE; step(early,1);
    early.sniper_order=SWAT_SNIPER_EXECUTE; step(early,1);
    assert(sim.actors[9].arsenal.shots==0 && sim.actors[1].health==100);
    step(swat_neutral_input(),60);
    assert(sim.snipers[0].deployed && sim.snipers[1].deployed && sim.actors[9].role==SWAT_SNIPER);
    assert(sim.actors[9].arsenal.primary==3 && sim.actors[10].arsenal.primary==4);
    assert(fabsf((float)swat_controller_eye(&sim.actors[9].controller).y-2.4f)<.08f);
    assert(fabsf((float)swat_controller_eye(&sim.actors[10].controller).y-2.6f)<.08f);
    assign(1,2,1); assert(sim.snipers[1].post==1); // occupied post is rejected
    assert(swat_sim_set_player(&sim,1,true));
    SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[3].sniper_order=SWAT_SNIPER_ASSIGN; inputs[3].sniper_unit=0; inputs[3].sniper_post=0;
    swat_sim_step_inputs(&sim,inputs); assert(sim.snipers[0].post==2); swat_sim_set_player(&sim,1,false);
    mark(0,1); mark(1,8);
    assert(sim.snipers[0].status==SWAT_SNIPER_READY && sim.snipers[1].status==SWAT_SNIPER_READY);
    step(swat_neutral_input(),60); assert(sim.actors[9].arsenal.shots==0 && sim.actors[10].arsenal.shots==0);
    b3Pos original=swat_body_feet_position(&sim.actors[7].controller.body);
    place(7,(b3Pos){16.5f,0,3.28f}); step(swat_neutral_input(),3);
    SwatInput execute=swat_neutral_input(); execute.sniper_order=SWAT_SNIPER_EXECUTE;
    step(execute,1);
    assert(sim.actors[9].arsenal.shots==1 && sim.actors[10].arsenal.shots==0);
    assert(!sim.actors[1].alive && sim.actors[7].health==100);
    assert(sim.totals.destroyed>0); // the real shot broke the intervening window
    place(7,original); step(swat_neutral_input(),30);
    step(execute,45);
    assert(sim.actors[10].arsenal.shots==1 && sim.actors[8].alive && sim.actors[8].health<35);
    step(swat_neutral_input(),2); step(execute,1);
    assert(sim.actors[10].arsenal.shots==2 && !sim.actors[8].alive && sim.totals.civilian_damage==0);
    puts("PASS overwatch: authored post/rifle selection, physical platforms, leader authority, optical glass marking, hold, synchronized execute and hostage interlock");
    int rounds=swat_weapon_rounds(&sim.actors[9].arsenal.slots[0]);
    swat_sim_damage_actor(&sim,9,1,10);
    b3Pos old=b3Body_GetPosition(sim.actors[9].controller.body.body);
    assign(0,0,0); assert(sim.snipers[0].travel_ticks>170);
    assert(b3Distance(old,b3Body_GetPosition(sim.actors[9].controller.body.body))<.02f);
    step(swat_neutral_input(),190);
    assert(sim.snipers[0].post==0 && sim.snipers[0].travel_ticks==0 && sim.actors[9].health==90);
    assert(swat_weapon_rounds(&sim.actors[9].arsenal.slots[0])==rounds);
    assign(0,2,1); assert(sim.snipers[0].rifle==0 && sim.snipers[0].post==0);
    // Manual control also routes through the same rifle; the officer stays put.
    old=b3Body_GetPosition(sim.actors[0].controller.body.body);
    SwatInput manual=swat_neutral_input(); manual.sniper_control=true; manual.sniper_unit=0;
    manual.yaw_delta=.12f; manual.forward=1; manual.fire=true;
    step(manual,1); assert(sim.actors[9].arsenal.shots==2 && sim.actors[0].arsenal.shots==0);
    assert(b3Distance(old,b3Body_GetPosition(sim.actors[0].controller.body.body))<.01f);
    static SwatMap map; static SwatSnapshot state,decoded; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,4,&map); swat_apply_map(&replica,&map); swat_capture_snapshot(&sim,4,&state);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state); assert(n && swat_decode_snapshot(&decoded,bytes,n));
    assert(swat_apply_snapshot(&replica,&decoded));
    assert(replica.snipers[0].post==0 && replica.actors[9].arsenal.primary==3 && replica.actors[10].arsenal.shots==2);
    swat_sim_close(&replica);
    swat_sim_damage_actor(&sim,9,1,100); assign(0,2,0); assert(!sim.actors[9].alive && sim.snipers[0].post==0);
    swat_sim_close(&sim);
    puts("PASS overwatch: reposition delay, health/ammunition preservation, manual scope input, replicated rifle/state and no redeploy revival");
    layered_glass();
    return 0;
}
