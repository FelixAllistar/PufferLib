#include "protocol.h"
#include "motel.h"
#include "replay.h"
#include <assert.h>
#include <stdio.h>
static SwatSim sim,replica;
static SwatMap map,decoded;
static SwatSnapshot snapshot;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static SwatReplay journal;
// Explicitly exercise the pre-R1 map/snapshot recipe. Replays use the current recipe.
static void legacy_world(void){
    static SwatMap old;static SwatSnapshot snapshot;
    swat_capture_map(&sim,1,&old);swat_capture_snapshot(&sim,1,&snapshot);
    old.count=SWAT_MOTEL_WINDOWS_FIRST;snapshot.object_count=old.count;
    const int parents[]={6,17,41,65,89};
    for(int i=0;i<5;i++)snapshot.objects[parents[i]].active=true;
    for(int i=0;i<10;i++){int id=SWAT_MOTEL_PANES_FIRST+i;snapshot.objects[id].active=true;snapshot.objects[id].health=old.objects[id].max_health;}
    swat_apply_map(&sim,&old);assert(sim.world.motel && swat_apply_snapshot(&sim,&snapshot));
}
static void reset_legacy(void){swat_sim_reset(&sim);legacy_world();}
static SwatInput look(b3Pos target) {
    SwatController* c=&sim.actors[0].controller;b3Vec3 d=b3SubPos(target,swat_controller_eye(c));SwatInput in=swat_neutral_input();
    in.yaw_delta=swat_clamp(swat_angle(atan2f(d.z,d.x)-c->yaw),-.1f,.1f);
    in.pitch_delta=swat_clamp(atan2f(d.y,hypotf(d.x,d.z))-c->pitch,-.1f,.1f);return in;
}
static void record_step(SwatInput input) {swat_sim_step(&sim,&input);assert(swat_replay_append(&journal,&input,&sim));}
static void walk(b3Pos target) {
    b3Pos waypoint=target;int replan=0,t=0;
    for(;t<1200;t++) {
        b3Pos feet=swat_body_feet_position(&sim.actors[0].controller.body);b3Vec3 d=b3SubPos(target,feet);if(hypotf(d.x,d.z)<.43f)break;
        if(t>=replan || b3Distance(feet,waypoint)<.3f){assert(swat_navigation_next_for_actor(&sim,0,target,&waypoint));replan=t+30;}
        SwatInput input=look(b3OffsetPos(waypoint,swat_v(0,1.65f,0)));if(fabsf(input.yaw_delta)<.07f)input.forward=.8f;record_step(input);
    }
    assert(t<1200);
}
static b3Pos point(int parent,float x,float y,float z) {
    const SwatMotelInstance* p=swat_motel_instance(parent-1);
    return b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*x+sinf(p->yaw)*z,y,-sinf(p->yaw)*x+cosf(p->yaw)*z));
}
static b3Vec3 normal(int parent,int side) {
    float yaw=swat_motel_instance(parent-1)->yaw;return swat_v(sinf(yaw)*(side?-1:1),0,cosf(yaw)*(side?-1:1));
}
static void sync(void) {
    swat_capture_map(&sim,1,&map);size_t size=swat_encode_map(bytes,sizeof(bytes),&map);assert(size && swat_decode_map(&decoded,bytes,size));
    swat_apply_map(&replica,&decoded);assert(replica.world.motel);
    swat_capture_snapshot(&sim,1,&snapshot);assert(swat_apply_snapshot(&replica,&snapshot));
    for(int i=0;i<10;i++)assert(replica.world.objects[SWAT_MOTEL_PANES_FIRST+i].active==sim.world.objects[SWAT_MOTEL_PANES_FIRST+i].active);
}
static void panes(void) {
    const int parents[]={6,17,41,65,89};int rays=0;
    for(int bay=0;bay<5;bay++)for(int side=0;side<2;side++) {
        reset_legacy();int parent=parents[bay];
        for(int pane=0;pane<2;pane++) {
            int id=SWAT_MOTEL_PANES_FIRST+2*bay+pane;SwatObject* o=&sim.world.objects[id];
            assert(swat_motel_pane_parent(&sim.world,o)==parent && swat_world_breachable(o));
            float x=bay?(pane?.25f:-.25f):(pane?1.31f:0),y=bay?.85f:1.7f;
            b3Pos from=point(parent,x,y,side?.15f:-.15f);b3Vec3 dir=normal(parent,side);
            SwatHit hit=swat_world_ray(&sim.world,from,dir,.3f,b3_nullBodyId);
            assert(hit.hit && hit.index==id);
            SwatHit sight=swat_world_sight_ray(&sim.world,from,dir,.3f,b3_nullBodyId);assert(sight.hit && sight.index==id);
            float thickness=swat_world_exit_distance(o,hit.point,dir);assert(fabsf(thickness-o->structural_thickness)<1e-5f);
            assert(b3Shape_GetMesh(o->shape).data->triangleCount==44);
            swat_sim_shoot(&sim,0,from,dir,(SwatShot){.fired=true,.damage=34,.range=.3f,.energy=6});
            assert(!o->active && sim.world.objects[parent].active);
            assert(!swat_world_ray(&sim.world,from,dir,.3f,b3_nullBodyId).hit);
            assert(!swat_world_sight_ray(&sim.world,from,dir,.3f,b3_nullBodyId).hit);rays++;
            if(!pane)assert(sim.world.objects[id+1].active);
        }
        assert(swat_motel_pane_mask(&sim.world,parent)==0);sync();
        // A frame/sill cannot disappear when its pane breaks.
        SwatHit frame=swat_world_ray(&sim.world,point(parent,bay?-.98f:1.97f, .65f,.3f),normal(parent,1),.6f,b3_nullBodyId);
        assert(frame.hit && frame.index==parent);
    }
    // Curtain folds overlap the pane proxy. A shot must encounter BOTH materials.
    for(int side=0;side<2;side++) {
        reset_legacy();int id=SWAT_MOTEL_PANES_FIRST+2;
        b3Pos from=point(17,-.72f,.85f,side?.15f:-.15f);b3Vec3 dir=normal(17,side);
        swat_sim_shoot(&sim,0,from,dir,(SwatShot){.fired=true,.damage=34,.range=.3f,.energy=6});
        assert(!sim.world.objects[id].active && sim.world.objects[17].active);
        SwatHit hit=swat_world_ray(&sim.world,from,dir,.3f,b3_nullBodyId);assert(hit.hit && hit.index==17);
        assert(swat_world_sight_ray(&sim.world,from,dir,.3f,b3_nullBodyId).hit);
    }
    printf("PASS motel panes: %d two-face shots/exit depths, opaque intact sight, independent glass destruction, retained frames and overlapping curtains, encoded late replicas\n",rays);
}
static void charge(void) {
    reset_legacy();int id=SWAT_MOTEL_PANES_FIRST;
    assert(swat_world_breach(&sim.world,id,point(6,0,1.5f,0))==1);
    assert(!sim.world.objects[id].active && sim.world.objects[id+1].active && sim.world.objects[6].active);
    sync();
    // A crouched capsule clears the aperture ABOVE the surviving crossrail.
    // This measures clearance; it does not add a player vault/climb action.
    b3Capsule capsule={.center1={0,.22f,0},.center2={0,.92f,0},.radius=.2f};
    b3Pos from=point(6,0,.8f,.55f);b3Vec3 delta=normal(6,1);delta=swat_mul(delta,1.1f);
    float open=b3World_CastMover(sim.world.id,from,&capsule,delta,b3DefaultQueryFilter(),NULL,NULL);
    reset_legacy();
    float closed=b3World_CastMover(sim.world.id,from,&capsule,delta,b3DefaultQueryFilter(),NULL,NULL);
    assert(open>.999f && closed<.6f);
    puts("PASS motel pane charge: local aperture, neighboring pane/frame retained, crouched capsule clearance above surviving lobby crossrail");
}
static void validation(void) {
    reset_legacy();sync();int id=SWAT_MOTEL_PANES_FIRST;
    map.objects[id].material=SWAT_GLASS;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync();map.objects[id].half.x+=.001f;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync();map.count=id+1;swat_apply_map(&replica,&map);assert(!replica.world.motel);
    sync();snapshot.objects[6].active=false;assert(!swat_apply_snapshot(&replica,&snapshot));
    sync();map.count=id;swat_apply_map(&replica,&map);assert(replica.world.motel);
    SwatHit legacy=swat_world_ray(&replica.world,point(6,0,1.7f,.15f),normal(6,1),.3f,b3_nullBodyId);assert(legacy.hit && legacy.index==6);
    puts("PASS motel panes: legacy aggregate prefix retained; partial recipes, changed opacity/depth and unsupported snapshots rejected");
}
static void live_charge(void) {
    reset_legacy();SwatController* c=&sim.actors[0].controller;
    b3Body_SetTransform(c->body.body,b3OffsetPos(point(6,0,0,1.05f),swat_v(0,c->body.totalHeight*.5f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,b3Vec3_zero);c->yaw=-SWAT_PI*.5f;c->pitch=0;
    SwatInput input=swat_neutral_input();for(int i=0;i<10;i++)swat_sim_step(&sim,&input);
    int id=SWAT_MOTEL_PANES_FIRST;SwatHit target=swat_context_hit(&sim,0,1.7f);assert(target.hit && target.index==id);
    sim.actors[0].gear.breaching_charges=1;input.door_tool=SWAT_PLACE_CHARGE;
    for(int i=0;i<100;i++)swat_sim_step(&sim,&input);
    assert(sim.world.objects[id].breach_owner==0 && sim.actors[0].gear.breaching_charges==0);
    input=swat_neutral_input();input.forward=-1;for(int i=0;i<150;i++)swat_sim_step(&sim,&input);
    input=swat_neutral_input();input.door_tool=SWAT_DETONATE_CHARGE;swat_sim_step(&sim,&input);
    assert(!sim.world.objects[id].active && sim.world.objects[id+1].active && sim.world.objects[6].active && sim.actors[0].health==100);
    sync();puts("PASS motel panes: ordinary C4 placement, finite inventory, retreat and detonation; adjacent glass and frames retained");
}
static void live_penetration(void) {
    reset_legacy();int id=SWAT_MOTEL_PANES_FIRST+2;
    swat_sim_spawn_actor(&sim,1,SWAT_CIVILIAN,point(17,-.25f,0,.65f),0);
    b3Pos from=point(17,-.25f,.85f,-.15f);b3Vec3 direction=normal(17,0);
    assert(swat_world_sight_ray(&sim.world,from,direction,1,b3_nullBodyId).index==id);
    swat_sim_shoot(&sim,0,from,direction,(SwatShot){.fired=true,.damage=34,.range=1,.energy=6});
    assert(!sim.world.objects[id].active && sim.actors[1].health<100 && sim.actors[1].health>66);
    SwatHit sight=swat_world_sight_ray(&sim.world,from,direction,1,b3_nullBodyId);assert(sight.kind==SWAT_HIT_ACTOR && sight.index==1);
    puts("PASS motel panes: actor obscured by intact opaque glass, physical bullet penetration consumes glass energy and exposes the broken sightline");
}
static void replay_shooting(const char* path) {
    swat_sim_close(&sim);SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;cfg.squad_bots=0;cfg.max_ticks=12000;
    swat_sim_init(&sim,cfg,81);assert(swat_replay_record(&journal,path,&sim));
    walk((b3Pos){9,-.079f,8});walk((b3Pos){9,-.079f,.9f});
    b3Pos approach=point(89,-.25f,0,1.1f);approach.y=-.079f;walk(approach);
    int id=swat_motel_window_find(&sim.world,89,"room_glass_left",SWAT_OPAQUE_GLASS);
    int right=swat_motel_window_find(&sim.world,89,"room_glass_right",SWAT_OPAQUE_GLASS);assert(id>=0 && right>=0);
    b3Pos target=sim.world.objects[id].center;
    for(int t=0;t<45;t++){SwatInput input=look(target);input.aim=true;record_step(input);}
    int t=0;for(;t<240 && sim.world.objects[id].active;t++) {
        SwatInput input=look(target);input.aim=true;input.fire=t%12==0;record_step(input);
    }
    assert(t<240 && sim.world.objects[right].active && sim.world.objects[89].active);assert(swat_replay_close(&journal));
    uint32_t digest=swat_replay_digest(&sim);char error[256];assert(swat_replay_restore(&replica,path,error,sizeof(error)));
    assert(swat_replay_digest(&replica)==digest && replica.world.motel && !replica.world.objects[id].active);
    assert(remove(path)==0);puts("PASS motel panes: normal walking/aim/fire from canonical spawn, exact destroyed-pane replay/save restore");
}
int main(int argc,char** argv) {
    assert(argc==2);
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);legacy_world();assert(sim.world.count==SWAT_MOTEL_WINDOWS_FIRST);
    panes();charge();validation();live_charge();live_penetration();replay_shooting(argv[1]);swat_sim_close(&replica);swat_sim_close(&sim);return 0;
}
