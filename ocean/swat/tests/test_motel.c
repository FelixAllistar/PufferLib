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
typedef struct WallQuery {SwatWorld* world;int group;} WallQuery;
static bool wall_only(b3ShapeId shape,void* context) {
    WallQuery* q=context;SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape));
    return tag && tag->kind==SWAT_HIT_WORLD && q->world->objects[tag->index].wall_group==q->group;
}
static void masonry_ballistics(void) {
    static SwatSim s,replica;SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,81);
    // Empty rear-wall lane: the target is behind the masonry, before the bathroom partition.
    b3Pos origin={-6.7f,1.1f,-7};b3Vec3 direction=swat_v(0,0,1);
    SwatHit hit=swat_world_ray(&s.world,origin,direction,1.5f,b3_nullBodyId);
    assert(hit.hit && s.world.objects[hit.index].material==SWAT_BRICK);
    SwatObject* wall=&s.world.objects[hit.index];int group=wall->wall_group;
    assert(swat_world_breachable(wall));
    swat_sim_spawn_actor(&s,1,SWAT_CIVILIAN,(b3Pos){-6.7f,.01f,-5.5f},-SWAT_PI*.5f);
    for(int weapon=0;weapon<SWAT_WEAPON_PROFILES;weapon++) {
        const SwatWeaponDef* def=swat_weapon_def(weapon);
        SwatShot shot={.fired=true,.damage=def->damage,.range=def->range,.energy=def->energy};
        for(int i=0;i<30;i++)swat_sim_shoot(&s,0,origin,direction,shot);
        assert(s.actors[1].health==100 && wall->health==wall->max_health && wall->active);
    }
    assert(!swat_world_impact(&s.world,hit.index,160)); // A ram cannot demolish exterior masonry.
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    b3Pos entry={-6.7f,.06f,-6.6f},exit={-6.7f,.06f,-5.4f};
    WallQuery query={&s.world,group};
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),wall_only,&query)<.99f);
    int removed=swat_world_breach(&s.world,hit.index,hit.point),surviving=0;
    assert(removed>0);
    for(int i=0;i<s.world.count;i++)surviving+=s.world.objects[i].active && s.world.objects[i].wall_group==group;
    assert(surviving>0); // A local aperture, not removal of the entire facade.
    int exposed=0;
    for(int i=group-1;i<s.world.count && s.world.objects[i].wall_group==group;i++) {
        SwatMotelEdge edges[32];int n=swat_motel_wall_edges(&s.world,&s.world.objects[i],edges,32);
        exposed+=n;for(int k=0;k<n;k++) {
            assert(edges[k].owner==i && s.world.objects[i].active && !s.world.objects[edges[k].neighbor].active && edges[k].length>0);
            int neighbor=edges[k].neighbor;s.world.objects[neighbor].active=true;
            assert(swat_motel_wall_edges(&s.world,&s.world.objects[i],edges,32)==n-1);
            s.world.objects[neighbor].active=false;
            break;
        }
    }
    assert(exposed>0);
    // Isolate the assembly clearance: bathroom fixtures behind it deliberately remain solid.
    swat_sim_spawn_actor(&s,1,SWAT_CIVILIAN,(b3Pos){-6.7f,.01f,-5},-SWAT_PI*.5f);
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),wall_only,&query)>.999f);
    static SwatMap map;static SwatSnapshot snapshot;
    swat_capture_map(&s,1,&map);swat_capture_snapshot(&s,1,&snapshot);
    swat_apply_map(&replica,&map);assert(swat_apply_snapshot(&replica,&snapshot));
    query.world=&replica.world;
    assert(b3World_CastMover(replica.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),wall_only,&query)>.999f);
    swat_sim_close(&replica);
    SwatShot shot={.fired=true,.damage=34,.range=30,.energy=1};
    swat_sim_shoot(&s,0,origin,direction,shot);assert(s.actors[1].health==66);
    // Actual unobstructed exterior entry, with all world collisions enabled.
    entry=(b3Pos){-12.6f,.06f,-1.17f};exit=(b3Pos){-11.4f,.06f,-1.17f};
    hit=swat_world_ray(&s.world,b3OffsetPos(entry,swat_v(0,1,0)),swat_v(1,0,0),1.2f,b3_nullBodyId);
    assert(hit.hit && s.world.objects[hit.index].material==SWAT_BRICK);
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)<.99f);
    assert(swat_world_breach(&s.world,hit.index,hit.point)>0);
    assert(b3World_CastMover(s.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    b3Pos next;assert(swat_navigation_next(&s,entry,exit,&next));
    swat_sim_close(&s);
    // Every exterior assembly uses masonry; bathroom partitions use board.
    swat_world_init(&world);swat_motel_build(&world);
    int assemblies=0;
    for(int i=SWAT_MOTEL_INSTANCES+1;i<world.count;i++) {
        SwatObject* o=&world.objects[i];int parent=swat_motel_wall_parent(&world,o);if(parent<0)continue;
        int asset=swat_motel_instance(parent-1)->asset;
        assert(o->material==(asset==22?SWAT_DRYWALL:SWAT_BRICK));assert(swat_world_breachable(o));assemblies++;
    }
    assert(assemblies>100 && world.count<SWAT_MAX_OBJECTS);
    SwatObject probe=world.objects[world.count-1];probe.half.x=.09f;probe.material=SWAT_BRICK;assert(swat_world_breachable(&probe));
    probe.half.x=.3f;assert(!swat_world_breachable(&probe));
    probe.half.x=.1f;probe.material=SWAT_CONCRETE;assert(!swat_world_breachable(&probe));
    probe.half.x=.004f;probe.material=SWAT_STEEL;assert(!swat_world_breachable(&probe));
    printf("PASS masonry: %d pieces/%d objects, all weapon profiles stopped, civilian protected, ram resisted, localized charge aperture/capsule/replica, strength and thickness limits\n",assemblies,world.count);
    swat_world_close(&world);
}
int main(void) {
    masonry_ballistics();
    swat_world_init(&world); swat_motel_build(&world); assert(world.motel && world.count>SWAT_MOTEL_INSTANCES+1 && world.room_count==6);
    utility_hits(&world);
    for(int room=1;room<=4;room++) {
        b3Pos bulb;assert(swat_motel_lamp(&world,room,&bulb));
        SwatMotelInstance mount;int owner=swat_motel_dressing(&world,(room-1)*SWAT_MOTEL_DRESSING_ASSETS+7,&mount);
        assert(owner>0 && fabsf((float)(bulb.y-mount.origin.y)-.08959322f)<1e-6f);
        world.objects[owner].active=false;assert(!swat_motel_lamp(&world,room,&bulb));world.objects[owner].active=true;
    }
    for(int i=0;i<SWAT_MOTEL_DRESSING_INSTANCES;i++) {
        SwatMotelInstance mount;int owner=swat_motel_dressing(&world,i,&mount);assert(owner>0);
        world.objects[owner].active=false;assert(swat_motel_dressing(&world,i,&mount)==-1);world.objects[owner].active=true;
    }
    SwatMotelInstance closed_viewer,open_viewer;
    assert(swat_motel_dressing(&world,5,&closed_viewer)==16);
    world.objects[16].yaw+=SWAT_PI*.5f;
    assert(swat_motel_dressing(&world,5,&open_viewer)==16);
    assert(fabsf(open_viewer.origin.x-world.objects[16].hinge.x-.0345f)<1e-5f);
    assert(fabsf(open_viewer.origin.z-world.objects[16].hinge.z+.54f)<1e-5f);
    world.objects[16].yaw-=SWAT_PI*.5f;

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
    for(int i=1;i<=SWAT_MOTEL_INSTANCES;i++) if(server.world.objects[i].active && !server.world.objects[i].door) assert(b3Shape_GetType(replica.world.objects[i].shape)==b3_meshShape);
    for(int i=0;i<4;i++) {
        b3Pos p={-7.2f+4*i,1,1};SwatHit a=swat_world_ray(&server.world,p,swat_v(0,0,-1),2,b3_nullBodyId),b=swat_world_ray(&replica.world,p,swat_v(0,0,-1),2,b3_nullBodyId);
        assert(a.hit && b.hit && a.index==b.index && fabsf(a.distance-b.distance)<1e-5f);
    }
    swat_sim_close(&replica);
    // An actual guest-room wall opens once, with the same physics/nav and wire state.
    b3Pos entry={-4.5f,.06f,-1.17f},exit={-3.5f,.06f,-1.17f},next;
    b3Capsule capsule={.center1={0,.32f,0},.center2={0,1.52f,0},.radius=.3f};
    SwatHit wall=swat_world_ray(&server.world,b3OffsetPos(entry,swat_v(0,1,0)),swat_v(1,0,0),1,b3_nullBodyId);
    assert(wall.hit && swat_world_breachable(&server.world.objects[wall.index]));
    assert(b3World_CastMover(server.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)<.9f);
    for(int i=0;i<server.world.count;i++)if(server.world.objects[i].door)server.world.objects[i].wedge_owner=0;
    server.world.generation++;
    assert(!swat_navigation_next(&server,entry,exit,&next));
    int old_generation=server.world.generation;
    assert(swat_world_breach(&server.world,wall.index,wall.point)>4);
    assert(server.world.generation>old_generation);
    assert(!swat_world_ray(&server.world,b3OffsetPos(entry,swat_v(0,1,0)),swat_v(1,0,0),1,b3_nullBodyId).hit);
    assert(b3World_CastMover(server.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    assert(swat_navigation_next(&server,entry,exit,&next));
    assert(server.navigation->generation==server.world.generation && server.navigation->updated_cells<SWAT_NAV_CELLS);
    static SwatSnapshot snapshot;
    swat_capture_map(&server,1,&map);swat_capture_snapshot(&server,1,&snapshot);
    swat_apply_map(&replica,&map);assert(swat_apply_snapshot(&replica,&snapshot));
    assert(b3World_CastMover(replica.world.id,entry,&capsule,b3SubPos(exit,entry),b3DefaultQueryFilter(),NULL,NULL)>.999f);
    swat_sim_close(&replica);
    for(int i=0;i<server.world.count;i++)server.world.objects[i].wedge_owner=-1;
    // Original 147-object motel maps keep their original mesh collision.
    SwatMap legacy=decoded;legacy.count=SWAT_MOTEL_BASE_INSTANCES+1;
    swat_apply_map(&replica,&legacy);assert(replica.world.motel && replica.world.count==147);
    swat_sim_close(&replica);
    legacy.count=SWAT_MOTEL_INSTANCES+1;
    swat_apply_map(&replica,&legacy);assert(replica.world.motel && replica.world.objects[106].active);
    utility_hits(&replica.world);swat_sim_close(&replica);
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
    puts("PASS motel: nine capsule routes, five functional hinged/breachable doors, original mesh openings, exact network collision reconstruction, reset, attached dressing, breach capsule/LOS/nav and replicated destruction");return 0;
}
