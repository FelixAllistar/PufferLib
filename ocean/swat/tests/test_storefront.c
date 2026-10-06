#include "protocol.h"
#include "storefront.h"
#include <assert.h>
#include <stdio.h>
static SwatSim server,replica;
static SwatMap map,decoded;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static void routes(SwatWorld* w) {
    // Original source QA routes, with 25 mm clearance above the authored
    // <=20 mm thresholds. Source XYZ -> engine X,Z,-Y once.
    const b3Pos paths[][5]={{{-3,-1,0},{-3,1.4f,0},{-3,4.6f,0},{-3,6.8f,0},{-3,8,0}},
        {{3,-1,0},{3,1.4f,0},{3,4.6f,0},{3,6.8f,0},{3,8,0}},
        {{3,8,0},{-3,8,0}},{{-3,8,0},{-3,9.5f,0},{-3,11.3f,0}},{{-3,-1,0},{3,-1,0}}};
    const int counts[]={5,5,2,3,2};
    b3Capsule capsule={.center1={0,.325f,0},.center2={0,1.525f,0},.radius=.3f};
    for(int p=0;p<5;p++) for(int i=1;i<counts[p];i++) {
        b3Pos from={paths[p][i-1].x,paths[p][i-1].z,-paths[p][i-1].y},to={paths[p][i].x,paths[p][i].z,-paths[p][i].y};
        float f=b3World_CastMover(w->id,from,&capsule,b3SubPos(to,from),b3DefaultQueryFilter(),NULL,NULL);
        printf("storefront route %d/%d: %.6f\n",p,i,f);fflush(stdout);assert(f>.999f);
    }
}
int main(void) {
    SwatConfig config=swat_default_config(); config.mission=SWAT_STOREFRONT;config.randomize=false;config.hostile_fire=false;
    swat_sim_init(&server,config,42); SwatWorld* w=&server.world;
    assert(w->storefront && !w->motel && w->count==153 && w->room_count==3);
    int doors=0,entry=-1;
    for(int i=1;i<w->count;i++) if(w->objects[i].door) { doors++;if(entry<0)entry=i; }
    assert(doors==5);SwatObject* d=&w->objects[entry];assert(d->locked);
    assert(swat_world_ray(w,(b3Pos){-3,1.1f,1},swat_v(0,0,-1),2,b3_nullBodyId).index==entry);
    // Serialized resting map reconstructs non-box openings and mesh ownership.
    swat_capture_map(&server,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded,bytes,n));
    swat_apply_map(&replica,&decoded);assert(replica.world.storefront);
    for(int i=1;i<w->count;i++) if(!w->objects[i].door) assert(b3Shape_GetType(replica.world.objects[i].shape)==b3_meshShape);
    swat_sim_close(&replica);
    for(int i=1;i<w->count;i++) if(w->objects[i].door) { w->objects[i].locked=false;w->objects[i].door_open=true; }
    for(int i=0;i<60;i++) swat_world_step_doors(w);
    assert(!swat_world_ray(w,(b3Pos){-3,1.1f,1},swat_v(0,0,-1),2,b3_nullBodyId).hit);
    for(int i=1;i<server.actor_count;i++) if(server.actors[i].present) {
        b3DestroyBody(server.actors[i].controller.body.body);server.actors[i].present=false;
    }
    server.actor_count=1;
    routes(w);
    // Grounding and walking are tested through the actual controller rather
    // than accepting a capsule route as proof of threshold stepping.
    swat_sim_spawn_actor(&server,0,SWAT_OFFICER,(b3Pos){-3,0,1},-SWAT_PI*.5f);
    SwatInput input=swat_neutral_input();input.forward=1;
    for(int i=0;i<180;i++) swat_sim_step(&server,&input);
    b3Pos feet=swat_body_feet_position(&server.actors[0].controller.body);
    printf("storefront controller feet %.3f %.3f %.3f\n",(float)feet.x,(float)feet.y,(float)feet.z);
    assert(feet.z<-2 && feet.y>-.05f && feet.y<.15f);
    swat_capture_map(&server,2,&map);n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded,bytes,n));
    swat_apply_map(&replica,&decoded);assert(replica.world.storefront);swat_sim_close(&replica);
    decoded.objects[2].center.x+=.1f;swat_apply_map(&replica,&decoded);assert(!replica.world.storefront);
    for(int i=0;i<40;i++) assert(!replica.world.storefront_meshes[i]);swat_sim_close(&replica);
    assert(swat_world_damage(w,entry,1000) && !w->objects[entry].active);
    swat_sim_reset(&server);assert(server.world.storefront);swat_sim_close(&server);
    assert(!server.world.storefront && !server.world.storefront_meshes[0]);
    puts("PASS storefront: original triangles/site, five hinged locked doors, five capsule routes, physical threshold traversal, serialized open/rest geometry and ownership");return 0;
}
