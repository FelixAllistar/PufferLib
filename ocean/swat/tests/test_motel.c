#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatWorld world;
static void utility_hits(SwatWorld* w) {
    for(int room=0;room<4;room++) {
        int rack=SWAT_MOTEL_BASE_INSTANCES+1+2*room,basket=rack+1;
        const SwatMotelInstance* r=swat_motel_instance(rack-1);
        const SwatMotelInstance* b=swat_motel_instance(basket-1);
        // Ray crosses the empty space under the rack; no aggregate box hit.
        b3Pos from={r->origin.x,r->origin.y+.3f,r->origin.z+.3f};
        assert(!swat_world_ray(w,from,swat_v(0,0,-1),.6f,b3_nullBodyId).hit);
        from.y=r->origin.y+.491f;
        SwatHit rail=swat_world_ray(w,from,swat_v(0,0,-1),.6f,b3_nullBodyId);
        assert(rail.hit && rail.index==rack);
        // Through the open top into the base, rather than an invisible lid.
        from=(b3Pos){b->origin.x,b->origin.y+.5f,b->origin.z};
        SwatHit base=swat_world_ray(w,from,swat_v(0,-1,0),.6f,b3_nullBodyId);
        assert(base.hit && base.index==basket && base.distance>.46f && base.distance<.51f);
        from=(b3Pos){b->origin.x,b->origin.y+.18f,b->origin.z};
        SwatHit side=swat_world_ray(w,from,swat_v(1,0,0),.3f,b3_nullBodyId);
        assert(side.hit && side.index==basket && side.distance>.10f && side.distance<.16f);
    }
}
static void route(b3Pos* source,int count) {
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    for(int i=1;i<count;i++) {
        b3Pos from={source[i-1].x,source[i-1].z,-source[i-1].y};
        b3Pos to={source[i].x,source[i].z,-source[i].y};
        float f=b3World_CastMover(world.id,from,&capsule,b3SubPos(to,from),b3DefaultQueryFilter(),NULL,NULL);
        printf("capsule route %.2f %.2f -> %.2f %.2f: %.6f\n",(float)from.x,(float)from.z,(float)to.x,(float)to.z,f); fflush(stdout); assert(f>.999f);
    }
}
int main(void) {
    swat_world_init(&world); swat_motel_build(&world); assert(world.motel && world.count==SWAT_MOTEL_INSTANCES+1 && world.room_count==6);
    utility_hits(&world);
    int doors=0,first=-1;
    for(int i=0;i<world.count;i++) if(world.objects[i].door) { doors++;if(first<0)first=i;world.objects[i].door_open=true; }
    assert(doors==5);
    SwatObject* d=&world.objects[first];
    b3Pos from={d->hinge.x+.54f,1.1f,1};
    SwatHit closed=swat_world_ray(&world,from,swat_v(0,0,-1),2,b3_nullBodyId);assert(closed.hit && closed.index==first);
    for(int t=0;t<60;t++)swat_world_step_doors(&world);
    assert(!swat_world_ray(&world,from,swat_v(0,0,-1),2,b3_nullBodyId).hit);
    b3Pos gallery[]={{-11.2f,-.85f,0},{6.8f,-.85f,0}}; route(gallery,2);
    b3Pos reception[]={{-11.25f,-.85f,0},{-11.25f,4.72f,0},{-10,4.72f,0},{-10,8.6f,0}};route(reception,4);
    b3Pos side[]={{-10,8,0},{-6.8f,8,0}};route(side,2);
    b3Pos west[]={{-14,-.6f,-.08f},{-14,3.3f,-.08f}};route(west,2);
    b3Pos ramp[]={{-10.9f,-3.01f,-.078f},{-10.9f,-1.83f,0},{-10.9f,-.8f,0}};route(ramp,3);
    for(int i=0;i<4;i++) {
        float x=-6+4*i;b3Pos room[]={{x-1.2f,-.85f,0},{x-1.2f,.7f,0},{x-.15f,1.12f,0},{x-.15f,3.52f,0},{x-1.2f,3.52f,0},{x-1.2f,4.70f,0}};route(room,6);
    }
    assert(swat_world_damage(&world,first,1000) && !world.objects[first].active);
    assert(swat_world_ray(&world,(b3Pos){-4,1,1},swat_v(0,0,-1),2,b3_nullBodyId).hit);
    swat_world_close(&world);assert(!world.motel && !world.motel_meshes[0]);
    // A network replica must reconstruct exactly the same non-box geometry.
    static SwatSim server,replica;SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.randomize=false;config.hostile_fire=false;
    swat_sim_init(&server,config,42);static SwatMap map,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&server,1,&map);size_t length=swat_encode_map(bytes,sizeof(bytes),&map);assert(length && swat_decode_map(&decoded,bytes,length));
    swat_apply_map(&replica,&decoded);assert(replica.world.motel && replica.world.count==server.world.count);
    utility_hits(&replica.world);
    for(int i=1;i<server.world.count;i++) if(!server.world.objects[i].door) assert(b3Shape_GetType(replica.world.objects[i].shape)==b3_meshShape);
    for(int i=0;i<4;i++) {
        b3Pos p={-7.2f+4*i,1,1};SwatHit a=swat_world_ray(&server.world,p,swat_v(0,0,-1),2,b3_nullBodyId),b=swat_world_ray(&replica.world,p,swat_v(0,0,-1),2,b3_nullBodyId);
        assert(a.hit && b.hit && a.index==b.index && fabsf(a.distance-b.distance)<1e-5f);
    }
    swat_sim_close(&replica);
    // Original 147-object motel maps keep their original mesh collision.
    SwatMap legacy=decoded;legacy.count=SWAT_MOTEL_BASE_INSTANCES+1;
    swat_apply_map(&replica,&legacy);assert(replica.world.motel && replica.world.count==147);
    swat_sim_close(&replica);
    for(int i=1;i<server.world.count;i++) if(server.world.objects[i].door) server.world.objects[i].door_open=true;
    for(int i=0;i<60;i++) swat_world_step_doors(&server.world);
    swat_capture_map(&server,2,&map); length=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(length && swat_decode_map(&decoded,bytes,length));
    swat_apply_map(&replica,&decoded); assert(replica.world.motel);
    swat_sim_close(&replica);
    decoded.objects[2].center.x+=.1f;
    swat_apply_map(&replica,&decoded); assert(!replica.world.motel);
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) assert(!replica.world.motel_meshes[i]);
    assert(b3Shape_GetType(replica.world.objects[2].shape)==b3_hullShape);
    swat_sim_close(&replica);swat_sim_reset(&server);assert(server.world.motel);swat_sim_close(&server);
    puts("PASS motel: nine capsule routes, five functional hinged/breachable doors, original mesh openings, exact network collision reconstruction, reset and mesh ownership");return 0;
}
