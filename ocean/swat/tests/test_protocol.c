#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static unsigned char bytes[SWAT_NET_PACKET_MAX],bad[SWAT_NET_PACKET_MAX];
    SwatCommand command={4,99,swat_neutral_input()};
    command.input.forward=1; command.input.yaw_delta=.2f; command.input.fire=true; command.input.weapon=2;
    command.input.inspect=command.input.command=command.input.melee=true; command.input.loadout=3;
    command.input.ready=SWAT_HIGH_READY; command.input.cancel_reload=true;
    command.input.primary_profile=6; command.input.sight_profile=3; command.input.magazine_inventory=true;
    size_t length=swat_encode_command(bytes,sizeof(bytes),&command);
    assert(length && swat_message_type(bytes,length)==SWAT_MSG_INPUT);
    SwatCommand decoded={0}; assert(swat_decode_command(&decoded,bytes,length));
    assert(decoded.epoch==4 && decoded.sequence==99 && decoded.input.forward==1 && decoded.input.fire && decoded.input.weapon==2);
    assert(decoded.input.inspect && decoded.input.command && decoded.input.melee && decoded.input.loadout==3);
    assert(decoded.input.ready==SWAT_HIGH_READY && decoded.input.cancel_reload && decoded.input.primary_profile==6 &&
        decoded.input.sight_profile==3 && decoded.input.magazine_inventory);
    for(size_t i=0;i<length;i++) {
        SwatCommand untouched=decoded;
        assert(!swat_decode_command(&decoded,bytes,i)); assert(!memcmp(&decoded,&untouched,sizeof(decoded)));
    }
    memcpy(bad,bytes,length); bad[16]=0x7f; bad[17]=0xc0; bad[18]=bad[19]=0; // NaN
    assert(!swat_decode_command(&decoded,bad,length));
    bytes[length]=0; assert(!swat_decode_command(&decoded,bytes,length+1));

    SwatConfig config=swat_default_config(); config.randomize=false; config.hostile_fire=false;
    static SwatSim server,replica; swat_sim_init(&server,config,42); memset(&replica,0,sizeof(replica));
    assert(swat_sim_set_player(&server,1,true));
    static SwatMap map,copy; swat_capture_map(&server,3,&map);
    length=swat_encode_map(bytes,sizeof(bytes),&map); assert(length && swat_decode_map(&copy,bytes,length));
    assert(copy.count==server.world.count && copy.config.max_ticks==config.max_ticks);
    for(size_t i=0;i<length;i++) assert(!swat_decode_map(&copy,bytes,i));
    assert(swat_decode_map(&copy,bytes,length)); swat_apply_map(&replica,&copy);
    SwatInput inputs[SWAT_MAX_ACTORS];
    for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[3].crouch=true; inputs[3].lean=1; inputs[3].strafe=1;
    for(int t=0;t<24;t++) swat_sim_step_inputs(&server,inputs);
    assert(swat_world_damage(&server.world,8,1000));
    static SwatSnapshot state,roundtrip; swat_capture_snapshot(&server,3,&state); state.player_mask=3;
    length=swat_encode_snapshot(bytes,sizeof(bytes),&state); assert(length);
    assert(swat_decode_snapshot(&roundtrip,bytes,length));
    for(size_t i=0;i<length;i++) assert(!swat_decode_snapshot(&roundtrip,bytes,i));
    assert(swat_decode_snapshot(&roundtrip,bytes,length)); assert(swat_apply_snapshot(&replica,&roundtrip));
    assert(replica.tick==server.tick && replica.actor_count==4 && !replica.world.objects[8].active);
    assert(replica.actors[3].controller.body.crouched && replica.actors[3].controller.lean>.9f);
    assert(b3Distance(swat_controller_eye(&replica.actors[3].controller),swat_controller_eye(&server.actors[3].controller))<1e-5f);
    b3Capsule cap=b3Shape_GetCapsule(replica.actors[3].controller.body.capsuleId);
    assert(cap.center2.z>.39f);
    // State application validates the whole update before removing more cover.
    roundtrip.objects[9].active=false; roundtrip.objects[0].health=10;
    assert(!swat_apply_snapshot(&replica,&roundtrip) && replica.world.objects[9].active);
    swat_sim_close(&replica); swat_sim_close(&server);
    state.object_count=SWAT_MAX_OBJECTS;
    for(int i=0;i<state.object_count;i++) {
        SwatObjectState* o=&state.objects[i];*o=(SwatObjectState){.active=true,.health=50,.breach_owner=-1,.wedge_owner=-1,.cut_count=SWAT_MAX_CUTS};
        for(int c=0;c<SWAT_MAX_CUTS;c++)o->cuts[c]=(SwatCut){.height=.02f,.width=.02f,.seed=0x4a4a4a4au,
            .kind=c<SWAT_IMPACT_PATCHES?(SwatCutKind)(SWAT_CUT_PATCH+c):SWAT_CUT_BREACH};
        o->door=(SwatDoorState){.broken_hinges=7,.position={0,1,0},.rotation=b3Quat_identity};
    }
    state.rubble_count=SWAT_RUBBLE_CAPACITY;state.rubble_serial=SWAT_RUBBLE_CAPACITY;
    for(int i=0;i<SWAT_RUBBLE_CAPACITY;i++) {
        SwatRubbleState* r=&state.rubble[i];*r=(SwatRubbleState){.active=true,.serial=(uint32_t)i+1,.source=1,.health=1,.section_count=SWAT_RUBBLE_SECTIONS,.rotation=b3Quat_identity};
        for(int s=0;s<SWAT_RUBBLE_SECTIONS;s++) {
            SwatFracturePolygon* p=&r->sections[s];p->count=SWAT_CUT_VERTICES;p->exposed_edges=UINT32_MAX;
            for(int k=0;k<p->count;k++){float a=2*SWAT_PI*k/p->count;p->vertices[k][0]=cosf(a)*.1f;p->vertices[k][1]=sinf(a)*.1f;}
        }
    }
    length=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(length && length<sizeof(bytes) && swat_decode_snapshot(&roundtrip,bytes,length));
    assert(roundtrip.object_count==SWAT_MAX_OBJECTS && roundtrip.objects[SWAT_MAX_OBJECTS-1].cut_count==SWAT_MAX_CUTS);
    assert(!swat_encode_snapshot(bytes,length-1,&state));
    printf("PASS maximum damage packet: %d objects with %d profiles and loose-door poses each plus %d debris bodies with %d polygons of %d vertices, %zu / %zu bytes\n",SWAT_MAX_OBJECTS,SWAT_MAX_CUTS,SWAT_RUBBLE_CAPACITY,SWAT_RUBBLE_SECTIONS,SWAT_CUT_VERTICES,length,sizeof(bytes));
    puts("PASS protocol: explicit portable encoding, all truncations, NaN/trailing bytes, no partial mutation, exact replica pose/lean/crouch and destruction");
    return 0;
}
