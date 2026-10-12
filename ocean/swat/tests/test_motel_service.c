#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot snapshot;
static unsigned char packet[SWAT_NET_PACKET_MAX];
static b3Pos local_point(const SwatMotelInstance* p,b3Vec3 v) {
    return b3OffsetPos(p->origin,b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),p->yaw),v));
}
static void synchronize(void) {
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(packet,sizeof(packet),&map);
    assert(n&&swat_decode_map(&decoded,packet,n));swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&snapshot);n=swat_encode_snapshot(packet,sizeof(packet),&snapshot);
    assert(n&&swat_decode_snapshot(&snapshot,packet,n)&&swat_apply_snapshot(&replica,&snapshot));
}
int main(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);
    const int triangles[]={712,5676,1992,1408};int first=SWAT_MOTEL_SERVICE_FIRST;SwatMotelInstance placements[4];
    for(int i=0;i<4;i++) {
        SwatObject* o=&sim.world.objects[first+i];assert(swat_motel_service(&sim.world,first+i,&placements[i]));
        assert(placements[i].asset==55+i&&placements[i].scale.x==1&&placements[i].scale.y==1&&placements[i].scale.z==1);
        assert(o->query_mesh.data&&o->query_mesh.data->triangleCount==triangles[i]&&o->part==SWAT_PART_FIXTURE&&!o->door);
        assert(o->material==(i==1?SWAT_RUBBER:SWAT_STEEL));
        assert(b3Distance(o->center,local_point(&placements[i],swat_motel_asset(55+i)->center))<1e-4f);
    }
    assert(sim.world.objects[first+1].supports[0]==first&&sim.world.objects[first+2].supports[0]==first+1);
    assert(swat_material(SWAT_RUBBER)->resistance<swat_material(SWAT_STEEL)->resistance);
    // Source wall-reference coordinates, not invented certified fasteners.
    const b3Vec3 anchors[]={{0,-.077f,0},{0,.077f,0},{-.044f,.062665071f,0},{.044f,.062665071f,0},{-.044f,.147665071f,0},{.044f,.147665071f,0}};
    for(int i=0;i<6;i++) {
        int index=i<2?0:3;SwatObject* o=&sim.world.objects[first+index];b3Pos p=local_point(&placements[index],anchors[i]);
        SwatHit wall=swat_world_ray(&sim.world,b3OffsetPos(p,swat_v(0,0,-.001f)),swat_v(0,0,1),.006f,o->body);
        assert(wall.hit&&fabsf((float)wall.point.z+6.09f)<.002f);
        bool found=false;for(int k=0;k<SWAT_MAX_SUPPORTS;k++)found|=wall.index==o->supports[k];assert(found);
    }
    // The authored 45 mm separation survives placement. The coil's central
    // opening reaches the supporting wall instead of a solid bounding proxy.
    assert(fabsf((float)(placements[3].origin.x-placements[0].origin.x)+.322f)<1e-5f);
    assert(fabsf((float)(placements[3].origin.y-placements[0].origin.y)+.105165071f)<1e-5f);
    b3Pos origin=local_point(&placements[0],swat_v(0,-.19f,.7f));
    SwatHit opening=swat_world_ray(&sim.world,origin,swat_v(0,0,1),.8f,b3_nullBodyId);
    assert(opening.hit&&opening.index<first);
    int hose_hits=0,empty=0;
    for(int x=0;x<17;x++)for(int y=0;y<21;y++) {
        origin=local_point(&placements[0],swat_v(-.24f+x*.03f,-.54f+y*.032f,.7f));
        SwatHit hit=swat_world_ray(&sim.world,origin,swat_v(0,0,1),.8f,b3_nullBodyId);
        if(hit.hit&&hit.index==first+1){hose_hits++;assert(swat_world_exit_distance(&sim.world.objects[first+1],hit.point,swat_v(0,0,1))>0);}
        if(hit.hit&&hit.index<first)empty++;
    }
    assert(hose_hits>10&&empty>100);
    synchronize();assert(replica.world.objects[first+1].query_mesh.data->triangleCount==5676);
    map.count=SWAT_MOTEL_CABINET_END;swat_apply_map(&replica,&map);assert(replica.world.motel);
    SwatMotelInstance p;assert(swat_motel_prop(&replica.world,SWAT_MOTEL_CABINET_FIRST,&p));assert(!swat_motel_service(&replica.world,first,&p));
    synchronize();map.objects[first+1].material=SWAT_STEEL;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[first+2].supports[0]=first+3;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[first+3].center.x+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();snapshot.objects[first].active=false;assert(!swat_apply_snapshot(&replica,&snapshot));
    assert(!swat_world_damage(&sim.world,first+1,.1f));synchronize();assert(replica.world.objects[first+1].health==sim.world.objects[first+1].health);
    assert(swat_world_damage(&sim.world,first+1,10000));synchronize();
    assert(sim.world.objects[first].active&&!sim.world.objects[first+1].active&&!sim.world.objects[first+2].active&&sim.world.objects[first+3].active);
    assert(!replica.world.objects[first+2].active&&replica.world.objects[first].active);
    swat_sim_reset(&sim);assert(swat_world_damage(&sim.world,first,10000));synchronize();
    assert(!replica.world.objects[first].active&&!replica.world.objects[first+1].active&&!replica.world.objects[first+2].active&&replica.world.objects[first+3].active);
    swat_sim_reset(&sim);int support=sim.world.objects[first].supports[0];assert(swat_world_damage(&sim.world,support,10000));synchronize();
    assert(!replica.world.objects[first].active&&!replica.world.objects[first+1].active&&!replica.world.objects[first+2].active);
    swat_sim_reset(&sim);support=sim.world.objects[first].supports[0];assert(swat_world_breach(&sim.world,support,sim.world.objects[support].center)>0);synchronize();
    assert(!replica.world.objects[first].active&&!replica.world.objects[first+1].active&&!replica.world.objects[first+2].active);
    swat_sim_reset(&sim);assert(sim.world.objects[first+1].active&&sim.world.objects[first+1].query_mesh.data->triangleCount==5676);
    swat_sim_close(&replica);swat_sim_close(&sim);
    printf("PASS wall service: full 8380+1408 source triangles, original metre assembly, six wall references, open coil (%d hits/%d openings), distinct rubber/metal damage, dependent removal, support/C4 replicas, legacy cabinet maps and malformed state rejection\n",hose_hits,empty);
}
