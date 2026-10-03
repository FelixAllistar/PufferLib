#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char bytes[SWAT_NET_PACKET_MAX],bad[SWAT_NET_PACKET_MAX];
    SwatCommand command={4,99,swat_neutral_input()};
    command.input.forward=1; command.input.yaw_delta=.2f; command.input.fire=true; command.input.weapon=2;
    size_t length=swat_encode_command(bytes,sizeof(bytes),&command);
    assert(length && swat_message_type(bytes,length)==SWAT_MSG_INPUT);
    SwatCommand decoded={0}; assert(swat_decode_command(&decoded,bytes,length));
    assert(decoded.epoch==4 && decoded.sequence==99 && decoded.input.forward==1 && decoded.input.fire && decoded.input.weapon==2);
    for(size_t i=0;i<length;i++) {
        SwatCommand untouched=decoded;
        assert(!swat_decode_command(&decoded,bytes,i)); assert(!memcmp(&decoded,&untouched,sizeof(decoded)));
    }
    memcpy(bad,bytes,length); bad[16]=0x7f; bad[17]=0xc0; bad[18]=bad[19]=0; // NaN
    assert(!swat_decode_command(&decoded,bad,length));
    bytes[length]=0; assert(!swat_decode_command(&decoded,bytes,length+1));

    SwatConfig config=swat_default_config(); config.randomize=false; config.hostile_fire=false;
    SwatSim server,replica; swat_sim_init(&server,config,42); memset(&replica,0,sizeof(replica));
    assert(swat_sim_set_player(&server,1,true));
    SwatMap map,copy; swat_capture_map(&server,3,&map);
    length=swat_encode_map(bytes,sizeof(bytes),&map); assert(length && swat_decode_map(&copy,bytes,length));
    assert(copy.count==server.world.count && copy.config.max_ticks==config.max_ticks);
    for(size_t i=0;i<length;i++) assert(!swat_decode_map(&copy,bytes,i));
    assert(swat_decode_map(&copy,bytes,length)); swat_apply_map(&replica,&copy);
    SwatInput inputs[SWAT_MAX_ACTORS];
    for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    inputs[3].crouch=true; inputs[3].lean=1; inputs[3].strafe=1;
    for(int t=0;t<24;t++) swat_sim_step_inputs(&server,inputs);
    assert(swat_world_damage(&server.world,8,1000));
    SwatSnapshot state,roundtrip; swat_capture_snapshot(&server,3,&state); state.player_mask=3;
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
    puts("PASS protocol: explicit portable encoding, all truncations, NaN/trailing bytes, no partial mutation, exact replica pose/lean/crouch and destruction");
    return 0;
}
