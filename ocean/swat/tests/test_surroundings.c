#include "protocol.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SwatSim sim,replica;
static b3Pos local(SwatMotelInstance p,b3Pos v) {
    return b3OffsetPos(p.origin,swat_v(cosf(p.yaw)*v.x+sinf(p.yaw)*v.z,v.y,-sinf(p.yaw)*v.x+cosf(p.yaw)*v.z));
}
static SwatHit ground(const SwatWorld* w,b3Pos p) {p.y=3;return swat_world_ray(w,p,swat_v(0,-1,0),4,b3_nullBodyId);}
static void place(int actor,b3Pos p,float yaw) {
    SwatController* c=&sim.actors[actor].controller;SwatHit h=ground(&sim.world,p);assert(h.hit);
    p.y=h.point.y+.015f;b3Body_SetTransform(c->body.body,b3OffsetPos(p,swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));c->yaw=yaw;
}
static void walk(b3Pos goal) {
    int t=0;for(;t<600;t++) {
        SwatController* c=&sim.actors[0].controller;b3Pos feet=swat_body_feet_position(&c->body);b3Vec3 d=b3SubPos(goal,feet);
        if(hypotf(d.x,d.z)<.15f)break;
        SwatInput in=swat_neutral_input();float error=swat_angle(atan2f(d.z,d.x)-c->yaw);
        in.yaw_delta=swat_clamp(error,-.1f,.1f);if(fabsf(error)<.3f)in.forward=.7f;
        swat_sim_step(&sim,&in);assert(feet.y>-.4f && feet.y<.1f);
    }
    assert(t<600);
}
static void ballistic_materials(void) {
    // A small physical range isolates the imported closed meshes from unrelated
    // motel cover. Use their real shape data and production bullet traversal.
    static SwatSim range;
    const int parts[]={0,2};
    for(int k=0;k<2;k++) {
        memset(&range,0,sizeof(range));
        range.config=swat_default_config();swat_world_init(&range.world);
        swat_world_box(&range.world,(b3Pos){0,-.5f,0},swat_v(6,.5f,6),SWAT_CONCRETE,0);
        const SwatMotelAsset* asset=swat_motel_surroundings_asset(parts[k]);
        int owner=swat_world_box(&range.world,(b3Pos){0,asset->half.y,0},asset->half,k?SWAT_STONE:SWAT_SOIL,0);
        SwatObject* o=&range.world.objects[owner];b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        o->shape=b3CreateMeshShape(o->body,&def,sim.world.surroundings_meshes[parts[k]],swat_v(1,1,1));
        swat_sim_spawn_actor(&range,0,SWAT_OFFICER,(b3Pos){-5.2f,0,0},0);
        swat_sim_spawn_actor(&range,1,SWAT_CIVILIAN,(b3Pos){4.4f,0,0},SWAT_PI);range.actor_count=2;
        b3Pos origin={-4.4f,asset->half.y,0};SwatShot shot={.fired=true,.damage=34,.energy=1,.range=10};
        swat_sim_shoot(&range,0,origin,swat_v(1,0,0),shot);assert(range.actors[1].health==100 && o->active);
        b3DestroyBody(o->body);o->body=b3_nullBodyId;o->shape=b3_nullShapeId;o->active=false;
        swat_sim_shoot(&range,0,origin,swat_v(1,0,0),shot);assert(range.actors[1].health<100);
        swat_sim_close(&range);
    }
    puts("PASS surroundings ballistics: actual imported earth/rock meshes stop rifle energy; the same shot reaches a civilian when cover is removed");
}
int main(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;cfg.max_ticks=60000;
    swat_sim_init(&sim,cfg,81);assert(sim.world.count==SWAT_MOTEL_OBJECTS);
    int triangles=0;for(int p=0;p<SWAT_SURROUNDINGS_PARTS;p++) {
        assert(sim.world.surroundings_meshes[p] && sim.world.surroundings_meshes[p]->triangleCount<=240);
        triangles+=sim.world.surroundings_meshes[p]->triangleCount;
    }
    assert(triangles==1368);
    ballistic_materials();
    int rays=0;for(int placement=0;placement<5;placement++) {
        int first=SWAT_MOTEL_SURROUNDINGS_FIRST+placement*15;SwatMotelInstance p;assert(swat_motel_surroundings_instance(first,&p));
        for(int x=0;x<=30;x++)for(int z=0;z<=40;z++) {
            b3Pos origin=local(p,(b3Pos){.01f+x*.199f,0,-3.99f+z*.199f});SwatHit h=ground(&sim.world,origin);
            assert(h.hit && h.index>=first && h.index<first+15);assert(h.point.y>-.22f && h.point.y<1.3f);rays++;
        }
        // Both faces of the closed support are real; court and end seams have no holes.
        for(int z=0;z<=40;z++)for(int dx=-1;dx<=1;dx++) {
            b3Pos origin=local(p,(b3Pos){dx*.002f,0,-3.99f+z*.199f});SwatHit h=ground(&sim.world,origin);
            assert(h.hit && fabs(h.point.y+.08)<.001f);rays++;
        }
        for(int rock=2;rock<15;rock++) {
            const SwatObject* o=&sim.world.objects[first+rock];assert(o->material==SWAT_STONE);
            SwatHit h=ground(&sim.world,o->center);assert(h.index==first+rock);
            float thickness=swat_world_exit_distance(o,h.point,swat_v(0,-1,0));assert(thickness>.04f && thickness<1.4f);
        }
        float high=0;for(int part=0;part<15;part++) {
            const SwatObject* o=&sim.world.objects[first+part];high=fmaxf(high,(float)o->center.y+o->half.y);
        }
        high+=.1f;assert(high<2.2f);
        if(!placement) {
            SwatWorld air;swat_world_init(&air);
            SwatSoundEvent event={.position=local(p,(b3Pos){2.9f,high,0}),.strength=1,.range=30};b3Pos listener=local(p,(b3Pos){6.1f,high,0});
            SwatAcousticPath expected=swat_acoustic_path(&air,&event,listener),actual=swat_acoustic_path(&sim.world,&event,listener);
            for(int band=0;band<3;band++)assert(fabsf(expected.bands[band]-actual.bands[band])<1e-6f);
            swat_world_close(&air);
        }
        for(int z=0;z<20;z++) {
            b3Pos a=local(p,(b3Pos){2.9f,high,-3.8f+z*.39f}),b=local(p,(b3Pos){6.1f,high,-3.8f+z*.39f});
            assert(!swat_world_ray(&sim.world,a,swat_normalize(b3SubPos(b,a)),b3Distance(a,b),b3_nullBodyId).hit);
            assert(swat_world_visible(&sim.world,a,b));
        }
        // Actual controller walks onto the slope and back in all five placements.
        place(0,local(p,(b3Pos){-.7f,0,0}),p.yaw==0?0:SWAT_PI);
        walk(local(p,(b3Pos){2.3f,0,0}));walk(local(p,(b3Pos){-.7f,0,0}));
    }
    // Walk over every repeated-module join, including reversed west placement.
    const b3Pos joins[]={{25,0,-4},{25,0,4},{-25,0,0}};
    for(int i=0;i<3;i++) {
        b3Pos a=b3OffsetPos(joins[i],swat_v(0,0,-.7f)),b=b3OffsetPos(joins[i],swat_v(0,0,.7f));
        place(0,a,SWAT_PI*.5f);walk(b);walk(a);
    }
    printf("PASS surroundings: %d supported rays, 75 closed components, separate earth/rock response, no foliage colliders, ten live court crossings and six repeated-seam crossings\n",rays);fflush(stdout);
    // Real squad controller on each previously uncovered outer side.
    sim.config.tactical_rules=true;
    for(int side=-1;side<=1;side+=2) {
        swat_sim_spawn_actor(&sim,3,SWAT_OFFICER,(b3Pos){side*22.5f,0,0},side<0?SWAT_PI:0);sim.actors[3].mind.bot=true;
        sim.actors[3].mind.order=SWAT_ORDER_MOVE;sim.actors[3].mind.goal=(b3Pos){side*26.2f,-.13f,0};
        if(sim.actor_count<4)sim.actor_count=4;
        place(0,(b3Pos){0,0,12},0);
        int t=0;for(;t<1800;t++) {
            swat_sim_step(&sim,&(SwatInput){0});
            if(b3Distance(swat_body_feet_position(&sim.actors[3].controller.body),sim.actors[3].mind.goal)<.65f)break;
        }
        printf("Perimeter squad side%d completed in %d ticks\n",side,t);fflush(stdout);assert(t<1800);
    }
    static SwatMap map,decoded;static SwatSnapshot state,wire;static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded,bytes,n));
    swat_apply_map(&replica,&decoded);assert(replica.world.motel && replica.world.count==SWAT_MOTEL_OBJECTS);
    swat_capture_snapshot(&sim,1,&state);n=swat_encode_snapshot(bytes,sizeof(bytes),&state);assert(n && swat_decode_snapshot(&wire,bytes,n) && swat_apply_snapshot(&replica,&wire));
    for(int i=SWAT_MOTEL_SURROUNDINGS_FIRST;i<sim.world.count;i++) {
        const SwatObject* o=&sim.world.objects[i];SwatHit a=ground(&sim.world,o->center),b=ground(&replica.world,o->center);
        assert(a.hit && b.hit && a.index==b.index && fabs(a.point.y-b.point.y)<1e-6);
    }
    swat_sim_close(&replica);map.objects[SWAT_MOTEL_SURROUNDINGS_FIRST].center.x+=.1f;
    swat_apply_map(&replica,&map);assert(!replica.world.motel && !replica.world.surroundings_meshes[0]);swat_sim_close(&replica);
    swat_sim_reset(&sim);assert(sim.world.count==SWAT_MOTEL_OBJECTS && sim.world.surroundings_meshes[14]);swat_sim_close(&sim);
    for(int i=0;i<15;i++)assert(!sim.world.surroundings_meshes[i]);
    puts("PASS surroundings: physical squad movement on both outer edges, encoded map/snapshot exact reconstruction, modified-map rejection and reset/close ownership");
    return 0;
}
