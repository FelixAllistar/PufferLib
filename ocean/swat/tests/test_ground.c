#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
typedef struct GroundQuery {const SwatWorld* world;SwatHit hit;} GroundQuery;
static float support_hit(b3ShapeId shape,b3Pos point,b3Vec3 normal,float fraction,uint64_t material,int triangle,int child,void* context) {
    GroundQuery* q=context;SwatTag* tag=b3Body_GetUserData(b3Shape_GetBody(shape));
    if(!tag || tag->kind!=SWAT_HIT_WORLD)return -1;
    const SwatObject* o=&q->world->objects[tag->index];
    // The preserved bank lip and rocks sit above the joined ground. Probe the
    // support underneath them, rather than mistake that cover for a bad seam.
    if(tag->index!=0 && tag->index<SWAT_MOTEL_GROUND_FIRST && swat_motel_surroundings_part(q->world,o)!=0)return -1;
    q->hit=(SwatHit){.hit=true,.kind=SWAT_HIT_WORLD,.index=tag->index,.point=point,.normal=normal,.fraction=fraction,.distance=4*fraction};
    return fraction;
}
static SwatHit ground(const SwatWorld* w,float x,float z) {
    GroundQuery q={.world=w};b3World_CastRay(w->id,(b3Pos){x,3,z},swat_v(0,-4,0),b3DefaultQueryFilter(),support_hit,&q);return q.hit;
}
static void place(float x,float z) {
    SwatHit h=ground(&sim.world,x,z);assert(h.hit);
    SwatController* c=&sim.actors[0].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(h.point,swat_v(0,c->body.totalHeight*.5f+.015f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
}
static void walk(float x,float z) {
    int t=0;for(;t<5000;t++) {
        SwatController* c=&sim.actors[0].controller;b3Pos feet=swat_body_feet_position(&c->body);
        b3Vec3 d=swat_v(x-feet.x,0,z-feet.z);if(b3Length(d)<.2f)break;
        SwatInput in=swat_neutral_input();float error=swat_angle(atan2f(d.z,d.x)-c->yaw);
        in.yaw_delta=swat_clamp(error,-.1f,.1f);if(fabsf(error)<.3f)in.forward=.8f;
        swat_sim_step(&sim,&in);
        if(!(feet.y>-.4f && feet.y<.12f)) {printf("Ground walk to %.2f %.2f tick%d feet %.3f %.3f %.3f\n",x,z,t,feet.x,feet.y,feet.z);fflush(stdout);}
        assert(feet.y>-.4f && feet.y<.12f);
    }
    assert(t<5000);
}
int main(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=120000;
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);
    int rays=0;
    for(int part=0;part<SWAT_GROUND_PARTS;part++) {
        const SwatMotelAsset* a=swat_motel_ground_asset(part);const SwatObject* o=&sim.world.objects[SWAT_MOTEL_GROUND_FIRST+part];
        assert(sim.world.ground_meshes[part] && sim.world.ground_meshes[part]->triangleCount==12);
        for(int x=0;x<3;x++)for(int z=0;z<3;z++) {
            float px=a->center.x+(.7f*x-.7f)*a->half.x,pz=a->center.z+(.7f*z-.7f)*a->half.z;
            SwatHit h=ground(&sim.world,px,pz);assert(h.hit && h.index==o->tag.index);
            assert(h.point.y>=-.201f && h.point.y<=-.079f);rays++;
            float thickness=swat_world_exit_distance(o,h.point,swat_v(0,-1,0));assert(thickness>.39f && thickness<.53f);
        }
    }
    // Probe both sides of court, asphalt, driveway and shoulder-end seams.
    const float seams[][4]={{-23,24,0,1},{0,24,0,1},{23,24,0,1},{-24,20,1,0},{24,20,1,0},
        {-3,30,1,0},{3,30,1,0},{0,36,0,1},{0,44,0,1},{-56,40,1,0},{56,40,1,0},
        {30,0,1,0},{-30,0,1,0},{27,12,0,1},{27,-12,0,1},{-27,8,0,1},{-27,-8,0,1}};
    for(unsigned i=0;i<sizeof(seams)/sizeof(seams[0]);i++) {
        SwatHit a=ground(&sim.world,seams[i][0]-.002f*seams[i][2],seams[i][1]-.002f*seams[i][3]);
        SwatHit b=ground(&sim.world,seams[i][0]+.002f*seams[i][2],seams[i][1]+.002f*seams[i][3]);
        assert(a.hit && b.hit && fabs(a.point.y-b.point.y)<.002f);rays+=2;
    }
    const float outside[][2]={{-64.1f,0},{64.1f,0},{0,-48.1f},{0,52.1f}};
    for(int i=0;i<4;i++)assert(!ground(&sim.world,outside[i][0],outside[i][1]).hit);
    // Staging/extraction remain the original court, with a clear outward path.
    for(int z=5;z<=48;z++)for(int x=-2;x<=2;x++) {
        SwatHit h=ground(&sim.world,x,z);assert(h.hit && fabs(h.point.y+.08)<.001f);
        if(z<=13)assert(h.index==0);
        rays++;
    }
    // Physical controllers, not nav coordinates, cross each material/grade.
    place(0,20);walk(0,46);walk(0,20);
    // Go around the existing raised bank/rocks, along the new end grades.
    place(22,14);walk(34,14);walk(22,14);
    place(-22,10);walk(-34,10);walk(-22,10);
    place(-58,40);walk(58,40);
    printf("PASS ground: %d rays, exact closed thickness, court/asphalt/shoulder seams, finite edges and live controller crossings\n",rays);fflush(stdout);
    // The wider grid exceeds uint16 capacity; distant road routes remain valid.
    b3Pos next;assert(swat_navigation_next(&sim,(b3Pos){-60,-.08f,46},(b3Pos){60,-.08f,46},&next));
    assert(sim.navigation->nodes>UINT16_MAX && sim.navigation->cells==216*168);
    assert(!swat_navigation_next(&sim,(b3Pos){0,-.08f,20},(b3Pos){70,-.08f,40},&next));
    sim.config.tactical_rules=true;place(0,0);
    swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,(b3Pos){0,-.08f,20},SWAT_PI*.5f);sim.actor_count=4;
    sim.actors[3].mind.bot=true;sim.actors[3].mind.order=SWAT_ORDER_MOVE;sim.actors[3].mind.goal=(b3Pos){0,-.08f,46};
    int t=0;for(;t<6000;t++){swat_sim_step(&sim,&(SwatInput){0});if(b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),sim.actors[3].mind.goal)<.65f)break;}assert(t<6000);
    static SwatMap map,decoded;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded,bytes,n));
    swat_apply_map(&replica,&decoded);assert(replica.world.motel && replica.world.count==SWAT_MOTEL_OBJECTS);
    for(int part=0;part<SWAT_GROUND_PARTS;part++) {
        const SwatMotelAsset* a=swat_motel_ground_asset(part);SwatHit h=ground(&sim.world,a->center.x,a->center.z),r=ground(&replica.world,a->center.x,a->center.z);
        assert(h.hit && r.hit && h.index==r.index && fabs(h.point.y-r.point.y)<1e-6);
    }
    swat_sim_close(&replica);map.objects[SWAT_MOTEL_GROUND_FIRST].center.x+=.1f;
    swat_apply_map(&replica,&map);assert(!replica.world.motel && !replica.world.ground_meshes[0]);swat_sim_close(&replica);
    // Four physically supported levels force real BFS nodes above 65535. This
    // catches truncated queue indices independently of the single-level road.
    for(int level=1;level<=3;level++)swat_world_box(&sim.world,(b3Pos){58,level*3,46},swat_v(3,.1f,3),SWAT_CONCRETE,0);
    assert(swat_navigation_next(&sim,(b3Pos){57,9.1f,46},(b3Pos){59,9.1f,46},&next) && next.y>9);
    bool high_index=false;for(int i=UINT16_MAX+1;i<sim.navigation->nodes;i++)if(sim.navigation->parent[i]>=0)high_index=true;
    assert(high_index);
    swat_sim_reset(&sim);assert(!sim.navigation && sim.world.ground_meshes[53]);swat_sim_close(&sim);
    for(int i=0;i<SWAT_GROUND_PARTS;i++)assert(!sim.world.ground_meshes[i]);
    cfg.mission=SWAT_BUILDING;swat_sim_init(&sim,cfg,1);(void)swat_navigation_next(&sim,(b3Pos){0,0,0},(b3Pos){1,0,0},&next);
    assert(sim.navigation->nodes==SWAT_NAV_NODES);swat_sim_close(&sim);
    puts("PASS ground: wide routes, real squad road crossing, lazy small-world grid, wire reconstruction, modified-map rejection and reset/close");
    return 0;
}
