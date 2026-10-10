#include "motel.h"
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot state;
static unsigned char packet[SWAT_NET_PACKET_MAX];
static void sync(void);
static void cloth_penetration(void){
 swat_sim_reset(&sim);
 int pane=swat_motel_window_find(&sim.world,17,"room_glass_left",SWAT_OPAQUE_GLASS);
 int cloth=swat_motel_window_find(&sim.world,17,"left_curtain",SWAT_CLOTH);
 assert(swat_world_damage(&sim.world,pane,10000));
 const SwatWindowModel* model=swat_motel_window_model(0);
 int part=cloth-swat_motel_window_first(17);b3Vec3 center={0};bool found=false;
 for(int t=0;t<model->triangle_count;t++)if(model->triangle_parts[t]==part){
  const b3Vec3* v=model->vertices+3*t;
  b3Vec3 c=swat_mul(b3Add(b3Add(v[0],v[1]),v[2]),1.0f/3);
  b3Vec3 n=b3Cross(b3Sub(v[1],v[0]),b3Sub(v[2],v[0]));
  if(c.y>.45f && c.y<.8f && fabsf(n.z)>b3Length(n)*.8f){center=c;found=true;break;}
 }
 assert(found);const SwatMotelInstance* p=swat_motel_instance(16);
 b3Vec3 normal=swat_v(sinf(p->yaw),0,cosf(p->yaw));
 b3Pos target=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*center.x+sinf(p->yaw)*center.z,center.y,-sinf(p->yaw)*center.x+cosf(p->yaw)*center.z));
 b3Pos from=b3OffsetPos(target,swat_mul(normal,-.05f));
 SwatHit hit=swat_world_ray(&sim.world,from,normal,.1f,b3_nullBodyId);assert(hit.hit && hit.index==cloth);
 float depth=swat_world_exit_distance(&sim.world.objects[cloth],hit.point,normal);assert(depth>0 && depth<.002f);
 swat_sim_spawn_actor(&sim,1,SWAT_CIVILIAN,b3OffsetPos(target,b3Add(swat_mul(normal,.4f),swat_v(0,-.85f,0))),0);
 assert(swat_world_sight_ray(&sim.world,from,normal,.8f,b3_nullBodyId).index==cloth);
 swat_sim_shoot(&sim,0,from,normal,(SwatShot){.fired=true,.damage=34,.range=.8f,.energy=6});
 assert(sim.world.objects[cloth].active && sim.world.objects[cloth].health<sim.world.objects[cloth].max_health);
 assert(sim.actors[1].health>0 && sim.actors[1].health<100);sync();
 puts("PASS revised-window cloth: measured thin folded surface blocks sight, ordinary bullet penetrates and hits concealed actor while curtain remains");
}
static void sync(void){
 swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(packet,sizeof(packet),&map);assert(n && swat_decode_map(&decoded,packet,n));
 swat_apply_map(&replica,&decoded);assert(replica.world.motel);swat_capture_snapshot(&sim,1,&state);n=swat_encode_snapshot(packet,sizeof(packet),&state);
 assert(n && swat_decode_snapshot(&state,packet,n) && swat_apply_snapshot(&replica,&state));
 for(int i=SWAT_MOTEL_WINDOWS_FIRST;i<sim.world.count;i++){assert(replica.world.objects[i].active==sim.world.objects[i].active);assert(replica.world.objects[i].health==sim.world.objects[i].health);}
}
int main(void){
 SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;swat_sim_init(&sim,cfg,81);
 assert(swat_motel_windows_revised(&sim.world) && sim.world.count==SWAT_MOTEL_OBJECTS);
 const int parents[]={6,17,41,65,89};int glass=0,triangles=0,shapes=0;
 for(int i=0;i<10;i++)assert(!sim.world.objects[SWAT_MOTEL_PANES_FIRST+i].active);
 for(int bay=0;bay<5;bay++){
  const SwatWindowModel* model=swat_motel_window_model(bay?0:1);int parent=parents[bay],first=swat_motel_window_first(parent);
  assert(sim.world.objects[parent].active && B3_IS_NULL(sim.world.objects[parent].shape) && !b3Body_GetShapeCount(sim.world.objects[parent].body));
  assert(swat_motel_window_mask(&sim.world,parent)==((UINT64_C(1)<<model->part_count)-1));
  for(int part=0;part<model->part_count;part++){
   SwatObject* o=&sim.world.objects[first+part];assert(swat_motel_window_part_parent(&sim.world,first+part)==parent && o->query_mesh.data);
   assert(o->material==model->parts[part].material && o->structural_thickness==model->parts[part].thickness);
   triangles+=o->query_mesh.data->triangleCount;int n=b3Body_GetShapeCount(o->body);b3ShapeId ids[64];assert(n>0 && n<64 && b3Body_GetShapes(o->body,ids,64)==n);shapes+=n;int total=0;
   for(int j=0;j<n;j++){b3Mesh m=b3Shape_GetMesh(ids[j]);assert(m.data->triangleCount<=240);total+=m.data->triangleCount;}assert(total==o->query_mesh.data->triangleCount);
   if(o->material!=SWAT_OPAQUE_GLASS)continue;
   glass++;assert(o->query_mesh.data->triangleCount==12 && fabsf(o->structural_thickness-.0064f)<1e-8f);
   b3Vec3 normal=swat_v(cosf(o->yaw),0,-sinf(o->yaw));
   for(int side=0;side<2;side++){
    b3Vec3 direction=swat_mul(normal,side?-1:1);b3Pos from=b3OffsetPos(o->center,swat_mul(direction,-.02f));
    SwatHit hit=swat_world_ray(&sim.world,from,direction,.04f,b3_nullBodyId);assert(hit.hit && hit.index==first+part);
    assert(swat_world_sight_ray(&sim.world,from,direction,.04f,b3_nullBodyId).index==first+part);
    assert(fabsf(swat_world_exit_distance(o,hit.point,direction)-.0064f)<2e-6f);
   }
  }
 }
 assert(glass==12 && triangles==26052);printf("PASS revised windows: 137 damage pieces, %d original triangles, %d bounded shapes, 12 independent 6.4mm opaque panes; original aggregates have no ghost collision\n",triangles,shapes);
 sync();int left=swat_motel_window_find(&sim.world,17,"room_glass_left",SWAT_OPAQUE_GLASS),right=swat_motel_window_find(&sim.world,17,"room_glass_right",SWAT_OPAQUE_GLASS);
 assert(left>=0 && right>=0);SwatObject* pane=&sim.world.objects[left];b3Vec3 normal=swat_v(cosf(pane->yaw),0,-sinf(pane->yaw));
 b3Pos from=b3OffsetPos(pane->center,swat_mul(normal,-.02f));swat_sim_shoot(&sim,0,from,normal,(SwatShot){.fired=true,.damage=34,.range=.04f,.energy=6});
 assert(!pane->active && sim.world.objects[right].active);assert(!swat_world_ray(&sim.world,from,normal,.04f,b3_nullBodyId).hit);sync();
 int frame=swat_motel_window_find(&sim.world,17,"left_jamb",SWAT_ALUMINUM);assert(frame>=0 && sim.world.objects[frame].active);
 int curtain=swat_motel_window_find(&sim.world,17,"left_curtain",SWAT_CLOTH);assert(curtain>=0);
 assert(swat_world_damage(&sim.world,frame,10000));assert(!sim.world.objects[right].active && sim.world.objects[curtain].active);sync();
 int socket=swat_motel_window_find(&sim.world,17,"left_rod_socket",SWAT_STEEL);assert(swat_world_damage(&sim.world,socket,10000));assert(!sim.world.objects[curtain].active);sync();
 puts("PASS revised-window damage: independent pane break leaves profiles; failed jamb sheds dependent rail/divider/glass; failed socket sheds rod/textile loops; encoded late replicas match");
 cloth_penetration();
 swat_sim_reset(&sim);int charge=swat_motel_window_find(&sim.world,6,"lobby_glass_left_lower",SWAT_OPAQUE_GLASS);
 assert(swat_world_breachable(&sim.world.objects[charge]));
 assert(swat_world_breach(&sim.world,charge,sim.world.objects[charge].center)>0 && !sim.world.objects[charge].active);
 assert(sim.world.objects[17].active && sim.world.objects[swat_motel_window_find(&sim.world,17,"room_glass_left",SWAT_OPAQUE_GLASS)].active);sync();
 puts("PASS revised-window charge: material section permits local lobby assembly breach without damaging another window assembly");
 swat_sim_reset(&sim);sync();map.count=SWAT_MOTEL_WINDOWS_FIRST+1;swat_apply_map(&replica,&map);assert(!replica.world.motel);
 sync();map.objects[SWAT_MOTEL_WINDOWS_FIRST].structural_thickness+=.001f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
 sync();state.objects[17].active=false;assert(!swat_apply_snapshot(&replica,&state));
 sync();map.count=SWAT_MOTEL_WINDOWS_FIRST;swat_apply_map(&replica,&map);assert(replica.world.motel && !swat_motel_windows_revised(&replica.world));
 assert(b3Body_GetShapeCount(replica.world.objects[17].body)>0 && replica.world.objects[SWAT_MOTEL_PANES_FIRST+2].active);
 swat_sim_close(&replica);swat_sim_close(&sim);puts("PASS revised-window validation: legacy 1190-object recipe retained; partial revision, changed section and unsupported snapshot rejected");
}
