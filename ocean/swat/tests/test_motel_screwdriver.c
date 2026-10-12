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
    assert(n&&swat_decode_map(&decoded,packet,n));swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&snapshot);n=swat_encode_snapshot(packet,sizeof(packet),&snapshot);
    assert(n&&swat_decode_snapshot(&snapshot,packet,n)&&swat_apply_snapshot(&replica,&snapshot));
}
int main(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;
    swat_sim_init(&sim,cfg,81);int owner=SWAT_MOTEL_SCREWDRIVER_FIRST,parent=SWAT_MOTEL_PROPS_FIRST;
    SwatObject* o=&sim.world.objects[owner];SwatMotelInstance p;
    assert(sim.world.count==SWAT_MOTEL_OBJECTS&&swat_motel_prop(&sim.world,owner,&p)&&p.asset==59);
    const SwatMotelAsset* a=swat_motel_asset(p.asset);
    assert(a->triangle_count==2560&&a->vertex_count==1507&&o->query_mesh.data->triangleCount==2560);
    assert(fabsf(a->half.z*2-.25f)<1e-6f&&fabsf(a->half.y*2-.02639959f)<1e-6f);
    assert(p.scale.x==1&&p.scale.y==1&&p.scale.z==1&&p.yaw==0&&o->supports[0]==parent);
    b3Vec3 minimum=a->vertices[0];for(int i=1;i<a->vertex_count;i++)if(a->vertices[i].y<minimum.y)minimum=a->vertices[i];
    b3Pos contact=b3OffsetPos(p.origin,b3Add(minimum,a->center));
    SwatHit support=swat_world_ray(&sim.world,b3OffsetPos(contact,swat_v(0,.0005f,0)),swat_v(0,-1,0),.002f,o->body);
    assert(support.hit&&support.index==parent&&fabsf((float)support.point.y-.7245f)<1e-5f);
    // Original silhouette, rather than a solid box filling the narrow tip.
    int hits=0,empty=0;
    for(int x=0;x<19;x++)for(int z=0;z<101;z++) {
        b3Pos start={p.origin.x-.018f+x*.002f,p.origin.y+.06f,p.origin.z-.15f+z*.003f};
        SwatHit hit=swat_world_ray(&sim.world,start,swat_v(0,-1,0),.08f,b3_nullBodyId);
        if(hit.hit&&hit.index==owner){hits++;assert(swat_world_exit_distance(o,hit.point,swat_v(0,-1,0))>0);}
        else empty++;
    }
    assert(hits>100&&empty>300);synchronize();
    map.count=SWAT_MOTEL_SERVICE_END;swat_apply_map(&replica,&map);assert(replica.world.motel);
    assert(swat_motel_service(&replica.world,SWAT_MOTEL_SERVICE_FIRST,&p)&&!swat_motel_prop(&replica.world,owner,&p));
    synchronize();map.objects[owner].supports[0]=127;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[owner].center.y+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[owner].material=SWAT_WOOD;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();snapshot.objects[parent].active=false;assert(!swat_apply_snapshot(&replica,&snapshot));
    assert(!swat_world_damage(&sim.world,owner,1));synchronize();assert(replica.world.objects[owner].health==o->health);
    assert(swat_world_damage(&sim.world,owner,10000));synchronize();assert(!replica.world.objects[owner].active&&replica.world.objects[parent].active);
    swat_sim_reset(&sim);assert(swat_world_damage(&sim.world,parent,10000));synchronize();assert(!replica.world.objects[owner].active);
    swat_sim_reset(&sim);assert(sim.world.objects[owner].active&&sim.world.objects[owner].query_mesh.data->triangleCount==2560);
    swat_sim_close(&replica);swat_sim_close(&sim);
    printf("PASS screwdriver: original 2560 triangles/editorial 250mm scale, actual minimum vertex/top-liner contact, %d silhouette hits/%d openings, finite damage/parent loss, live/late replicas, old service prefix and malformed-state rejection\n",hits,empty);
}
