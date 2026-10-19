#include "protocol.h"
#include "replay.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded_map;
static SwatSnapshot state,decoded_state;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static FILE* trace;
static void sync_state(bool fresh) {
    if(fresh){swat_sim_close(&replica);swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded_map,bytes,n));swat_apply_map(&replica,&decoded_map);assert(replica.world.motel);}
    swat_capture_snapshot(&sim,1,&state);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded_state,bytes,n) && swat_apply_snapshot(&replica,&decoded_state));
    assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));
    if(trace)assert(fprintf(trace,"%d %08x\n",sim.tick,swat_replay_digest(&sim))>0);
}
static b3Pos point(float x,float y,float z){return (b3Pos){10.8+x,-.08+y,-7.6+z};}
static SwatHit ray(float x,float y,float z,b3Vec3 d,float length){return swat_world_ray(&sim.world,point(x,y,z),d,length,b3_nullBodyId);}
static void place(b3Pos feet,float yaw) {
    SwatController* c=&sim.actors[0].controller;b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);c->yaw=yaw;c->pitch=0;
}
static void route(float from_x,float to_x,float z) {
    b3Pos goal=point(to_x,0,z),waypoint=goal;place(point(from_x,0,z),from_x<to_x?0:SWAT_PI);int tick;
    for(tick=0;tick<1800;tick++) {
        SwatController* c=&sim.actors[0].controller;b3Pos feet=swat_body_feet_position(&c->body);
        if(hypotf(feet.x-goal.x,feet.z-goal.z)<.35f && fabsf(feet.y-goal.y)<.3f)break;
        if(tick%15==0 || b3Distance(feet,waypoint)<.3f)assert(swat_navigation_next_for_actor(&sim,0,goal,&waypoint));
        b3Vec3 d=b3SubPos(waypoint,feet);float angle=swat_angle(b3Atan2(d.z,d.x)-c->yaw);
        SwatInput in=swat_neutral_input();in.yaw_delta=swat_clamp(angle,-6*SWAT_RAD,6*SWAT_RAD);in.forward=fabsf(angle)<.35f?1:0;in.crouch=swat_navigation_crouch(&sim,feet)||swat_navigation_crouch(&sim,waypoint);
        swat_sim_step(&sim,&in);assert(sim.actors[0].alive);if(tick%30==0)sync_state(false);
    }
    if(tick==1800){b3Pos at=swat_body_feet_position(&sim.actors[0].controller.body);fprintf(stderr,"Transformer route %.3f %.3f stopped %.3f %.3f waypoint %.3f %.3f\n",from_x,to_x,at.x,at.z,waypoint.x,waypoint.z);}
    assert(tick<1800);printf("PASS transformer route %.1f to %.1f at z%.1f in %d ticks\n",from_x,to_x,z,tick);
}
static void captured_corner_exit(void) {
    // Recorded from the original route failure. Preserve the roof-edge contact
    // even though waypoint-aware steering now avoids overshooting onto it.
    b3Pos start={9.8245f,.115f,-7.4734f},goal={9,-.08f,-7.6f},waypoint;
    place(start,0);SwatController* c=&sim.actors[0].controller;
    assert(!swat_body_clear_at(&c->body,start));
    assert(swat_navigation_next_for_actor(&sim,0,goal,&waypoint));int tick;
    for(tick=0;tick<1800;tick++) {
        b3Pos feet=swat_body_feet_position(&c->body);
        if(hypotf(feet.x-goal.x,feet.z-goal.z)<.35f && fabsf(feet.y-goal.y)<.3f)break;
        assert(swat_navigation_next_for_actor(&sim,0,goal,&waypoint));
        b3Vec3 d=b3SubPos(waypoint,feet);float angle=swat_angle(b3Atan2(d.z,d.x)-c->yaw);
        SwatInput in=swat_neutral_input();in.yaw_delta=swat_clamp(angle,-6*SWAT_RAD,6*SWAT_RAD);
        in.forward=fabsf(angle)<.35f?.8f:0;
        in.crouch=swat_navigation_crouch(&sim,feet)||swat_navigation_crouch(&sim,waypoint);
        swat_sim_step(&sim,&in);assert(sim.actors[0].alive && sim.actors[0].health==100);
        if(tick%30==0)sync_state(false);
    }
    assert(tick<1800);sync_state(false);
    printf("PASS transformer captured corner: ordinary inputs leave the roof edge and reach the real goal in %d ticks with exact replicas\n",tick);
}
int main(int argc,char** argv) {
    setvbuf(stdout,NULL,_IONBF,0);assert(argc<=2);if(argc==2){trace=fopen(argv[1],"w");assert(trace);}
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=10000;swat_config_human(&cfg);
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);int first=SWAT_MOTEL_TRANSFORMER_FIRST;sync_state(true);
    const int triangles[]={108,2824,688,1164},assets[]={66,63,64,65};int total=0;
    for(int i=0;i<4;i++) {
        SwatMotelInstance p;assert(swat_motel_prop(&sim.world,first+i,&p) && p.asset==assets[i]);
        const SwatObject* o=&sim.world.objects[first+i];assert(o->active && !o->door && o->part==SWAT_PART_FIXTURE);
        assert(o->material==(i?SWAT_STEEL:SWAT_CONCRETE));assert(o->supports[0]==(i?first+(i>1):0) && o->supports[1]==-1);
        assert(o->query_mesh.data && o->query_mesh.data->triangleCount==triangles[i]);total+=triangles[i];
        int count=b3Body_GetShapeCount(o->body);b3ShapeId shapes[32];assert(count>0 && count<=32);
        assert(b3Body_GetShapes(o->body,shapes,32)==count);int selected=0;
        for(int j=0;j<count;j++){b3Mesh mesh=b3Shape_GetMesh(shapes[j]);assert(mesh.data && mesh.data->triangleCount<=240);selected+=mesh.data->triangleCount;}
        assert(selected==triangles[i]);
        SwatMotelInstance roadside;assert(!swat_motel_roadside(&sim.world,first+i,&roadside));
    }
    assert(total==4784);puts("PASS transformer ownership: all 4784 source triangles, four bounded material owners, no operational doors and exact encoded replicas");
    for(int x=-1;x<=1;x+=2)for(int z=-1;z<=1;z+=2) {
        // Cast from outside the source solid onto its exposed top corner.
        SwatHit hit=ray(x*.77f,.30f,z*.67f,swat_v(0,-1,0),.4f);
        assert(hit.hit && hit.index==first && fabs(hit.point.y-.10)<1e-4);
        assert(swat_world_object_ray(&sim.world.objects[0],point(x*.787f,.02f,z*.687f),swat_v(0,-.04f,0)));
    }
    SwatHit left=ray(-.3f,.8f,1,swat_v(0,0,-1),.6f),right=ray(.3f,.8f,1,swat_v(0,0,-1),.6f);
    assert(left.hit && left.index==first+2 && right.hit && right.index==first+3);
    SwatHit rear=ray(0,.8f,-1,swat_v(0,0,1),.6f);assert(rear.hit && rear.index==first+1);
    assert(!ray(0,1.7f,1,swat_v(0,0,-1),2).hit);
    puts("PASS transformer contacts: actual plinth feet rest on the foundation, front panels and rear shell hit their authored surfaces, space above stays open");
    place(point(0,0,2),-SWAT_PI*.5f);SwatInput forward=swat_neutral_input();forward.forward=1;
    for(int t=0;t<100;t++){swat_sim_step(&sim,&forward);assert(sim.actors[0].alive);if(t%30==0)sync_state(false);}
    b3Pos at=swat_body_feet_position(&sim.actors[0].controller.body);
    // The low plinth can also stop an attempted step into the tall cabinet.
    // Bound the actual source front envelope plus the controller's hull width.
    float hull=sim.actors[0].controller.body.bodyRadius*.5f;
    float capsule=b3Shape_GetCapsule(sim.actors[0].controller.body.capsuleId).radius;
    assert(at.z>point(0,0,.56f+hull-.04f).z && at.z<point(0,0,.70f+fmaxf(hull,capsule)+.04f).z);
    printf("TRANSFORMER_STOP %.9f %.9f %.9f hull%.6f capsule%.6f\n",at.x,at.y,at.z,hull,capsule);
    puts("PASS transformer capsule: normal forward motion stops at the closed front without entering the cabinet");
    captured_corner_exit();route(-1.8f,1.8f,0);route(1.8f,-1.8f,0);route(-1.8f,1.8f,-1.4f);
    assert(swat_world_damage(&sim.world,first+2,10000));assert(!sim.world.objects[first+2].active);
    assert(sim.world.objects[first].active && sim.world.objects[first+1].active && sim.world.objects[first+3].active);sync_state(true);
    left=ray(-.3f,.8f,1,swat_v(0,0,-1),.6f);assert(!left.hit || left.index==first+1);
    assert(swat_world_damage(&sim.world,first+1,10000));assert(sim.world.objects[first].active && !sim.world.objects[first+3].active);sync_state(true);
    assert(!ray(0,.8f,1,swat_v(0,0,-1),2).hit);
    assert(swat_world_damage(&sim.world,first,10000));sync_state(true);
    puts("PASS transformer damage: one panel removes independently, cabinet loss sheds its remaining panel, plinth is separate, and fresh replicas match each state");
    swat_sim_reset(&sim);sync_state(true);
    for(int fault=0;fault<5;fault++) {
        decoded_map=map;
        if(fault==0)decoded_map.objects[first+1].center.x+=.02f;
        if(fault==1)decoded_map.objects[first+2].material=SWAT_WOOD;
        if(fault==2)decoded_map.objects[first+3].supports[0]=first;
        if(fault==3)decoded_map.objects[first].structural_thickness+=.01f;
        if(fault==4)decoded_map.count--;
        swat_sim_close(&replica);swat_apply_map(&replica,&decoded_map);assert(!replica.world.motel);
    }
    decoded_map=map;decoded_map.count=SWAT_MOTEL_ROADSIDE_END;swat_sim_close(&replica);swat_apply_map(&replica,&decoded_map);assert(replica.world.motel);
    SwatMotelInstance p;assert(!swat_motel_transformer(&replica.world,first,&p));assert(swat_motel_roadside(&replica.world,SWAT_MOTEL_SEDAN_FIRST,&p));
    assert(swat_motel_roadside_mask(&replica.world,0)==((UINT64_C(1)<<SWAT_SEDAN_PARTS)-1));
    puts("PASS transformer lifecycle: modified geometry/material/support/section and truncated candidates reject canonical collision, previous roadside map stays canonical, reset restores all four owners");
    swat_sim_close(&replica);swat_sim_close(&sim);if(trace)assert(!fclose(trace));return 0;
}
