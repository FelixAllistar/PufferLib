#include "protocol.h"
#include "motel.h"
#include "replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot snapshot;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static FILE* trace;
static b3Pos point(b3Vec3 v){return b3OffsetPos((b3Pos){-4.9f,-.08f,-8.4f},b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),SWAT_PI),v));}
static b3Vec3 direction(b3Vec3 v){return b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),SWAT_PI),v);}
static SwatHit ray(b3Vec3 p,b3Vec3 d,float length){return swat_world_ray(&sim.world,point(p),direction(d),length,b3_nullBodyId);}
static void sync_state(void){
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n&&swat_decode_map(&decoded,bytes,n));
    swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&snapshot);n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(n&&swat_decode_snapshot(&snapshot,bytes,n)&&swat_apply_snapshot(&replica,&snapshot));
    assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));
    if(trace)assert(fprintf(trace,"%08x\n",swat_replay_digest(&sim))>0);
}
static void physical_passages(void){
    for(int side=0;side<2;side++){
        float z=side?-10.0f:-7.0f;
        swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-7.5f,-.08f,z},0);
        for(int t=0;t<300;t++){
            b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);
            if(feet.x>-3.7f)break;
            SwatInput in=swat_neutral_input();in.forward=.65f;in.gait=SWAT_WALK;swat_sim_step(&sim,&in);
            if(t%30==0)sync_state();
        }
        b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);
        assert(feet.x>-3.7f&&fabsf((float)feet.z-z)<.1f&&sim.actors[0].health==100);
    }
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-4.9f,-.08f,-10},SWAT_PI*.5f);
    for(int t=0;t<120;t++){
        SwatInput in=swat_neutral_input();in.forward=.65f;in.gait=SWAT_WALK;swat_sim_step(&sim,&in);
        if(t%30==0)sync_state();
    }
    b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);
    assert(feet.z<-9.0f&&feet.z>-9.8f&&sim.actors[0].health==100);
    puts("PASS dumpster movement: ordinary controller input crosses both service lanes; the same physical body stops against the solid container");
}
static void bullet_cover(void){
    float damage[2];
    for(int steel=0;steel<2;steel++){
        float x=steel?0:1.13f,y=steel?.70f:.85f;
        swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,point(swat_v(x,0,-1.2f)),0);
        sim.actors[1].last_shot_tick=sim.tick; // Controlled lawful return-fire fixture.
        swat_sim_shoot(&sim,0,point(swat_v(x,y,1)),direction(swat_v(0,0,-1)),(SwatShot){.fired=true,.damage=34,.range=3,.energy=1});
        damage[steel]=100-sim.actors[1].health;sync_state();
    }
    assert(damage[0]>0&&damage[1]==0&&!sim.debrief.roe_violations);
    printf("PASS dumpster bullet cover: fork opening passes %.3f damage to the actor behind; thin steel blocks the same round, with matching encoded replicas\n",damage[0]);
}
int main(int argc,char** argv){
    assert(argc<=2);if(argc==2){trace=fopen(argv[1],"w");assert(trace);}
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);int first=SWAT_MOTEL_DUMPSTER_FIRST;
    for(int i=0;i<3;i++){
        SwatMotelInstance p;assert(swat_motel_prop(&sim.world,first+i,&p)&&p.asset==60+i);
        SwatObject* o=&sim.world.objects[first+i];assert(o->query_mesh.data&&o->query_mesh.data->triangleCount==(i?472:1964));
        assert(o->material==(i?SWAT_RUBBER:SWAT_STEEL)&&o->part==SWAT_PART_FIXTURE);
        assert(o->supports[0]==(i?first:0)&&o->supports[1]==-1);
        int count=b3Body_GetShapeCount(o->body);b3ShapeId shapes[32];assert(count>1&&count<=32);
        assert(b3Body_GetShapes(o->body,shapes,32)==count);
        for(int j=0;j<count;j++)assert(b3Shape_GetMesh(shapes[j]).data->triangleCount<=240);
    }
    // Original skid corners, not the enclosing box's minimum-height plane.
    for(int s=-1;s<=1;s+=2)for(int edge=-1;edge<=1;edge+=2)for(int z=-1;z<=1;z+=2){
        b3Pos a=point(swat_v(s*.7f+edge*.065f,.03f,z*.61f));
        SwatHit h=swat_world_ray(&sim.world,a,swat_v(0,-1,0),.05f,sim.world.objects[first].body);
        assert(h.hit&&h.index==0&&fabsf((float)h.point.y+.08f)<1e-5f);
    }
    for(int s=-1;s<=1;s+=2){
        SwatHit opening=ray(swat_v(s*1.13f,.85f,.9f),swat_v(0,0,-1),1.8f);assert(!opening.hit);
        SwatHit sleeve=ray(swat_v(s*1.13f,.95f,.9f),swat_v(0,0,-1),1.8f);assert(sleeve.hit&&sleeve.index==first);
    }
    assert(!ray(swat_v(0,.10f,1),swat_v(0,0,-1),2).hit);
    SwatHit steel=ray(swat_v(0,.70f,1),swat_v(0,0,-1),1);assert(steel.hit&&steel.index==first);
    float thickness=swat_world_exit_distance(&sim.world.objects[first],steel.point,direction(swat_v(0,0,-1)));
    fprintf(stderr,"Dumpster steel exit %.9f m\n",thickness);assert(thickness>.014f&&thickness<.024f);
    for(int i=0;i<2;i++){SwatHit lid=ray(swat_v(i?.5f:-.5f,2,0),swat_v(0,-1,0),1);assert(lid.hit&&lid.index==first+1+i);}
    sync_state();puts("PASS dumpster geometry: original thin steel panels, two separate lids, open fork sleeves/underbody, eight ground contacts and bounded source-preserving contact subsets");
    bullet_cover();
    // Rear passage remains usable by the shared physical navigation planner.
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    // Exterior end of the legacy service-exit route; the bay's closed door
    // remains outside this segment and is opened by the full level fixture.
    assert(b3World_CastMover(sim.world.id,(b3Pos){-7.5f,0,-8},&capsule,swat_v(.7f,0,0),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    b3Pos next;assert(swat_navigation_next(&sim, (b3Pos){-7.5f,-.08f,-7.0f},(b3Pos){-3.5f,-.08f,-7.0f},&next));
    assert(swat_navigation_next(&sim,(b3Pos){-7.5f,-.08f,-10.0f},(b3Pos){-3.5f,-.08f,-10.0f},&next));
    map.count=SWAT_MOTEL_SCREWDRIVER_END;swat_apply_map(&replica,&map);assert(replica.world.motel);
    SwatMotelInstance p;assert(swat_motel_prop(&replica.world,SWAT_MOTEL_SCREWDRIVER_FIRST,&p)&&p.asset==59);
    assert(!swat_motel_dumpster(&replica.world,first,&p));
    sync_state();map.count=first+1;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync_state();map.objects[first].center.x+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync_state();map.objects[first+1].material=SWAT_STEEL;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync_state();map.objects[first+2].supports[0]=0;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync_state();map.objects[0].center.y+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync_state();snapshot.objects[first].active=false;assert(!swat_apply_snapshot(&replica,&snapshot));
    assert(!swat_world_damage(&sim.world,first+1,1));sync_state();assert(replica.world.objects[first+1].health==sim.world.objects[first+1].health);
    assert(swat_world_damage(&sim.world,first+1,100000));sync_state();
    assert(sim.world.objects[first].active&&!sim.world.objects[first+1].active&&sim.world.objects[first+2].active);
    assert(!ray(swat_v(-.5f,2,0),swat_v(0,-1,0),1.2f).hit);
    assert(ray(swat_v(.5f,2,0),swat_v(0,-1,0),1).index==first+2);
    assert(swat_world_damage(&sim.world,first,100000));sync_state();for(int i=0;i<3;i++)assert(!replica.world.objects[first+i].active);
    swat_sim_reset(&sim);sync_state();for(int i=0;i<3;i++)assert(replica.world.objects[first+i].active);
    physical_passages();
    swat_sim_close(&replica);swat_sim_close(&sim);if(trace)assert(!fclose(trace));
    puts("PASS dumpster lifecycle: rear routes, independent lid damage/opening, body-owned removal, reset, encoded late replicas, old screwdriver prefix and malformed geometry/support rejection");
}
