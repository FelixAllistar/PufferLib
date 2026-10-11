#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot snapshot;
static unsigned char packet[SWAT_NET_PACKET_MAX];
static void synchronize(void) {
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(packet,sizeof(packet),&map);
    assert(n && swat_decode_map(&decoded,packet,n));swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&snapshot);n=swat_encode_snapshot(packet,sizeof(packet),&snapshot);
    assert(n && swat_decode_snapshot(&snapshot,packet,n) && swat_apply_snapshot(&replica,&snapshot));
}
int main(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);
    int owner=SWAT_MOTEL_BAG_FIRST;SwatMotelInstance p;assert(swat_motel_prop(&sim.world,owner,&p) && p.asset==53);
    SwatObject* o=&sim.world.objects[owner];const SwatMotelAsset* a=swat_motel_asset(p.asset);
    assert(a->triangle_count==4482 && o->query_mesh.data->triangleCount==4482 && o->material==SWAT_CLOTH);
    assert(p.scale.x==1 && p.scale.y==1 && p.scale.z==1 && o->supports[0]==127 && o->part==SWAT_PART_FIXTURE);
    for(int i=1;i<SWAT_MAX_SUPPORTS;i++)assert(o->supports[i]==-1);
    assert(fabsf(2*o->half.x-.5280701f)<1e-6f && fabsf(2*o->half.y-.5749994f)<1e-6f && fabsf(2*o->half.z-.462893f)<1e-6f);
    int lowest=0;for(int v=1;v<a->vertex_count;v++)if(a->vertices[v].y<a->vertices[lowest].y)lowest=v;
    b3Pos contact=b3OffsetPos(p.origin,b3Add(a->vertices[lowest],a->center));
    SwatHit floor=swat_world_ray(&sim.world,b3OffsetPos(contact,swat_v(0,.001f,0)),swat_v(0,-1,0),.002f,o->body);
    assert(floor.hit && floor.index==127 && fabsf((float)floor.point.y-(float)contact.y)<1e-5f);
    // The service trolley retains a clear side gap; this is geometric clearance,
    // while the ordinary-input completion driver verifies usable room routes.
    const SwatObject* trolley=&sim.world.objects[SWAT_MOTEL_PROPS_FIRST];
    assert(o->center.x+o->half.x<trolley->center.x-trolley->half.x-.15f);
    SwatHit bag=swat_world_ray(&sim.world,b3OffsetPos(o->center,swat_v(0,0,1)),swat_v(0,0,-1),1.5f,b3_nullBodyId);
    assert(bag.hit && bag.index==owner && swat_world_exit_distance(o,bag.point,swat_v(0,0,-1))>0);
    synchronize();assert(replica.world.objects[owner].query_mesh.data->triangleCount==4482);
    map.count=SWAT_MOTEL_SEATING_END;swat_apply_map(&replica,&map);assert(replica.world.motel && !swat_motel_prop(&replica.world,owner,&p));
    assert(swat_motel_prop(&replica.world,SWAT_MOTEL_SEATING_FIRST,&p) && replica.world.objects[SWAT_MOTEL_SEATING_FIRST].query_mesh.data);
    synchronize();map.objects[owner].center.x+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[owner].supports[0]=81;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();snapshot.objects[127].active=false;assert(!swat_apply_snapshot(&replica,&snapshot));
    assert(!swat_world_damage(&sim.world,owner,1));synchronize();assert(replica.world.objects[owner].health==o->health);
    assert(swat_world_damage(&sim.world,owner,10000));synchronize();assert(!replica.world.objects[owner].active && sim.world.objects[127].active);
    swat_sim_reset(&sim);o=&sim.world.objects[owner];assert(o->active && o->query_mesh.data->triangleCount==4482);
    // Only a synthetic floor failure exercises dependency propagation: the
    // existing canonical floor remains indestructible in ordinary gameplay.
    sim.world.objects[127].health=sim.world.objects[127].max_health=1;
    assert(swat_world_damage(&sim.world,127,1) && !o->active);synchronize();assert(!replica.world.objects[owner].active);
    swat_sim_close(&replica);swat_sim_close(&sim);
    puts("PASS maintenance bag: original 4482 triangles/metres, actual floor contact, trolley gap, shell ray/exit, legacy seating, malformed recipe/support/state rejection, partial health/removal/floor dependency replicas, reset/close");
}
