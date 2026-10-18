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
static void build_progress(void* context){(*(int*)context)++;}
static void sync_state(bool fresh) {
    if(fresh){swat_sim_close(&replica);swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded_map,bytes,n));int serviced=0;swat_apply_map_progress(&replica,&decoded_map,build_progress,&serviced);assert(serviced>32 && !replica.world.build_progress && !replica.world.build_context && replica.world.motel);}
    swat_capture_snapshot(&sim,1,&state);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded_state,bytes,n) && swat_apply_snapshot(&replica,&decoded_state));
    assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));
    if(trace)assert(fprintf(trace,"%d %08x\n",sim.tick,swat_replay_digest(&sim))>0);
}
static b3Pos point(int model,b3Vec3 local) {
    SwatMotelInstance p;assert(swat_motel_roadside(&sim.world,swat_motel_roadside_first(model),&p));
    return b3OffsetPos(p.origin,b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),p.yaw),local));
}
static b3Vec3 direction(int model,b3Vec3 v) {return model?v:swat_v(-v.x,v.y,-v.z);}
static SwatHit ray(int model,b3Vec3 origin,b3Vec3 dir,float distance) {return swat_world_ray(&sim.world,point(model,origin),direction(model,dir),distance,b3_nullBodyId);}
static int named(int model,const char* name) {for(int i=0;i<swat_motel_roadside_count(model);i++)if(!strcmp(swat_motel_roadside_part(model,i)->name,name))return swat_motel_roadside_first(model)+i;assert(false);return -1;}
static void place(b3Pos feet,float yaw) {
    SwatController* c=&sim.actors[0].controller;b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);c->yaw=yaw;c->pitch=0;
}
static void walk(int model,b3Vec3 start,float yaw,int ticks) {
    place(point(model,start),yaw);SwatInput input=swat_neutral_input();input.forward=1;
    for(int t=0;t<ticks;t++){swat_sim_step(&sim,&input);assert(sim.actors[0].alive);assert(fabsf(b3Body_GetLinearVelocity(sim.actors[0].controller.body.body).y)<12);if(t%30==0)sync_state(false);}
}
static void street_route(void) {
    b3Pos goal=point(1,swat_v(0,0,1.3f)),waypoint=goal;place((b3Pos){-10,-.08f,15},SWAT_PI*.5f);int t;
    for(t=0;t<1800;t++) {
        SwatController* c=&sim.actors[0].controller;b3Pos feet=swat_body_feet_position(&c->body);
        if(hypotf(feet.x-goal.x,feet.z-goal.z)<.35f)break;
        if(t%15==0)assert(swat_navigation_next_for_actor(&sim,0,goal,&waypoint));
        b3Vec3 d=b3SubPos(waypoint,feet);float angle=swat_angle(b3Atan2(d.z,d.x)-c->yaw);
        SwatInput in=swat_neutral_input();in.yaw_delta=swat_clamp(angle,-6*SWAT_RAD,6*SWAT_RAD);in.forward=fabsf(angle)<.35f?1:0;
        swat_sim_step(&sim,&in);assert(sim.actors[0].alive);if(t%30==0)sync_state(false);
    }
    if(t==1800){b3Pos at=swat_body_feet_position(&sim.actors[0].controller.body);fprintf(stderr,"Street route stopped %.3f %.3f toward %.3f %.3f waypoint %.3f %.3f\n",at.x,at.z,goal.x,goal.z,waypoint.x,waypoint.z);}
    assert(t<1800);printf("PASS roadside connection: ordinary controller follows supported navigation from the motel court to the shelter's open front in %d ticks\n",t);
}
int main(int argc,char** argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    assert(argc<=2);if(argc==2){trace=fopen(argv[1],"w");assert(trace);}
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=10000;swat_config_human(&cfg);
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);sync_state(true);
    for(int model=0;model<2;model++) {
        int total=0;for(int part=0;part<swat_motel_roadside_count(model);part++) {
            const SwatRoadsidePart* p=swat_motel_roadside_part(model,part);const SwatObject* o=&sim.world.objects[swat_motel_roadside_first(model)+part];
            assert(o->active && o->material==p->material && o->query_mesh.data && o->supports[0]>=0);
            assert(o->query_mesh.data->triangleCount==p->triangle_count);total+=p->triangle_count;
            int count=b3Body_GetShapeCount(o->body);b3ShapeId shapes[64];assert(count>0 && count<64);assert(b3Body_GetShapes(o->body,shapes,64)==count);
            for(int i=0;i<count;i++)assert(b3Shape_GetMesh(shapes[i]).data->triangleCount<=240);
        }
        assert(total==(model?7868:13124));
    }
    puts("PASS roadside ownership: all 20992 source triangles, 78 material owners, bounded sparse contact shapes and encoded replica reconstruction");
    // Source wheel arches remain open above and beside the real tires. Ground
    // rays confirm each actual authored tire/baseplate anchor at its placement.
    for(int model=0;model<2;model++)for(int part=0;part<swat_motel_roadside_count(model);part++) {
        const SwatRoadsidePart* p=swat_motel_roadside_part(model,part);
        for(int a=0;a<p->anchor_count;a++) {
            b3Pos at=point(model,p->anchors[a]);int floor=sim.world.objects[swat_motel_roadside_first(model)+part].supports[0];
            assert(swat_world_object_ray(&sim.world.objects[floor],b3OffsetPos(at,swat_v(0,.02f,0)),swat_v(0,-.04f,0)));
        }
    }
    SwatHit opening=ray(0,swat_v(1.05f,.66f,1.28f),swat_v(-1,0,0),.38f);
    assert(!opening.hit);SwatHit tire=ray(0,swat_v(1.05f,.58f,1.28f),swat_v(-1,0,0),.4f);
    assert(tire.hit && sim.world.objects[tire.index].material==SWAT_RUBBER);
    SwatHit cover=ray(0,swat_v(1.1f,.65f,0),swat_v(-1,0,0),.7f);
    assert(cover.hit && sim.world.objects[cover.index].material==SWAT_STEEL);
    assert(!ray(1,swat_v(0,1.5f,.9f),swat_v(0,0,-1),1.4f).hit);
    SwatHit screen=ray(1,swat_v(-.6f,1.5f,.9f),swat_v(0,0,-1),1.8f);
    assert(screen.hit && screen.index==named(1,"Screen_Left"));
    SwatHit timber=ray(1,swat_v(.3f,1,.14f),swat_v(0,-1,-.6f),1);
    assert(timber.hit && sim.world.objects[timber.index].material==SWAT_WOOD);
    puts("PASS roadside geometry: real grounded contacts, open wheel arch and shelter front, separate rubber tire, steel cover and timber bench");
    walk(1,swat_v(0,0,1.8f),-SWAT_PI*.5f,70);b3Pos at=swat_body_feet_position(&sim.actors[0].controller.body);
    assert(at.z<35.4f && at.z>34.7f);
    walk(0,swat_v(2,0,0),0,100);at=swat_body_feet_position(&sim.actors[0].controller.body);
    assert(at.x< -6.9f && at.x> -7.4f);
    walk(0,swat_v(2,0,3.4f),0,100);at=swat_body_feet_position(&sim.actors[0].controller.body);assert(at.x>-5.3f);
    b3Pos next;assert(swat_navigation_next(&sim,point(0,swat_v(2,0,0)),point(0,swat_v(-2,0,0)),&next));
    puts("PASS roadside movement: ordinary controller enters the shelter and stops at the actual bench/car surfaces without entering their solids");
    street_route();
    // Physical material hits damage one owner and actual dependents. Another
    // slat, the opposite screen and the remaining car assembly must survive.
    int slat=named(1,"BenchSeatSlat_01"),bolt=named(1,"BenchSeatSlat_01_bolts"),other=named(1,"BenchSeatSlat_02");
    assert(swat_world_damage(&sim.world,slat,10000));assert(!sim.world.objects[slat].active && !sim.world.objects[bolt].active && sim.world.objects[other].active);sync_state(true);
    int glass=named(0,"Front_side_window_-1"),other_glass=named(0,"Front_side_window_1");
    assert(swat_world_damage(&sim.world,glass,100));assert(!sim.world.objects[glass].active && sim.world.objects[other_glass].active && sim.world.objects[named(0,"Coachwork")].active);
    uint64_t mask=swat_motel_roadside_mask(&sim.world,0);assert(!(mask&(UINT64_C(1)<<(glass-SWAT_MOTEL_SEDAN_FIRST))));sync_state(false);
    swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,point(1,swat_v(-.6f,0,-1.2f)),0);
    SwatShot shot={.fired=true,.damage=34,.range=4,.energy=1};b3Pos muzzle=point(1,swat_v(-.6f,1.5f,.9f));
    swat_sim_shoot(&sim,0,muzzle,swat_v(0,0,-1),shot);float through=100-sim.actors[1].health;
    int left=named(1,"Screen_Left"),right=named(1,"Screen_Right");assert(swat_world_damage(&sim.world,left,10000));assert(!sim.world.objects[left].active && sim.world.objects[right].active);
    assert(!ray(1,swat_v(-.6f,1.5f,.9f),swat_v(0,0,-1),1.8f).hit);sync_state(true);
    puts("PASS roadside damage: independent glazing, timber slat and its steel fixings, screen aperture and matching live/fresh encoded replicas");
    sim.actors[1].health=100;swat_sim_shoot(&sim,0,muzzle,swat_v(0,0,-1),shot);float open=100-sim.actors[1].health;
    assert(open>0 && through<open && sim.actors[1].alive);sync_state(false);
    printf("PASS roadside ballistic screen: original steel gives %.3f damage versus %.3f through the real broken aperture, with matching encoded state\n",through,open);
    swat_capture_map(&sim,1,&map);int first=SWAT_MOTEL_SEDAN_FIRST;
    for(int fault=0;fault<5;fault++) {
        decoded_map=map;
        if(fault==0)decoded_map.objects[first].center.x+=.1f;
        if(fault==1)decoded_map.objects[first].supports[0]=1;
        if(fault==2)decoded_map.objects[first+1].material=SWAT_WOOD;
        if(fault==3)decoded_map.objects[first+2].structural_thickness*=2;
        if(fault==4)decoded_map.count--;
        swat_sim_close(&replica);swat_apply_map(&replica,&decoded_map);assert(!replica.world.motel);
    }
    decoded_map=map;decoded_map.count=SWAT_MOTEL_DUMPSTER_END;swat_sim_close(&replica);swat_apply_map(&replica,&decoded_map);assert(replica.world.motel);SwatMotelInstance p;assert(!swat_motel_roadside(&replica.world,first,&p));
    for(int model=0;model<2;model++)for(int part=0;part<swat_motel_roadside_count(model);part++) {
        const SwatRoadsidePart* source=swat_motel_roadside_part(model,part);
        for(int a=0;a<source->anchor_count;a++) {
            b3Pos at=point(model,source->anchors[a]);SwatHit hit=swat_world_ray(&replica.world,b3OffsetPos(at,swat_v(0,.5f,0)),swat_v(0,-1,0),1,b3_nullBodyId);
            assert(hit.hit && fabs(hit.point.y-at.y)<1e-4f && hit.index==sim.world.objects[swat_motel_roadside_first(model)+part].supports[0]);
        }
    }
    swat_sim_reset(&sim);assert(sim.world.count==SWAT_MOTEL_OBJECTS && sim.world.objects[glass].active && sim.world.objects[slat].active);sync_state(true);
    puts("PASS roadside lifecycle: malformed geometry/material/attachment/section/truncated candidates retain explicit boxes, old canonical prefix remains, reset restores complete props");
    swat_sim_close(&replica);swat_sim_close(&sim);if(trace)assert(!fclose(trace));return 0;
}
