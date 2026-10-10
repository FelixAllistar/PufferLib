#include "protocol.h"
#include "replay.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot state;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static b3Pos point(const SwatMotelInstance*,b3Vec3);
static SwatReplay journal;
static void step(SwatInput in){swat_sim_step(&sim,&in);assert(swat_replay_append(&journal,&in,&sim));}
static SwatInput look(b3Pos target) {
    SwatController* c=&sim.actors[0].controller;b3Vec3 d=b3SubPos(target,swat_controller_eye(c));SwatInput in=swat_neutral_input();
    in.yaw_delta=swat_clamp(swat_angle(atan2f(d.z,d.x)-c->yaw),-.1f,.1f);in.pitch_delta=swat_clamp(atan2f(d.y,hypotf(d.x,d.z))-c->pitch,-.1f,.1f);return in;
}
static void walk(b3Pos target) {
    b3Pos waypoint=target;int replan=0;
    int t=0;for(;t<1200;t++) {
        b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);b3Vec3 d=b3SubPos(target,feet);if(hypotf(d.x,d.z)<.43f)break;
        if(t>=replan || b3Distance(feet,waypoint)<.3f){assert(swat_navigation_next_for_actor(&sim,0,target,&waypoint));replan=t+30;}
        SwatInput in=look(b3OffsetPos(waypoint,swat_v(0,1.65f,0)));if(fabsf(in.yaw_delta)<.07f)in.forward=.8f;step(in);
    }
    if(t==1200){b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);fprintf(stderr,"walk %.3f %.3f stuck %.3f %.3f\n",(float)target.x,(float)target.z,(float)feet.x,(float)feet.z);}
    assert(t<1200);
}
static void save_shooting(const char* path) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.squad_bots=0;cfg.max_ticks=12000;
    swat_sim_init(&sim,cfg,81);assert(swat_replay_record(&journal,path,&sim));
    walk((b3Pos){9,-.079f,8});walk((b3Pos){9,-.079f,.9f});walk((b3Pos){7.9f,-.079f,.9f});
    int owner=SWAT_MOTEL_MOUNTED_FIRST+1;SwatMotelInstance p;assert(swat_motel_mounted(&sim.world,owner,&p));
    b3Pos target=point(&p,swat_v(.02f,0,.058f));
    for(int t=0;t<45;t++){SwatInput in=look(target);in.aim=true;step(in);}
    int t=0;for(;t<1800&&sim.world.objects[owner].active;t++) {
        SwatInput in=look(target);in.aim=true;in.fire=t%12==0;
        SwatWeapon* gun=&sim.actors[0].arsenal.slots[0];in.reload=!gun->magazine&&!gun->chambered;step(in);
    }
    assert(t<1800);assert(swat_replay_close(&journal));
    uint32_t digest=swat_replay_digest(&sim);char error[256];assert(swat_replay_restore(&replica,path,error,sizeof(error)));
    assert(swat_replay_digest(&replica)==digest&&!replica.world.objects[owner].active&&replica.world.motel);
    swat_sim_close(&replica);swat_sim_close(&sim);remove(path);
    puts("PASS mounted-prop save: ordinary walking/aim/fire/reload inputs from canonical spawn, exact destroyed-prop replay restore");
}
static b3Pos point(const SwatMotelInstance* p,b3Vec3 v){return b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*v.x+sinf(p->yaw)*v.z,v.y,-sinf(p->yaw)*v.x+cosf(p->yaw)*v.z));}
static void synchronize(void) {
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n&&swat_decode_map(&decoded,bytes,n));
    swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&state);assert(swat_apply_snapshot(&replica,&state));
}
static void trolley(void) {
    int owner=SWAT_MOTEL_PROPS_FIRST;SwatMotelInstance p;
    assert(swat_motel_prop(&sim.world,owner,&p));SwatObject* o=&sim.world.objects[owner];
    assert(o->active && o->part==SWAT_PART_FIXTURE && o->material==SWAT_STEEL && o->query_mesh.data->triangleCount==2416);
    assert(fabsf(o->max_health-600)<.001f && o->structural_thickness==.008f && o->supports[0]==127);
    for(int i=1;i<SWAT_MAX_SUPPORTS;i++)assert(o->supports[i]==-1);
    for(int x=-1;x<=1;x+=2)for(int z=-1;z<=1;z+=2) {
        SwatHit h=swat_world_ray(&sim.world,point(&p,swat_v(x*.410f,.005f,z*.230f)),swat_v(0,-1,0),.01f,o->body);
        // Ignore the cart body to query the floor under its authored wheel.
        assert(h.hit && h.index==127 && fabsf((float)h.point.y-p.origin.y)<.001f);
    }
    SwatHit h=swat_world_ray(&sim.world,point(&p,swat_v(0,.45f,.34f)),swat_v(0,0,-1),.68f,b3_nullBodyId);
    assert(!h.hit); // Open bay stays empty rather than becoming a solid box.
    h=swat_world_ray(&sim.world,point(&p,swat_v(0,.745f,0)),swat_v(0,-1,0),.10f,b3_nullBodyId);
    assert(h.hit && h.index==owner);
    float thickness=swat_world_exit_distance(o,h.point,swat_v(0,-1,0));
    assert(thickness>.0039f && thickness<.0041f); // Original separate liner, not invented sheet thickness.
    synchronize();assert(replica.world.objects[owner].query_mesh.data->triangleCount==2416);
    // Older version-17 maps remain exact canonical prefixes without the cart.
    map.count=SWAT_MOTEL_PROPS_FIRST;size_t n=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(n && swat_decode_map(&decoded,bytes,n));swat_apply_map(&replica,&decoded);
    assert(replica.world.motel && replica.world.count==SWAT_MOTEL_PROPS_FIRST);
    assert(swat_motel_mounted(&replica.world,SWAT_MOTEL_MOUNTED_FIRST,&p));
    synchronize();map.objects[owner].structural_thickness=.004f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();state.objects[127].active=false;
    assert(!swat_apply_snapshot(&replica,&state) && replica.world.objects[127].active);
    assert(!swat_world_impact(&sim.world,owner,34) && o->health==o->max_health);
    assert(swat_world_impact(&sim.world,owner,10000) && !o->active && B3_IS_NULL(o->body));
    assert(sim.world.objects[127].active && sim.world.objects[SWAT_MOTEL_MOUNTED_FIRST].active);
    synchronize();assert(!replica.world.objects[owner].active && B3_IS_NULL(replica.world.objects[owner].body));
    swat_sim_reset(&sim);
    puts("PASS trolley: original sparse collision, four measured floor contacts, liner exit thickness, finite material strength, removal and late join, unchanged version-17 prefix, malformed recipe and unsupported snapshot rejection");
}
static void extinguisher(void) {
    int owner=SWAT_MOTEL_PROPS_FIRST+1;SwatMotelInstance p;
    assert(swat_motel_prop(&sim.world,owner,&p));SwatObject* o=&sim.world.objects[owner];
    assert(o->active && p.asset==46 && o->part==SWAT_PART_FIXTURE && o->material==SWAT_STEEL);
    assert(o->query_mesh.data->triangleCount==2412 && b3Body_GetShapeCount(o->body)==13);
    assert(o->structural_thickness==.012f && fabsf(o->max_health-900)<.001f);
    const float heights[]={.093f,.345f};
    for(int k=0;k<2;k++) {
        b3Pos contact=point(&p,swat_v(0,heights[k],-.134f));
        SwatHit h=swat_world_ray(&sim.world,b3OffsetPos(contact,swat_v(0,0,.005f)),swat_v(0,0,-1),.01f,o->body);
        assert(h.hit && sim.world.objects[h.index].material==SWAT_BRICK && fabsf((float)h.point.z-contact.z)<.0001f);
        bool found=false;for(int i=0;i<SWAT_MAX_SUPPORTS;i++)found|=o->supports[i]==h.index;assert(found);
    }
    SwatHit h=swat_world_ray(&sim.world,point(&p,swat_v(0,.2f,.20f)),swat_v(0,0,-1),.25f,b3_nullBodyId);
    assert(h.hit && h.index==owner);
    // The original bottle is a solid outer surface, not an invented thin shell.
    assert(swat_world_exit_distance(o,h.point,swat_v(0,0,-1))>.17f);
    synchronize();assert(replica.world.objects[owner].query_mesh.data->triangleCount==2412);
    map.count=owner;size_t n=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(n && swat_decode_map(&decoded,bytes,n));swat_apply_map(&replica,&decoded);
    assert(replica.world.motel && replica.world.count==owner);
    assert(swat_motel_prop(&replica.world,SWAT_MOTEL_PROPS_FIRST,&p));
    assert(!swat_motel_prop(&replica.world,owner,&p));
    synchronize();map.objects[owner].center.x+=.05f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();int support=o->supports[0];state.objects[support].active=false;
    assert(!swat_apply_snapshot(&replica,&state) && replica.world.objects[support].active);
    assert(!swat_world_impact(&sim.world,owner,34) && o->health==o->max_health);
    assert(swat_world_damage(&sim.world,owner,10000) && !o->active && B3_IS_NULL(o->body));
    synchronize();assert(!replica.world.objects[owner].active && B3_IS_NULL(replica.world.objects[owner].body));
    swat_sim_reset(&sim);
    for(int k=0;k<SWAT_MAX_SUPPORTS && sim.world.objects[owner].supports[k]>=0;k++) {
        support=sim.world.objects[owner].supports[k];assert(swat_world_damage(&sim.world,support,10000));
        assert(!sim.world.objects[owner].active && B3_IS_NULL(sim.world.objects[owner].body));
        synchronize();assert(!replica.world.objects[owner].active);
        swat_sim_reset(&sim);
    }
    support=sim.world.objects[owner].supports[0];
    assert(swat_world_breach(&sim.world,support,sim.world.objects[support].center)>0);
    assert(!sim.world.objects[owner].active);synchronize();assert(!replica.world.objects[owner].active);
    swat_sim_reset(&sim);
    puts("PASS extinguisher: exact curved contact subsets, two measured wall anchors, original bottle exit thickness, finite steel response, damage/support/charge removal, old trolley prefix and malformed/unsupported state rejection");
}
static void noticeboard(void) {
    int owner=SWAT_MOTEL_PROPS_FIRST+2;SwatMotelInstance p;
    assert(swat_motel_prop(&sim.world,owner,&p));SwatObject* o=&sim.world.objects[owner];
    assert(o->active && p.asset==47 && o->part==SWAT_PART_FIXTURE && o->material==SWAT_WOOD);
    assert(o->query_mesh.data->triangleCount==1416 && b3Body_GetShapeCount(o->body)==7);
    assert(o->structural_thickness==.032f && fabsf(o->max_health-150*.032f/.038f)<.001f);
    b3Vec3 normal=swat_v(sinf(p.yaw),0,cosf(p.yaw));
    for(int side=-1;side<=1;side+=2) {
        b3Pos contact=point(&p,swat_v(side*.25f,.234f,0));
        SwatHit h=swat_world_ray(&sim.world,b3OffsetPos(contact,swat_mul(normal,.005f)),swat_mul(normal,-1),.01f,o->body);
        assert(h.hit && sim.world.objects[h.index].material==SWAT_BRICK && b3Distance(h.point,contact)<.0001f);
        bool found=false;for(int k=0;k<SWAT_MAX_SUPPORTS;k++)found|=o->supports[k]==h.index;assert(found);
    }
    SwatHit h=swat_world_ray(&sim.world,point(&p,swat_v(.35f,0,.10f)),swat_mul(normal,-1),.12f,b3_nullBodyId);
    assert(h.hit && h.index==owner);
    assert(fabsf(swat_world_exit_distance(o,h.point,swat_mul(normal,-1))-.032f)<.0001f);
    synchronize();assert(replica.world.objects[owner].query_mesh.data->triangleCount==1416);
    map.count=owner;size_t n=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(n && swat_decode_map(&decoded,bytes,n));swat_apply_map(&replica,&decoded);
    assert(replica.world.motel && replica.world.count==owner);
    assert(swat_motel_prop(&replica.world,owner-1,&p) && !swat_motel_prop(&replica.world,owner,&p));
    synchronize();map.objects[owner].material=SWAT_STEEL;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();assert(swat_motel_prop(&sim.world,owner,&p));int support=o->supports[0];state.objects[support].active=false;
    assert(!swat_apply_snapshot(&replica,&state) && replica.world.objects[support].active);
    int shots=0;for(;o->active && shots<8;shots++)
        swat_sim_shoot(&sim,0,point(&p,swat_v(.35f,0,.10f)),swat_mul(normal,-1),(SwatShot){.fired=true,.damage=34,.range=.5f,.energy=1});
    assert(shots==4 && !o->active && B3_IS_NULL(o->body) && sim.world.objects[support].active);
    synchronize();assert(!replica.world.objects[owner].active);swat_sim_reset(&sim);
    for(int k=0;k<SWAT_MAX_SUPPORTS && sim.world.objects[owner].supports[k]>=0;k++) {
        support=sim.world.objects[owner].supports[k];assert(swat_world_damage(&sim.world,support,10000));
        assert(!sim.world.objects[owner].active && B3_IS_NULL(sim.world.objects[owner].body));
        synchronize();assert(!replica.world.objects[owner].active);swat_sim_reset(&sim);
    }
    puts("PASS noticeboard: measured wall anchors, original 32 mm wood frame exit thickness, four-shot material damage, support loss, old extinguisher prefix, malformed recipes and unsupported snapshots");
}
static void shell(SwatWorld* world,int owner) {
    SwatMotelInstance p;assert(swat_motel_mounted(world,owner,&p));SwatObject* o=&world->objects[owner];
    assert(o->active && o->part==SWAT_PART_FIXTURE && o->query_mesh.data && o->query_mesh.data->triangleCount==1464);
    assert(o->structural_thickness==.0012f && fabsf(o->max_health-90)<.001f);
    int count=b3Body_GetShapeCount(o->body);assert(count==8);b3ShapeId shapes[8];assert(b3Body_GetShapes(o->body,shapes,8)==8);
    int triangles=0;for(int i=0;i<count;i++){b3Mesh m=b3Shape_GetMesh(shapes[i]);assert(m.data->triangleCount<=240);triangles+=m.data->triangleCount;}assert(triangles==1464);
    const SwatMotelAsset* asset=swat_motel_asset(p.asset);
    // Compare partitioned collision with the original exported shell, including
    // empty space inside its bounds. Sampling covers both faces and upper conduit.
    for(int side=0;side<2;side++)for(int y=0;y<41;y++)for(int x=0;x<17;x++) {
        b3RayCastInput in={.origin={-.052f+.0063f*x-asset->center.x,-.22f+.014f*y-asset->center.y,(side?.09f:-.03f)-asset->center.z},.translation={0,0,side?-.15f:.15f},.maxFraction=1};
        b3CastOutput full=b3RayCastMesh(&o->query_mesh,&in),parts={0};
        for(int i=0;i<count;i++){b3Mesh mesh=b3Shape_GetMesh(shapes[i]);b3CastOutput h=b3RayCastMesh(&mesh,&in);if(h.hit&&(!parts.hit||h.fraction<parts.fraction))parts=h;}
        assert(full.hit==parts.hit);if(full.hit)assert(fabsf(full.fraction-parts.fraction)<1e-5f);
    }
    b3Vec3 normal=swat_v(sinf(p.yaw),0,cosf(p.yaw));
    SwatHit h=swat_world_ray(world,point(&p,swat_v(.02f,0,.2f)),swat_mul(normal,-1),.3f,b3_nullBodyId);
    assert(h.hit && h.index==owner);
    float thickness=swat_world_exit_distance(o,h.point,swat_mul(normal,-1));assert(thickness>.002f&&thickness<.0031f);
    // Probe an open conduit mouth and its rolled metal rim independently.
    h=swat_world_ray(world,point(&p,swat_v(0,.40f,.027f)),swat_v(0,-1,0),.07f,b3_nullBodyId);assert(!h.hit);
    h=swat_world_ray(world,point(&p,swat_v(.0085f,.40f,.027f)),swat_v(0,-1,0),.07f,b3_nullBodyId);assert(h.hit&&h.index==owner);
    for(int k=0;k<SWAT_MAX_SUPPORTS;k++)if(o->supports[k]>=0)assert(o->supports[k]<owner && world->objects[o->supports[k]].active);
}
static void cascade(void) {
    SwatWorld w;swat_world_init(&w);
    int parent=swat_world_box(&w,(b3Pos){0,0,0},swat_v(1,1,1),SWAT_WOOD,1);
    int child=swat_world_box(&w,(b3Pos){3,0,0},swat_v(1,1,1),SWAT_STEEL,0);assert(swat_world_attach(&w,child,&parent,1));
    int leaf=swat_world_box(&w,(b3Pos){6,0,0},swat_v(1,1,1),SWAT_WOOD,1);assert(swat_world_attach(&w,leaf,&child,1));
    assert(!swat_world_attach(&w,parent,&child,1));assert(!swat_world_attach(&w,child,&child,1));
    assert(swat_world_damage(&w,parent,1));assert(!w.objects[parent].active&&!w.objects[child].active&&!w.objects[leaf].active&&w.generation==3);swat_world_close(&w);
}
int main(int argc,char** argv) {
    assert(argc==2);cascade();SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);
    trolley();extinguisher();noticeboard();
    for(int i=0;i<SWAT_MOTEL_MOUNTED_INSTANCES;i++)shell(&sim.world,SWAT_MOTEL_MOUNTED_FIRST+i);
    synchronize();for(int i=0;i<SWAT_MOTEL_MOUNTED_INSTANCES;i++)shell(&replica.world,SWAT_MOTEL_MOUNTED_FIRST+i);
    int owner=SWAT_MOTEL_MOUNTED_FIRST;SwatMotelInstance p;assert(swat_motel_mounted(&sim.world,owner,&p));
    // Reject cyclic/forward dependencies, unsupported snapshots and modified
    // canonical mount recipes, without mutating an already valid replica.
    map.objects[owner].supports[0]=owner;size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n&&!swat_decode_map(&decoded,bytes,n));
    swat_capture_map(&sim,1,&map);map.objects[owner].structural_thickness=.002f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    synchronize();int support=sim.world.objects[owner].supports[0];state.objects[support].active=false;
    assert(!swat_apply_snapshot(&replica,&state)&&replica.world.objects[support].active);
    // Direct shots hit the real steel cover; an intact masonry layer behind it
    // still stops the ray. Low impacts leave the mounting intact.
    assert(!swat_world_impact(&sim.world,owner,20)&&fabsf(sim.world.objects[owner].health-90)<.001f);
    b3Vec3 normal=swat_v(sinf(p.yaw),0,cosf(p.yaw));
    int shots=0;for(;sim.world.objects[owner].active&&shots<32;shots++)swat_sim_shoot(&sim,0,point(&p,swat_v(.02f,0,.2f)),swat_mul(normal,-1),(SwatShot){.fired=true,.damage=34,.range=.5f,.energy=1});
    assert(shots>1&&shots<32);assert(sim.world.objects[support].active&&sim.world.objects[owner+1].active);
    SwatHit h=swat_world_ray(&sim.world,point(&p,swat_v(.02f,0,.2f)),swat_mul(normal,-1),.5f,b3_nullBodyId);assert(h.hit&&h.index!=owner&&sim.world.objects[h.index].material==SWAT_BRICK);
    synchronize();assert(!replica.world.objects[owner].active&&B3_IS_NULL(replica.world.objects[owner].body));
    swat_sim_reset(&sim);
    // Each independent fastening is required; losing one removes shell collision
    // immediately and remains removed after late join and save/resume.
    for(int i=0;i<SWAT_MOTEL_MOUNTED_INSTANCES;i++) {
        owner=SWAT_MOTEL_MOUNTED_FIRST+i;
        for(int k=0;k<SWAT_MAX_SUPPORTS && sim.world.objects[owner].supports[k]>=0;k++) {
            support=sim.world.objects[owner].supports[k];assert(swat_world_damage(&sim.world,support,10000));
            assert(!sim.world.objects[owner].active&&B3_IS_NULL(sim.world.objects[owner].body));synchronize();assert(!replica.world.objects[owner].active);
            swat_sim_reset(&sim);
        }
    }
    owner=SWAT_MOTEL_MOUNTED_FIRST;support=sim.world.objects[owner].supports[0];
    assert(swat_world_breach(&sim.world,support,sim.world.objects[support].center)>0);assert(!sim.world.objects[owner].active);
    synchronize();assert(replica.world.motel&&!replica.world.objects[owner].active);
    swat_sim_close(&replica);swat_sim_close(&sim);
    save_shooting(argv[1]);
    printf("PASS mounted props: two measured shell colliders, 5576 partition/full rays, open conduit mouths, 2.5 mm cover, %d-shot damage, five authored anchors, transitive support loss, charge removal, map/snapshot rejection and late join\n",shots);
    return 0;
}
