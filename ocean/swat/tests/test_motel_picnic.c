#include "protocol.h"
#include "replay.h"
#include "motel.h"
#include "pose.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded_map;
static SwatSnapshot snapshot,decoded_snapshot;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static FILE*trace;
static b3Pos point(float x,float y,float z){return(b3Pos){6+x,-.08+y,7.5+z};}
static int named(const char*n){for(int i=0;i<SWAT_MOTEL_PICNIC_PARTS;i++)if(!strcmp(swat_motel_picnic_part(i)->name,n))return SWAT_MOTEL_PICNIC_FIRST+i;assert(false);return-1;}
static void sync_state(bool fresh){
 if(fresh){swat_sim_close(&replica);swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n&&swat_decode_map(&decoded_map,bytes,n));swat_apply_map(&replica,&decoded_map);assert(replica.world.motel&&replica.world.count==SWAT_MOTEL_OBJECTS);}
 swat_capture_snapshot(&sim,1,&snapshot);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);assert(n&&swat_decode_snapshot(&decoded_snapshot,bytes,n)&&swat_apply_snapshot(&replica,&decoded_snapshot));assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));if(trace)assert(fprintf(trace,"%d %08x\n",sim.tick,swat_replay_digest(&sim))>0);
}
static void place(b3Pos feet,float yaw){SwatController*c=&sim.actors[0].controller;b3Body_SetTransform(c->body.body,b3OffsetPos(feet,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);c->yaw=yaw;c->pitch=0;}
static SwatHit ray(float x,float y,float z,b3Vec3 d,float length){return swat_world_ray(&sim.world,point(x,y,z),d,length,b3_nullBodyId);}
int main(int argc,char**argv){
 assert(argc<=2);setvbuf(stdout,NULL,_IONBF,0);if(argc==2){trace=fopen(argv[1],"w");assert(trace);}
 SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=cfg.hostile_fire=false;cfg.squad_bots=0;cfg.max_ticks=10000;swat_config_human(&cfg);swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);sync_state(true);
 int total=0,subsets=0,anchors=0;
 for(int i=0;i<50;i++){
  const SwatRoadsidePart*p=swat_motel_picnic_part(i);const SwatObject*o=&sim.world.objects[SWAT_MOTEL_PICNIC_FIRST+i];SwatMotelInstance placement;assert(swat_motel_picnic(&sim.world,o->tag.index,&placement));assert(o->active&&o->part==SWAT_PART_FIXTURE&&o->material==p->material&&o->query_mesh.data&&o->query_mesh.data->triangleCount==p->triangle_count);total+=p->triangle_count;
  int n=b3Body_GetShapeCount(o->body),triangles=0;b3ShapeId shapes[4];assert(n>0&&n<=4&&b3Body_GetShapes(o->body,shapes,4)==n);for(int j=0;j<n;j++){b3Mesh m=b3Shape_GetMesh(shapes[j]);assert(m.data&&m.data->triangleCount<=240);triangles+=m.data->triangleCount;subsets++;}assert(triangles==p->triangle_count);
  for(int j=0;j<SWAT_MAX_SUPPORTS&&p->parents[j]>=0;j++){assert(o->supports[j]==SWAT_MOTEL_PICNIC_FIRST+p->parents[j]);b3Pos at=point(p->anchors[j].x,p->anchors[j].y,p->anchors[j].z);const SwatObject*parent=&sim.world.objects[o->supports[j]];const b3Vec3 axes[]={{1,0,0},{0,1,0},{0,0,1}};bool hit=false;for(int k=0;k<3;k++)for(int sign=-1;sign<=1;sign+=2)hit|=swat_world_object_ray(parent,b3OffsetPos(at,swat_mul(axes[k],sign*.05f)),swat_mul(axes[k],sign*-.1f));assert(hit);anchors++;}
 }
 assert(total==5752&&subsets==56&&anchors==63);puts("PASS picnic canonical owners:50 material owners,5752 unchanged triangles,56 bounded contact meshes,63 cooked parent anchors and exact fresh replicas");
 assert(!ray(0,.2f,1.2f,swat_v(0,0,-1),2.4f).hit);assert(!ray(.25f,.85f,.08125f,swat_v(0,-1,0),.19f).hit);SwatHit top=ray(.25f,.85f,.1625f,swat_v(0,-1,0),.19f);assert(top.hit&&top.index==named("Tabletop_Plank_02")&&fabs(top.point.y-(.76-.08))<1e-5);puts("PASS picnic sparse collision: under-frame opening,drainage gap and adjacent source plank agree with actual scene rays");
 place(point(0,0,1.8f),-SWAT_PI*.5f);SwatInput input=swat_neutral_input();input.forward=1;for(int t=0;t<120;t++){swat_sim_step(&sim,&input);assert(sim.actors[0].alive);if(t%30==0)sync_state(false);}b3Pos stopped=swat_body_feet_position(&sim.actors[0].controller.body);assert(stopped.z>point(0,0,.78f).z&&stopped.z<point(0,0,1.5f).z&&stopped.y<.1f);puts("PASS picnic bench stop: ordinary player movement stops at the original bench in the real motel court");
 place(point(1.5f,0,1.6f),-SWAT_PI*.5f);b3Pos goal=point(1.5f,0,-1.6f),waypoint=goal;int t;
 for(t=0;t<1800;t++){SwatController*c=&sim.actors[0].controller;b3Pos feet=swat_body_feet_position(&c->body);if(hypotf(feet.x-goal.x,feet.z-goal.z)<.3f)break;assert(swat_navigation_next_for_actor(&sim,0,goal,&waypoint));b3Vec3 d=b3SubPos(waypoint,feet);float angle=swat_angle(b3Atan2(d.z,d.x)-c->yaw);input=swat_neutral_input();input.yaw_delta=swat_clamp(angle,-6*SWAT_RAD,6*SWAT_RAD);input.forward=fabsf(angle)<.35f?.8f:0;swat_sim_step(&sim,&input);assert(sim.actors[0].alive);if(t%30==0)sync_state(false);}assert(t<1800);sync_state(false);printf("PASS picnic route: normal navigation/controller clears the table's right end in%d ticks with exact replicas\n",t);
 int plank=named("Tabletop_Plank_01"),left=named("Tabletop_Bolt_1_L"),right=named("Tabletop_Bolt_1_R"),shots=sim.actors[0].arsenal.shots;place(point(.25f,0,2.4f),-SWAT_PI*.5f);b3Pos target=point(.25f,.76f,.325f);
 for(t=0;t<480&&sim.world.objects[plank].active;t++){
  SwatController*c=&sim.actors[0].controller;SwatPose pose=swat_pose(c,&sim.actors[0].arsenal);b3Vec3 aim=swat_normalize(b3SubPos(target,pose.eye));float yaw=b3Atan2(aim.z,aim.x),pitch=b3Atan2(aim.y,hypotf(aim.x,aim.z));float current_yaw=c->yaw+c->recoil_yaw,current_pitch=c->pitch+c->recoil_pitch;input=swat_neutral_input();input.aim=true;input.yaw_delta=swat_clamp(swat_angle(yaw-current_yaw),-2*SWAT_RAD,2*SWAT_RAD);input.pitch_delta=swat_clamp(pitch-current_pitch,-2*SWAT_RAD,2*SWAT_RAD);input.fire=t>=90&&t%30==0;swat_sim_step(&sim,&input);assert(sim.actors[0].alive);if(t%30==0)sync_state(false);
 }
 assert(t<480&&sim.actors[0].arsenal.shots>shots);int removed=0;for(int i=SWAT_MOTEL_PICNIC_FIRST;i<SWAT_MOTEL_OBJECTS;i++){bool expected=i==plank||i==left||i==right;assert(sim.world.objects[i].active!=expected);removed+=expected;}assert(removed==3);sync_state(true);printf("PASS picnic live firing: ordinary aim/fire removes one plank and both complete fastener pairs in%d shots,with the other47 owners and fresh replica intact\n",sim.actors[0].arsenal.shots-shots);
 assert(!ray(.25f,.85f,.325f,swat_v(0,-1,0),.19f).hit);top=ray(.25f,.85f,.1625f,swat_v(0,-1,0),.19f);assert(top.hit&&top.index==named("Tabletop_Plank_02"));
 assert(swat_world_damage(&sim.world,named("Frame_Left_Leg_Front"),100000));int active=0;for(int i=SWAT_MOTEL_PICNIC_FIRST;i<SWAT_MOTEL_OBJECTS;i++)active+=sim.world.objects[i].active;assert(active==8);assert(!sim.world.objects[named("Frame_Left_Leg_Rear")].active);assert(!swat_motel_picnic_unstable_roots(&sim.world));sync_state(true);puts("PASS picnic support loss: a failed source leg sheds its unsupported partner and dependent assembly without restoring prior board damage");
 swat_sim_reset(&sim);assert(swat_motel_picnic_mask(&sim.world)==((UINT64_C(1)<<50)-1));sync_state(true);
 assert(swat_world_damage(&sim.world,named("Frame_Left_TopBearer"),100000));assert(sim.world.objects[named("Frame_Left_Leg_Front")].active&&sim.world.objects[named("Frame_Left_Leg_Rear")].active);sync_state(true);
 assert(swat_world_damage(&sim.world,named("Frame_Left_SeatBearer"),100000));assert(!sim.world.objects[named("Frame_Left_Leg_Front")].active&&!sim.world.objects[named("Frame_Left_Leg_Rear")].active);assert(sim.world.objects[named("Frame_Right_Leg_Front")].active&&sim.world.objects[named("Frame_Right_Leg_Rear")].active);assert(!swat_motel_picnic_unstable_roots(&sim.world));sync_state(true);puts("PASS picnic loose-leg controls: one remaining bearer retains the end frame; removing its last bearer sheds both angled legs,with exact fresh replicas");
 swat_sim_reset(&sim);assert(swat_motel_picnic_mask(&sim.world)==((UINT64_C(1)<<50)-1));sync_state(true);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n);bytes[4]=44;assert(!swat_decode_map(&decoded_map,bytes,n));puts("PASS picnic reset/protocol: fresh intact assembly and exact replica; incompatible version44 map rejected");
 swat_sim_close(&replica);swat_sim_close(&sim);if(trace)fclose(trace);return 0;
}
