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
    int owner=SWAT_MOTEL_CABINET_FIRST;SwatMotelInstance p;assert(swat_motel_prop(&sim.world,owner,&p) && p.asset==54);
    SwatObject* o=&sim.world.objects[owner];const SwatMotelAsset* a=swat_motel_asset(p.asset);
    assert(a->triangle_count==21272 && o->query_mesh.data->triangleCount==21272 && o->material==SWAT_STEEL);
    assert(p.scale.x==1 && p.scale.y==1 && p.scale.z==1 && !o->door && o->part==SWAT_PART_FIXTURE);
    assert(fabsf(2*o->half.x-.4613243f)<1e-6f && fabsf(2*o->half.y-.5031428f)<1e-6f && fabsf(2*o->half.z-.3989851f)<1e-6f);
    int supports=0;for(int i=0;i<SWAT_MAX_SUPPORTS;i++)if(o->supports[i]>=0){supports++;assert(sim.world.objects[o->supports[i]].part==SWAT_PART_SKIN);}
    assert(supports>0);
    // These independently recorded rear-rim points are geometry references,
    // not claimed fasteners. Their actual wall ray must reach a listed support.
    const b3Vec3 contacts[]={{-.170080036f,-.249597862f,0},{.166154206f,-.248606235f,0},
        {-.170080036f,.249597862f,0},{.166154206f,.248606235f,0}};
    for(int i=0;i<4;i++) {
        b3Pos point=b3OffsetPos(p.origin,contacts[i]);
        SwatHit wall=swat_world_ray(&sim.world,b3OffsetPos(point,swat_v(0,0,.001f)),swat_v(0,0,-1),.006f,o->body);
        assert(wall.hit && fabsf((float)wall.point.z+9.91f)<.002f);
        bool listed=false;for(int k=0;k<SWAT_MAX_SUPPORTS;k++)listed|=o->supports[k]==wall.index;assert(listed);
    }
    int hits=0;
    for(int i=0;i<9;i++)for(int j=0;j<9;j++) {
        b3Pos origin=b3OffsetPos(p.origin,swat_v((i-4)*.035f,(j-4)*.05f,.6f));
        SwatHit hit=swat_world_ray(&sim.world,origin,swat_v(0,0,-1),.7f,b3_nullBodyId);
        if(hit.hit && hit.index==owner){assert(swat_world_exit_distance(o,hit.point,swat_v(0,0,-1))>0);hits++;}
    }
    assert(hits>20);
    // Its bottom stays above the two source washer envelopes, rather than
    // burying either the cabinet or its source door in a dressing prop.
    for(int washer=133;washer<=134;washer++)assert(o->center.y-o->half.y>sim.world.objects[washer].center.y+sim.world.objects[washer].half.y+.05f);
    synchronize();assert(replica.world.objects[owner].query_mesh.data->triangleCount==21272);
    map.count=SWAT_MOTEL_BAG_END;swat_apply_map(&replica,&map);assert(replica.world.motel && !swat_motel_prop(&replica.world,owner,&p));
    assert(swat_motel_prop(&replica.world,SWAT_MOTEL_BAG_FIRST,&p) && replica.world.objects[SWAT_MOTEL_BAG_FIRST].query_mesh.data);
    synchronize();map.objects[owner].center.z+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[owner].supports[0]=127;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();snapshot.objects[o->supports[0]].active=false;assert(!swat_apply_snapshot(&replica,&snapshot));
    assert(!swat_world_damage(&sim.world,owner,1));synchronize();assert(replica.world.objects[owner].health==o->health);
    assert(swat_world_damage(&sim.world,owner,10000));synchronize();assert(!replica.world.objects[owner].active);
    swat_sim_reset(&sim);o=&sim.world.objects[owner];assert(o->active && o->query_mesh.data->triangleCount==21272);
    int support=o->supports[0];assert(swat_world_damage(&sim.world,support,10000) && !o->active);synchronize();assert(!replica.world.objects[owner].active);
    swat_sim_reset(&sim);o=&sim.world.objects[owner];support=o->supports[0];
    assert(swat_world_breach(&sim.world,support,sim.world.objects[support].center)>0 && !o->active);
    synchronize();assert(replica.world.motel && !replica.world.objects[owner].active);
    swat_sim_close(&replica);swat_sim_close(&sim);
    printf("PASS service cabinet: original 21272 triangles/open pose/metres, four source rim wall contacts, washer gap, %d shell ray/exits, legacy bag maps, malformed recipe/support/state rejection, partial health/removal/wall dependency/charge replicas, reset/close\n",hits);
}
