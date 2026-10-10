#include "protocol.h"
#include "replay.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot state;
static unsigned char packet[SWAT_NET_PACKET_MAX];
static SwatReplay journal;
static void synchronize(void){
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(packet,sizeof(packet),&map);
    assert(n && swat_decode_map(&decoded,packet,n));swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&state);n=swat_encode_snapshot(packet,sizeof(packet),&state);
    assert(n && swat_decode_snapshot(&state,packet,n) && swat_apply_snapshot(&replica,&state));
}
static void step(SwatInput in){swat_sim_step(&sim,&in);assert(swat_replay_append(&journal,&in,&sim));}
static SwatInput look(b3Pos target){
    SwatController* c=&sim.actors[0].controller;b3Vec3 d=b3SubPos(target,swat_controller_eye(c));SwatInput in=swat_neutral_input();
    in.yaw_delta=swat_clamp(swat_angle(atan2f(d.z,d.x)-c->yaw),-.1f,.1f);
    in.pitch_delta=swat_clamp(atan2f(d.y,hypotf(d.x,d.z))-c->pitch,-.1f,.1f);return in;
}
static void walk(b3Pos target){
    b3Pos waypoint=target;int replan=0,t=0;
    for(;t<1200;t++){
        b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);b3Vec3 d=b3SubPos(target,feet);if(hypotf(d.x,d.z)<.43f)break;
        SwatInput yield=swat_neutral_input();if(swat_navigation_yield_door(&sim,0,&yield)){step(yield);replan=0;continue;}
        if(t>=replan || b3Distance(feet,waypoint)<.3f){if(!swat_navigation_next_for_actor(&sim,0,target,&waypoint)){step(swat_neutral_input());continue;}replan=t+30;}
        SwatInput in=look(b3OffsetPos(waypoint,swat_v(0,1.65f,0)));if(fabsf(in.yaw_delta)<.07f)in.forward=.8f;
        SwatHit hit=swat_context_hit(&sim,0,1.7f);
        if(hit.kind==SWAT_HIT_WORLD && hit.index>=0 && sim.world.objects[hit.index].door){
            SwatObject* door=&sim.world.objects[hit.index];if(door->locked){in.forward=0;in.door_tool=SWAT_LOCKPICK;}
            else if(!door->door_open){in.forward=0;in.interact=!sim.actors[0].last_interact;}
        }
        step(in);
    }
    if(t==1200){b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);fprintf(stderr,"walk %.3f %.3f stuck %.3f %.3f\n",(float)target.x,(float)target.z,(float)feet.x,(float)feet.z);}assert(t<1200);
}
int main(int argc,char** argv){
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.squad_bots=0;cfg.max_ticks=12000;
    swat_sim_init(&sim,cfg,81);int owner=SWAT_MOTEL_NIGHTSTAND_FIRST;SwatObject* o=&sim.world.objects[owner];
    SwatMotelInstance p;assert(swat_motel_prop(&sim.world,owner,&p) && p.asset==49 && sim.world.count==1190);
    const SwatMotelAsset* source=swat_motel_asset(p.asset);assert(source->vertex_count==503 && source->triangle_count==470);
    assert(fabsf(2*o->half.x-.504564583f)<1e-6f && fabsf(2*o->half.y-.615520971f)<1e-6f && fabsf(2*o->half.z-.508716002f)<1e-6f);
    assert(o->active && o->material==SWAT_WOOD && o->part==SWAT_PART_FIXTURE && o->structural_thickness==.024f);
    assert(o->max_health>90 && o->max_health<100 && o->supports[0]==81);
    for(int i=1;i<SWAT_MAX_SUPPORTS;i++)assert(o->supports[i]==-1);
    const b3Vec3 feet[]={{-.208788887f,0,-.211462855f},{.211048245f,0,-.211462855f},{-.208788916f,0,.201241568f},{.211048290f,0,.201241568f}};
    for(int i=0;i<4;i++){
        SwatHit h=swat_world_ray(&sim.world,b3OffsetPos(p.origin,b3Add(feet[i],swat_v(0,.001f,0))),swat_v(0,-1,0),.002f,o->body);
        assert(h.hit && h.index==81 && fabsf((float)h.point.y-p.origin.y)<1e-6f);
    }
    // Query individual source geometry, avoiding the bed or wall behind it.
    SwatHit h=swat_world_ray(&sim.world,b3OffsetPos(p.origin,swat_v(0,.075f,.30f)),swat_v(0,0,-1),.60f,b3_nullBodyId);assert(!h.hit);
    h=swat_world_ray(&sim.world,b3OffsetPos(p.origin,swat_v(0,.70f,0)),swat_v(0,-1,0),.15f,b3_nullBodyId);assert(h.hit && h.index==owner);
    float thickness=swat_world_exit_distance(o,h.point,swat_v(0,-1,0));assert(isfinite(thickness) && thickness>=0 && thickness<.62f);
    printf("PASS nightstand geometry: 470 original triangles, 503 transformed vertices, four carpet contacts, open legs, tabletop hit; measured exit %.6fm (open meshes stop conservatively)\n",thickness);
    synchronize();assert(replica.world.objects[owner].query_mesh.data->triangleCount==470);
    assert(!swat_world_damage(&sim.world,owner,1) && o->health<o->max_health);synchronize();assert(replica.world.objects[owner].health==o->health);
    // Before the appended prop existed, the ten independent panes still bind.
    map.count=SWAT_MOTEL_NIGHTSTAND_FIRST;swat_apply_map(&replica,&map);
    assert(replica.world.motel && replica.world.count==1189 && B3_IS_NON_NULL(replica.world.objects[SWAT_MOTEL_PANES_FIRST].body));
    synchronize();map.objects[owner].center.x+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();state.objects[81].active=false;assert(!swat_apply_snapshot(&replica,&state) && replica.world.objects[81].active);
    synchronize();assert(swat_world_damage(&sim.world,owner,10000) && !o->active && B3_IS_NULL(o->body));synchronize();
    assert(!replica.world.objects[owner].active && B3_IS_NULL(replica.world.objects[owner].body));swat_sim_close(&replica);swat_sim_reset(&sim);
    puts("PASS nightstand replication: encoded map/snapshot, partial damage/removal, legacy pane prefix, malformed placement and unsupported state rejection");
    const char* path=argc>1?argv[1]:"nightstand-test.sgrp";assert(swat_replay_record(&journal,path,&sim));
    walk((b3Pos){9,-.079f,8});walk((b3Pos){9,-.079f,.9f});walk((b3Pos){4.8f,-.079f,.8f});
    // Canonical room 104 door: open it with the same interaction as the player.
    b3Pos door={4.8f,1.05f,0};for(int t=0;t<45;t++)step(look(door));SwatInput in=look(door);in.interact=true;step(in);
    walk((b3Pos){4.8f,.004f,-1});
    b3Pos target=b3OffsetPos(p.origin,swat_v(0,.48f,0));for(int t=0;t<45;t++){in=look(target);in.aim=true;step(in);}
    int t=0;for(;t<1800 && sim.world.objects[owner].active;t++){
        in=look(target);in.aim=true;in.fire=t%12==0;SwatWeapon* gun=&sim.actors[0].arsenal.slots[0];in.reload=!gun->magazine&&!gun->chambered;step(in);
    }
    assert(t<1800 && swat_replay_close(&journal));uint32_t digest=swat_replay_digest(&sim);char error[256];
    assert(swat_replay_restore(&replica,path,error,sizeof(error)) && swat_replay_digest(&replica)==digest && !replica.world.objects[owner].active);
    swat_sim_close(&replica);swat_sim_close(&sim);remove(path);
    puts("PASS nightstand gameplay: canonical spawn, room entry, ordinary aim/fire/reload, destruction and exact replay/save restore");
}
