#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void static_obstacles(const SwatMap* source) {
    static SwatMap candidate,decoded,untouched;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    candidate=*source;SwatMapObject* o=&candidate.objects[1];
    o->part=SWAT_PART_OBSTACLE;o->max_health=0;o->door=o->latch_broken=o->fractured=false;
    o->structural_thickness=0;o->support_role=SWAT_SUPPORT_NONE;o->wall_group=0;
    for(int i=0;i<SWAT_MAX_SUPPORTS;i++)o->supports[i]=-1;
    size_t size=swat_encode_map(bytes,sizeof(bytes),&candidate);assert(size && swat_decode_map(&decoded,bytes,size));
    assert(decoded.objects[1].part==SWAT_PART_OBSTACLE);untouched=decoded;
    SwatMapObject valid=*o;
    for(int fault=0;fault<6;fault++) {
        *o=valid;
        if(fault==0)o->max_health=1;
        if(fault==1)o->supports[0]=0;
        if(fault==2)o->structural_thickness=.01f;
        if(fault==3)o->wall_group=1;
        if(fault==4)o->door=true;
        if(fault==5)o->fractured=true;
        size=swat_encode_map(bytes,sizeof(bytes),&candidate);assert(size);
        assert(!swat_decode_map(&decoded,bytes,size) && !memcmp(&decoded,&untouched,sizeof(decoded)));
    }
    puts("PASS static obstacle maps: permanent props roundtrip; breakable, supported, thin, grouped, door and fractured impostors are rejected without mutation");
}
static void directed_doors(void) {
    static SwatSim authority,replica;static SwatMap map,decoded_map;static SwatSnapshot state,decoded,before,after,invalid,untouched;
    static unsigned char bytes[SWAT_NET_PACKET_MAX],original[SWAT_NET_PACKET_MAX];
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_HOUSE;cfg.randomize=false;cfg.hostile_fire=false;swat_config_human(&cfg);
    swat_sim_init(&authority,cfg,42);int door=-1,wall=-1;
    for(int i=1;i<authority.world.count;i++)if(authority.world.objects[i].door)door=i;else if(wall<0)wall=i;
    assert(door>0 && wall>0);
    authority.actors[3].mind.order=SWAT_ORDER_PICK;authority.actors[3].mind.order_door=door;
    authority.actors[3].mind.pending_order=SWAT_ORDER_WEDGE;authority.actors[3].mind.pending_door=door;authority.actors[3].mind.queued=true;
    swat_capture_map(&authority,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded_map,bytes,n));swat_apply_map(&replica,&decoded_map);
    swat_capture_snapshot(&authority,1,&state);n=swat_encode_snapshot(bytes,sizeof(bytes),&state);assert(n && swat_decode_snapshot(&decoded,bytes,n));
    assert(decoded.actors[3].mind.order_door==door && decoded.actors[3].mind.pending_door==door && swat_apply_snapshot(&replica,&decoded));
    swat_capture_snapshot(&replica,1,&before);size_t original_size=swat_encode_snapshot(original,sizeof(original),&before);assert(original_size);
    for(int field=0;field<2;field++)for(int fault=0;fault<4;fault++) {
        invalid=state;int value=fault==0?wall:fault==1?authority.world.count:fault==2?-1:SWAT_MAX_OBJECTS;
        if(field)invalid.actors[3].mind.pending_door=value;else invalid.actors[3].mind.order_door=value;
        invalid.objects[door].locked=!state.objects[door].locked;invalid.tick+=20;
        n=swat_encode_snapshot(bytes,sizeof(bytes),&invalid);assert(n);
        if(fault>=2) {
            untouched=decoded;
            assert(!swat_decode_snapshot(&decoded,bytes,n) && !memcmp(&decoded,&untouched,sizeof(decoded)));
        } else {assert(swat_decode_snapshot(&decoded,bytes,n));assert(!swat_apply_snapshot(&replica,&decoded));}
        swat_capture_snapshot(&replica,1,&after);size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&after);
        assert(size==original_size && !memcmp(bytes,original,size));
    }
    swat_sim_close(&replica);swat_sim_close(&authority);
    puts("PASS directed door state: active/queued identities survive encoded map/snapshot rebuild; invalid, absent and non-door references reject atomically");
}
int main(void) {
    directed_doors();
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
    static_obstacles(&map);
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
    state.actors[3].mind.order_actor=2;state.actors[3].mind.pending_actor=3;
    length=swat_encode_snapshot(bytes,sizeof(bytes),&state); assert(length);
    assert(swat_decode_snapshot(&roundtrip,bytes,length));
    for(size_t i=0;i<length;i++) assert(!swat_decode_snapshot(&roundtrip,bytes,i));
    assert(swat_decode_snapshot(&roundtrip,bytes,length)); assert(swat_apply_snapshot(&replica,&roundtrip));
    assert(replica.tick==server.tick && replica.actor_count==4 && !replica.world.objects[8].active);
    assert(replica.actors[3].controller.body.crouched && replica.actors[3].controller.lean>.9f);
    assert(replica.actors[3].mind.order_actor==2 && replica.actors[3].mind.pending_actor==3);
    // Reject an out-of-range directed target without replacing decoded state.
    for(int pending=0;pending<2;pending++) {
        static SwatSnapshot untouched;untouched=roundtrip;
        int* target=pending?&state.actors[3].mind.pending_actor:&state.actors[3].mind.order_actor;
        int saved=*target;*target=SWAT_MAX_ACTORS+1;
        size_t malformed=swat_encode_snapshot(bad,sizeof(bad),&state);assert(malformed);
        assert(!swat_decode_snapshot(&roundtrip,bad,malformed) && !memcmp(&roundtrip,&untouched,sizeof(roundtrip)));
        *target=saved;
    }
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
