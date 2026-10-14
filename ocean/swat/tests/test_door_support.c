#include "protocol.h"
#include "replay.h"
#include "door_damage.h"
#include "fracture_geometry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim server,replica,late;
static SwatMap map;
static SwatSnapshot state,decoded,bad,before,after;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static int contact_overflows;
static SwatReplay journal;
static void physics_log(const char* message) {
    if(strstr(message,"triangle buffer capacity")){contact_overflows++;fprintf(stderr,"Contact overflow at tick %d: %s\n",server.tick,message);}
}

static b3Pos point(const SwatObject* o,b3Vec3 local) {
    return b3OffsetPos(o->center,b3RotateVector(swat_object_rotation(o),local));
}
static void sync_replica(bool fresh) {
    if(fresh){swat_capture_map(&server,1,&map);swat_apply_map(&replica,&map);}
    swat_capture_snapshot(&server,1,&state);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n));
    assert(swat_apply_snapshot(&replica,&decoded));
    if(swat_replay_digest(&server)!=swat_replay_digest(&replica)) {
        swat_capture_snapshot(&replica,1,&after);
        for(int i=0;i<state.object_count;i++)if(memcmp(&state.objects[i],&after.objects[i],sizeof(state.objects[i]))) {
            const SwatDoorState* a=&state.objects[i].door;const SwatDoorState* b=&after.objects[i].door;
            fprintf(stderr,"Replica differs at owner%d tick%d fresh%d: mask%u/%u latched%d/%d awake%d/%d\n",i,server.tick,fresh,a->broken_hinges,b->broken_hinges,a->latched,b->latched,a->awake,b->awake);break;
        }
        assert(false);
    }
}
static void rays(int owner) {
    const SwatObject* o=&server.world.objects[owner];uint32_t rng=8192;
    b3Vec3 direction=b3RotateVector(swat_object_rotation(o),swat_v(.6f,0,0));
    for(int i=0;i<96;i++) {
        float y=o->half.y*(2*swat_rand01(&rng)-1),z=o->half.z*(2*swat_rand01(&rng)-1);
        b3Pos from=point(o,swat_v(-.3f,y,z));
        bool solid=swat_world_object_ray(o,from,direction);
        assert(solid==swat_fracture_contains(o,y,z));
        assert(solid==swat_world_object_ray(&replica.world.objects[owner],from,direction));
    }
}
static void reject(const SwatSnapshot* invalid) {
    swat_capture_snapshot(&replica,1,&before);
    assert(!swat_apply_snapshot(&replica,invalid));
    swat_capture_snapshot(&replica,1,&after);assert(!memcmp(&before,&after,sizeof(before)));
}
static void sever_hinge(int owner,int hinge) {
    SwatObject* o=&server.world.objects[owner];b3Vec3 anchor=swat_door_hinge(o,hinge);
    for(int shot=0;shot<8 && (!o->motion || !(o->motion->state.broken_hinges&(1u<<hinge)));shot++) {
        int sample=shot%4;
        b3Vec3 local=swat_v(-.3f,anchor.y+(sample&1?1:-1)*.035f*(o->half.y/1.095f),
            anchor.z+(sample&2?1:-1)*.008f*(o->half.z/.54f));
        b3Vec3 direction=b3RotateVector(swat_object_rotation(o),swat_v(1,0,0));
        swat_sim_shoot(&server,0,point(o,local),direction,(SwatShot){.fired=true,.damage=34,.energy=60,.range=2});
    }
    assert(o->motion && (o->motion->state.broken_hinges&(1u<<hinge)));
}
static int fixture(int mission) {
    SwatConfig cfg=swat_default_config();cfg.mission=mission;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=30000;
    swat_sim_init(&server,cfg,81);
    for(int i=1;i<server.actor_count;i++)if(server.actors[i].present) {
        b3DestroyBody(server.actors[i].controller.body.body);memset(&server.actors[i],0,sizeof(server.actors[i]));
    }
    server.extraction=(b3Pos){500,0,500};
    int owner=-1;for(int i=0;i<server.world.count;i++)if(server.world.objects[i].door){owner=i;break;}
    if(mission==SWAT_MOTEL)owner=16;
    assert(owner>=0);server.actors[0].arsenal.primary=0;
    return owner;
}
static void step(int count,bool sync,int owner) {
    SwatInput neutral=swat_neutral_input();
    for(int t=0;t<count;t++) {
        swat_sim_step(&server,&neutral);
        if(sync){sync_replica(false);if(!(t%15))rays(owner);}
    }
}
static void hanging_and_falling(int mission) {
    int owner=fixture(mission);SwatObject* o=&server.world.objects[owner];
    b3BodyId body=o->body;b3Pos standing=o->center;
    assert(swat_world_break_latch(&server.world,owner) && !o->door_open);
    b3Vec3 opening_normal=b3RotateVector(swat_object_rotation(o),swat_v(1,0,0));
    SwatSoundEvent sound={.kind=SWAT_SOUND_SHOT,.position=b3OffsetPos(standing,swat_mul(opening_normal,-.8f)),.strength=2.6f,.range=10};
    b3Pos listener=b3OffsetPos(standing,swat_mul(opening_normal,.8f));
    float closed_sound=swat_acoustic_path(&server.world,&sound,listener).gain;
    o->breach_owner=0;o->breach_position=point(o,swat_v(-o->half.x,.35f,.15f));
    sever_hinge(owner,0);assert(o->motion->state.broken_hinges==1 && !swat_door_dynamic(o));
    step(45,false,owner);assert(b3Distance(o->center,standing)<1e-6f && !o->door_open);
    sever_hinge(owner,1);assert(o->motion->state.broken_hinges==3 && !swat_door_loose(o));
    assert(swat_door_dynamic(o) && B3_IS_NON_NULL(o->motion->joint));
    sync_replica(true);rays(owner);
    b3Vec3 mount=b3InvRotateVector(swat_object_rotation(o),b3SubPos(o->breach_position,o->center));
    step(150,true,owner);
    fprintf(stderr,"Hanging mission%d: support%d mask%u standY%.5f now %.5f,%.5f,%.5f q%.5f,%.5f,%.5f,%.5f velocity%.5f,%.5f,%.5f\n",mission,o->motion->support,o->motion->state.broken_hinges,(float)standing.y,(float)o->center.x,(float)o->center.y,(float)o->center.z,o->motion->state.rotation.v.x,o->motion->state.rotation.v.y,o->motion->state.rotation.v.z,o->motion->state.rotation.s,o->motion->state.velocity.x,o->motion->state.velocity.y,o->motion->state.velocity.z);
    // The lab has under-leaf clearance; the motel can rest on its real floor
    // and jamb instead of dropping. Both must retain the last physical joint.
    if(mission==SWAT_BREACH_LAB)assert(o->center.y<standing.y-.005f);
    b3Vec3 top=swat_door_hinge(o,2);
    b3Pos fixed=b3OffsetPos(o->hinge,b3RotateVector(swat_rotation(o->closed_yaw,0),b3Add(top,swat_v(0,0,o->half.z))));
    assert(b3Distance(point(o,top),fixed)<.03f);
    assert(b3Length(b3Sub(mount,b3InvRotateVector(swat_object_rotation(o),b3SubPos(o->breach_position,o->center))))<1e-4f);
    bad=state;bad.objects[owner].door.position.y+=.3f;reject(&bad);
    bad=state;bad.objects[owner].door.rotation.s=0;reject(&bad);
    bad=state;bad.objects[owner].door.rotation.v=swat_mul(bad.objects[owner].door.rotation.v,1.0001f);
    bad.objects[owner].door.rotation.s*=1.0001f;reject(&bad);
    size_t invalid_size=swat_encode_snapshot(bytes,sizeof(bytes),&bad);
    assert(invalid_size && !swat_decode_snapshot(&decoded,bytes,invalid_size));
    bad=state;bad.objects[owner].door.broken_hinges=1;reject(&bad);
    // Gun contacts use the achieved hanging pose, not the old standing box.
    sever_hinge(owner,2);assert(swat_door_loose(o) && B3_IS_NULL(o->motion->joint));
    assert(B3_ID_EQUALS(body,o->body) && o->active && !o->locked && !o->door_open);
    step(600,true,owner);
    b3Vec3 normal=b3RotateVector(swat_object_rotation(o),swat_v(1,0,0));
    fprintf(stderr,"Settled mission%d: centre %.5f,%.5f,%.5f normalY %.5f awake%d\n",mission,(float)o->center.x,(float)o->center.y,(float)o->center.z,normal.y,b3Body_IsAwake(o->body));
    assert(!b3Body_IsAwake(o->body));
    float open_sound=swat_acoustic_path(&server.world,&sound,listener).gain;
    assert(open_sound>closed_sound);
    if(mission==SWAT_BREACH_LAB)assert(fabsf(normal.y)>.95f && o->center.y<.2f);
    else assert(fabsf(normal.y)>.3f && o->center.y<standing.y-.1f); // Furniture can support a fallen motel leaf.
    assert(B3_ID_EQUALS(body,o->body) && b3Body_GetShapeCount(o->body)==o->cuts->section_count);
    sync_replica(true);rays(owner);
    swat_capture_map(&server,1,&map);swat_apply_map(&late,&map);assert(swat_apply_snapshot(&late,&decoded));
    assert(swat_replay_digest(&server)==swat_replay_digest(&late));
    if(mission==SWAT_MOTEL)assert(late.world.motel && late.world.motel_contact_mesh_count>0);
    bad=state;bad.objects[owner].door.latched=true;reject(&bad);
    printf("PASS door support mission%d: three independent ordinary-shot hinge failures, last-attachment constraint, unchanged owner/body, carried device, floor collision/sleep, achieved-pose rays/sound, live/late replicas and atomic malformed-state rejection\n",mission);
    swat_sim_close(&late);swat_sim_close(&replica);swat_sim_close(&server);
}
static void latch_support(void) {
    int owner=fixture(SWAT_BREACH_LAB);SwatObject* o=&server.world.objects[owner];o->locked=true;
    for(int h=0;h<SWAT_DOOR_HINGES;h++)sever_hinge(owner,h);
    assert(o->motion->state.broken_hinges==SWAT_DOOR_HINGE_MASK && o->motion->state.latched && !swat_door_loose(o));
    assert(o->motion->support==SWAT_DOOR_LATCH && o->locked && !o->door_open);
    sync_replica(true);step(90,true,owner);
    assert(o->active && o->locked && B3_IS_NON_NULL(o->motion->joint));
    assert(swat_world_break_latch(&server.world,owner));
    assert(swat_door_loose(o) && !o->door_open && !o->locked && B3_IS_NULL(o->motion->joint));
    step(600,true,owner);
    assert(!b3Body_IsAwake(o->body));
    puts("PASS remaining latch support: all hinges can fail while the locked latch holds; releasing that final attachment frees the leaf without an open command");
    swat_sim_close(&replica);swat_sim_close(&server);
}
static void recorded_step(SwatInput in) {
    swat_sim_step(&server,&in);assert(server.end==SWAT_RUNNING);
    assert(swat_replay_append(&journal,&in,&server));
}
static void recorded_idle(int count) {for(int t=0;t<count;t++)recorded_step(swat_neutral_input());}
static SwatInput recorded_look(b3Pos target) {
    SwatController* c=&server.actors[0].controller;SwatInput in=swat_neutral_input();
    b3Vec3 d=b3SubPos(target,swat_controller_eye(c));
    in.yaw_delta=swat_clamp(swat_angle(atan2f(d.z,d.x)-c->yaw-c->recoil_yaw),-6*SWAT_RAD,6*SWAT_RAD);
    in.pitch_delta=swat_clamp(atan2f(d.y,hypotf(d.x,d.z))-c->pitch-c->recoil_pitch,-6*SWAT_RAD,6*SWAT_RAD);
    return in;
}
static void recorded_walk(b3Pos goal) {
    for(int t=0;t<900;t++) {
        SwatController* c=&server.actors[0].controller;b3Pos p=swat_body_feet_position(&c->body);b3Vec3 d=b3SubPos(goal,p);
        if(hypotf(d.x,d.z)<.07f){recorded_idle(15);return;}
        SwatInput in=swat_neutral_input();float cy=cosf(c->yaw),sy=sinf(c->yaw);
        in.forward=swat_clamp((cy*d.x+sy*d.z)*1.5f,-.8f,.8f);in.strafe=swat_clamp((-sy*d.x+cy*d.z)*1.5f,-.8f,.8f);
        recorded_step(in);
    }
    assert(false);
}
static void ordinary_input_replay(const char* path) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_BREACH_LAB;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=30000;
    swat_sim_init(&server,cfg,81);assert(swat_replay_record(&journal,path,&server));
    int owner=-1;for(int i=0;i<server.world.count;i++)if(server.world.objects[i].door){owner=i;break;}assert(owner>=0);
    SwatObject* o=&server.world.objects[owner];
    recorded_walk((b3Pos){2.8f,0,-4.2f});
    b3Pos latch=point(o,swat_v(-o->half.x,.06f,o->half.z-.05f));
    for(int t=0;t<30;t++)recorded_step(recorded_look(latch));
    int stock=server.actors[0].gear.breaching_charges;
    for(int t=0;t<100 && o->breach_owner<0;t++){SwatInput in=recorded_look(latch);in.door_tool=SWAT_PLACE_CHARGE;recorded_step(in);}
    assert(o->breach_owner==0 && server.actors[0].gear.breaching_charges==stock-1);
    recorded_walk((b3Pos){.6f,0,-4.2f});
    SwatInput remote=swat_neutral_input();remote.door_tool=SWAT_DETONATE_CHARGE;recorded_step(remote);
    assert(o->latch_broken && !o->locked && !o->door_open && o->breach_owner<0);
    recorded_walk((b3Pos){2.8f,0,-4.2f});
    for(int h=0;h<SWAT_DOOR_HINGES;h++) {
        int first=server.actors[0].arsenal.shots;
        for(int t=0;t<1500 && (!o->motion || !(o->motion->state.broken_hinges&(1u<<h)));t++) {
            int sample=(server.actors[0].arsenal.shots-first)%4;b3Vec3 p=swat_door_hinge(o,h);
            p.x=-o->half.x;p.y+=(sample&1?1:-1)*.035f*(o->half.y/1.095f);p.z+=(sample&2?1:-1)*.008f*(o->half.z/.54f);
            SwatInput in=recorded_look(point(o,p));in.aim=true;
            SwatWeapon* weapon=&server.actors[0].arsenal.slots[0];
            in.fire=!server.actors[0].arsenal.last_fire && !weapon->reload_remaining;
            in.reload=!weapon->chambered && !weapon->reload_remaining;
            recorded_step(in);
        }
        if(!o->motion || !(o->motion->state.broken_hinges&(1u<<h))) {
            fprintf(stderr,"Input hinge %d did not fail: %d shots, mask%u, muzzle%d, health%.3f\n",h,server.actors[0].arsenal.shots,o->motion?o->motion->state.broken_hinges:0,server.actors[0].controller.muzzle_blocked,o->health);assert(false);
        }
    }
    assert(swat_door_loose(o));recorded_idle(6);assert(b3Body_IsAwake(o->body));
    char save[4096];assert(snprintf(save,sizeof(save),"%s.save",path)<(int)sizeof(save));
    int saved_tick=server.tick;assert(swat_replay_checkpoint(&journal,save));
    recorded_idle(600);assert(!b3Body_IsAwake(o->body));
    b3Pos crossing={5.3f,0,-4.2f},next;
    assert(swat_navigation_next_for_actor(&server,0,crossing,&next));
    int navigation_tick=server.navigation->built_tick;
    recorded_walk(crossing);
    assert(swat_body_feet_position(&server.actors[0].controller.body).x>5.2f && swat_door_loose(o) && !o->door_open);
    assert(swat_navigation_next_for_actor(&server,0,(b3Pos){2.8f,0,-4.2f},&next) && server.navigation->built_tick==navigation_tick);
    assert(swat_replay_close(&journal));
    uint32_t final=swat_replay_digest(&server);int frames=server.tick;
    SwatReplay reader;assert(swat_replay_open(&reader,path));swat_sim_init(&replica,reader.config,reader.seed);
    SwatInput in;uint32_t expected;int status;
    while((status=swat_replay_next(&reader,&in,&expected))==1) {
        swat_sim_step(&replica,&in);assert(swat_replay_digest(&replica)==expected);
    }
    assert(status==0 && swat_replay_close(&reader) && final==swat_replay_digest(&replica));
    char error[256];assert(swat_replay_restore(&late,save,error,sizeof(error)) && late.tick==saved_tick);
    assert(swat_replay_open(&reader,path));
    while((status=swat_replay_next(&reader,&in,&expected))==1)if((int)reader.frame>saved_tick) {
        swat_sim_step(&late,&in);assert(swat_replay_digest(&late)==expected);
    }
    assert(status==0 && swat_replay_close(&reader) && final==swat_replay_digest(&late));
    printf("PASS ordinary-input door failure: approach, finite charge/retreat/remote latch release, %d rifle shots on three hinges, hanging/fall/sleep, navigation and physical crossing over the leaf, %d exact replay ticks and in-flight save/resume\n",server.actors[0].arsenal.shots,frames);
    swat_sim_close(&late);swat_sim_close(&replica);swat_sim_close(&server);
}
int main(int argc,char** argv) {
    assert(argc<=2);
    b3SetLogFcn(physics_log);
    hanging_and_falling(SWAT_BREACH_LAB);hanging_and_falling(SWAT_MOTEL);latch_support();
    const char* path=argc==2?argv[1]:"door-support-test.sgrp";ordinary_input_replay(path);
    if(argc==1){char save[4096];snprintf(save,sizeof(save),"%s.save",path);remove(path);remove(save);}
    assert(!contact_overflows);
    return 0;
}
