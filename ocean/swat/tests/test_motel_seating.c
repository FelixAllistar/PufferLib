#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot state;
static unsigned char packet[SWAT_NET_PACKET_MAX];
static void synchronize(void) {
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(packet,sizeof(packet),&map);
    assert(n && swat_decode_map(&decoded,packet,n));swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&state);n=swat_encode_snapshot(packet,sizeof(packet),&state);
    assert(n && swat_decode_snapshot(&state,packet,n) && swat_apply_snapshot(&replica,&state));
}
int main(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);
    const int triangle_counts[]={5608,3308,2715},floors[]={57,57,81};
    for(int i=0;i<3;i++) {
        int owner=SWAT_MOTEL_SEATING_FIRST+i;SwatMotelInstance p;assert(swat_motel_prop(&sim.world,owner,&p) && p.asset==50+i);
        const SwatMotelAsset* a=swat_motel_asset(p.asset);const SwatObject* o=&sim.world.objects[owner];
        printf("seating part%d source triangles%d collision triangles%d\n",i,a->triangle_count,o->query_mesh.data->triangleCount);
        assert(a->triangle_count==triangle_counts[i] && o->query_mesh.data->triangleCount==triangle_counts[i]);
        assert(p.scale.x==1 && p.scale.y==1 && p.scale.z==1 && o->part==SWAT_PART_FIXTURE && !o->door && !o->fractured);
        assert(o->material==(i==1?SWAT_CLOTH:SWAT_WOOD));assert(o->supports[0]==(i==1?SWAT_MOTEL_SEATING_FIRST:floors[i]));
        for(int k=1;k<SWAT_MAX_SUPPORTS;k++)assert(o->supports[k]==-1);
        assert(o->query_mesh.scale.x==.01f && o->query_mesh.scale.y==.01f && o->query_mesh.scale.z==.01f);
        if(i==1)continue;
        b3Vec3 contacts[4]={{0,INFINITY,0},{0,INFINITY,0},{0,INFINITY,0},{0,INFINITY,0}};
        for(int v=0;v<a->vertex_count;v++) {
            b3Vec3 point=b3Add(a->vertices[v],a->center);int foot=(point.x>0)+2*(point.z>0);
            if(point.y<contacts[foot].y)contacts[foot]=point;
        }
        for(int foot=0;foot<4;foot++) {
            b3Vec3 q=b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),p.yaw),contacts[foot]);
            SwatHit h=swat_world_ray(&sim.world,b3OffsetPos(p.origin,b3Add(q,swat_v(0,.001f,0))),swat_v(0,-1,0),.002f,o->body);
            assert(h.hit && h.index==floors[i] && fabsf((float)h.point.y-(float)p.origin.y)<.0001f);
        }
    }
    synchronize();for(int i=0;i<3;i++)assert(replica.world.objects[SWAT_MOTEL_SEATING_FIRST+i].query_mesh.data->triangleCount==triangle_counts[i]);
    assert(swat_motel_window_part_parent(&sim.world,SWAT_MOTEL_SEATING_FIRST)==-1);
    // Original R1 windows remain a valid complete legacy recipe.
    map.count=SWAT_MOTEL_WINDOWS_END;swat_apply_map(&replica,&map);assert(replica.world.motel && swat_motel_windows_revised(&replica.world));
    synchronize();map.count=SWAT_MOTEL_SEATING_FIRST+1;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[SWAT_MOTEL_SEATING_FIRST].center.x+=.01f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();map.objects[SWAT_MOTEL_SEATING_FIRST+1].supports[0]=57;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();state.objects[SWAT_MOTEL_SEATING_FIRST].active=false;assert(!swat_apply_snapshot(&replica,&state));
    puts("PASS seating: original primitive geometry, source metres, eight floor contacts, legacy windows, malformed placement/partial recipe/support rejection");
    int frame=SWAT_MOTEL_SEATING_FIRST,cushion=frame+1,bed=frame+2;
    assert(!swat_world_damage(&sim.world,bed,1));synchronize();assert(replica.world.objects[bed].health==sim.world.objects[bed].health);
    assert(swat_world_damage(&sim.world,cushion,10000));assert(sim.world.objects[frame].active && !sim.world.objects[cushion].active);synchronize();
    swat_sim_reset(&sim);assert(swat_world_damage(&sim.world,frame,10000));assert(!sim.world.objects[frame].active && !sim.world.objects[cushion].active && sim.world.objects[bed].active);synchronize();
    // The canonical floor is indestructible. A synthetic failure exercises
    // attachment propagation; this does not claim gameplay floor destruction.
    swat_sim_reset(&sim);sim.world.objects[81].health=sim.world.objects[81].max_health=1;
    assert(swat_world_damage(&sim.world,81,1));assert(!sim.world.objects[bed].active && !sim.world.objects[SWAT_MOTEL_NIGHTSTAND_FIRST].active && sim.world.objects[frame].active);synchronize();
    swat_sim_close(&replica);swat_sim_close(&sim);
    puts("PASS seating damage: partial health replication, independent cushion removal, failed frame sheds cushion, failed floor removes daybed and nightstand, reset/close ownership");
}
