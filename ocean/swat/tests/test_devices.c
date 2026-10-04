#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static void fixture(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_RANGE; config.max_ticks=10000; config.hostile_fire=false;
    swat_sim_init(&sim,config,11);
}
static void tick(SwatInput input,int count) { for(int t=0;t<count;t++) swat_sim_step(&sim,&input); }
static void deploy_recover(void) {
    fixture(); SwatInput input=swat_neutral_input(); input.device_deploy=1; input.fire=false; tick(input,50);
    assert(sim.devices[0].active && sim.devices[0].kind==SWAT_CAMERA && sim.actors[0].gear.devices[0]==0);
    assert(!swat_device_deploy(&sim,0,SWAT_CAMERA)); int count=0; for(int i=0;i<SWAT_MAX_DEVICES;i++) count+=sim.devices[i].active; assert(count==1);
    SwatDevice* d=&sim.devices[0]; b3Body_SetTransform(d->body,(b3Pos){.8f,.7f,0},b3Quat_identity); b3Body_SetLinearVelocity(d->body,swat_v(0,0,0)); d->position=b3Body_GetPosition(d->body);
    SwatController* c=&sim.actors[0].controller; c->pitch=atan2f(.7f-swat_controller_eye(c).y,.8f);
    assert(swat_context(&sim,0).action==SWAT_CONTEXT_DEVICE && swat_device_recover(&sim,0));
    assert(!d->active && sim.actors[0].gear.devices[0]==1 && !swat_device_recover(&sim,0));
    swat_sim_close(&sim); puts("PASS camera: physical throw, finite held-edge stock, aimed owner recovery and no duplicate recovery");
}
static void remote_motion(void) {
    for(int kind=SWAT_ROBOT;kind<=SWAT_DRONE;kind+=2) {
        fixture(); assert(swat_device_deploy(&sim,0,(SwatDeviceKind)kind));
        SwatDevice* d=&sim.devices[0]; d->position=(b3Pos){.7f,kind==SWAT_DRONE ? 1.6f : .2f,0};
        b3Body_SetTransform(d->body,d->position,b3Quat_identity); b3Body_SetLinearVelocity(d->body,swat_v(0,0,0));
        swat_world_box(&sim.world,(b3Pos){1.5f,1.5f,0},swat_v(.0125f,1.5f,2),SWAT_DRYWALL,50);
        b3Pos officer=swat_body_feet_position(&sim.actors[0].controller.body);
        SwatInput in=swat_neutral_input(); in.device_control=true; in.device_unit=0; in.forward=1; in.fire=true; tick(in,120);
        assert(d->position.x<1.5f && b3Distance(officer,swat_body_feet_position(&sim.actors[0].controller.body))<.1f && !sim.actors[0].arsenal.shots);
        assert(swat_sim_set_player(&sim,1,true)); SwatInput inputs[SWAT_MAX_ACTORS]; for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
        float yaw=d->yaw; inputs[3]=in; inputs[3].yaw_delta=.2f; swat_devices_inputs(&sim,inputs); assert(d->yaw==yaw);
        d->battery_ticks=1; tick(swat_neutral_input(),1); assert(!swat_feed_present(&sim,SWAT_SNIPERS));
        swat_device_damage(&sim,0,100); assert(!d->active && B3_IS_NULL(d->body));
        swat_sim_close(&sim);
    }
    puts("PASS robot/drone: physical thin-wall collision, stationary officer/fire isolation, owner-only steering, finite battery and physical destruction");
}
static void communication_and_replication(void) {
    fixture(); assert(swat_device_deploy(&sim,0,SWAT_BALL)); SwatDevice* d=&sim.devices[0];
    d->position=(b3Pos){1,1,0}; b3Body_SetTransform(d->body,d->position,b3Quat_identity); b3Body_SetLinearVelocity(d->body,swat_v(0,0,0));
    swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,(b3Pos){3,0,0},SWAT_PI); sim.actor_count=3; sim.actors[1].mind.resolve=.1f;
    int wall=swat_world_box(&sim.world,(b3Pos){2,1.5f,0},swat_v(.1f,1.5f,2),SWAT_DRYWALL,30);
    SwatInput in=swat_neutral_input(); in.device_control=true; in.device_unit=0; in.command=true; tick(in,1); assert(!sim.actors[1].gear.surrendered);
    assert(swat_world_damage(&sim.world,wall,100)); tick(in,100); assert(sim.actors[1].gear.surrendered);
    static SwatMap map; static SwatSnapshot snapshot,decoded; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map); swat_capture_snapshot(&sim,1,&snapshot);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot); assert(size && swat_decode_snapshot(&decoded,bytes,size)); assert(swat_apply_snapshot(&replica,&decoded));
    assert(replica.devices[0].active && replica.devices[0].kind==SWAT_BALL && B3_IS_NULL(replica.devices[0].body));
    assert(replica.actors[0].gear.devices[SWAT_BALL]==0 && replica.actors[1].gear.surrendered);
    SwatCommand command={1,1,in},received; size=swat_encode_command(bytes,sizeof(bytes),&command);
    assert(size && swat_decode_command(&received,bytes,size) && received.input.device_control && received.input.command);
    for(size_t n=0;n<size;n++) assert(!swat_decode_command(&received,bytes,n));
    swat_sim_close(&replica); swat_sim_close(&sim); puts("PASS communication device: occluded negotiation, ordinary delayed sound and portable devices/inventory/remote-input replication");
}
int main(void) { deploy_recover(); remote_motion(); communication_and_replication(); return 0; }
