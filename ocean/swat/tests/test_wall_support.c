#include "protocol.h"
#include "replay.h"
#include "net.h"
#include "enet/enet.h"
#include "fracture_geometry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica,late;
static SwatMap map,decoded_map;
static SwatSnapshot state,decoded,before,after,bad;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static int overflows;
#include "wall_support_fixture.h"
static void geometry(const SwatWorld* a,const SwatWorld* b) {
    assert(swat_world_geometry_count(a)==swat_world_geometry_count(b));uint32_t rng=913;
    for(int i=0;i<swat_world_geometry_count(a);i++) {
        const SwatObject* p=swat_world_geometry(a,i),*q=swat_world_geometry(b,i);
        assert(p->active==q->active);if(!p->active || (!p->support_role && !p->rubble))continue;
        if(swat_fracture_hash(p)!=swat_fracture_hash(q)) {
            fprintf(stderr,"Geometry mismatch owner%d kind%d cuts%d/%d sections%d/%d filtered%d/%d health%.9g/%.9g\n",i,p->tag.kind,p->cuts?p->cuts->count:-1,q->cuts?q->cuts->count:-1,p->cuts?p->cuts->section_count:-1,q->cuts?q->cuts->section_count:-1,p->cuts?p->cuts->support_filtered:0,q->cuts?q->cuts->support_filtered:0,p->health,q->health);
            if(p->cuts && q->cuts)for(int s=0;s<p->cuts->section_count;s++)if(memcmp(&p->cuts->sections[s],&q->cuts->sections[s],sizeof(p->cuts->sections[s]))) {
                fprintf(stderr," First differing section%d count%d/%d edges%u/%u\n",s,p->cuts->sections[s].count,q->cuts->sections[s].count,p->cuts->sections[s].exposed_edges,q->cuts->sections[s].exposed_edges);break;
            }
            assert(false);
        }
        for(int k=0;k<8;k++) {
            float y=p->half.y*(2*swat_rand01(&rng)-1),z=p->half.z*(2*swat_rand01(&rng)-1);
            b3Quat rotation=swat_object_rotation(p);b3Pos from=b3OffsetPos(p->center,b3RotateVector(rotation,swat_v(-p->half.x-.1f,y,z)));
            b3Vec3 delta=b3RotateVector(rotation,swat_v(2*p->half.x+.2f,0,0));
            bool solid=swat_world_object_ray(p,from,delta);
            assert(solid==swat_fracture_contains(p,y,z));assert(solid==swat_world_object_ray(q,from,delta));
        }
    }
}
static void reject(void) {
    swat_capture_snapshot(&replica,1,&before);assert(!swat_apply_snapshot(&replica,&bad));
    swat_capture_snapshot(&replica,1,&after);assert(!memcmp(&before,&after,sizeof(before)));geometry(&sim.world,&replica.world);
}
static void physics_log(const char* message){if(strstr(message,"triangle buffer capacity")){overflows++;fprintf(stderr,"%s\n",message);}}
static void synchronize(bool fresh) {
    if(fresh){swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);assert(n && swat_decode_map(&decoded_map,bytes,n));swat_apply_map(&replica,&decoded_map);}
    swat_capture_snapshot(&sim,1,&state);size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n));
    if(!swat_apply_snapshot(&replica,&decoded)){fprintf(stderr,"Wall replica rejected at tick%d, %d rubble slots\n",sim.tick,state.rubble_count);assert(false);}
    assert(swat_replay_digest(&sim)==swat_replay_digest(&replica));
    if(fresh || !(sim.tick%30))geometry(&sim.world,&replica.world);
}
static void udp(void) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_BREACH_LAB;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);
    SwatNetServer host={0};SwatNetClient client={0};int port=30000+(int)(enet_time_get()%20000),attempt=0;
    while(!swat_server_open(&host,&sim,port,true) && ++attempt<32)port++;
    int group=-1;for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].support_role==SWAT_SUPPORT_MEMBER){group=sim.world.objects[i].wall_group;break;}
    assert(group>0);wall_support_ring(&sim.world,group,1.3f,0,.5f,.7f);
    assert(host.transport && swat_client_open(&client,&late,"127.0.0.1",port));
    uint32_t start=enet_time_get();
    while(enet_time_get()-start<5000 && (client.status!=SWAT_NET_ACTIVE || late.world.rubble==NULL)) {
        swat_server_poll(&host);swat_client_poll(&client);
        enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;enet_socket_wait(((ENetHost*)host.transport)->socket,&condition,1);
    }
    if(client.status!=SWAT_NET_ACTIVE || !late.world.rubble)fprintf(stderr,"Wall UDP join status%d map%d error: %s; slot%d servermask%u ticks%d/%d\n",client.status,client.has_map,client.error,client.slot,host.player_mask,sim.tick,late.tick);
    assert(client.status==SWAT_NET_ACTIVE && late.world.rubble);geometry(&sim.world,&late.world);
    swat_client_close(&client);swat_server_close(&host);swat_sim_close(&late);swat_sim_close(&sim);
    puts("PASS wall rubble UDP: real socket join carries original owners, retained support geometry and every physical chunk");
}
static void topology(void) {
    static SwatWorld world;swat_world_init(&world);
    const b3Pos centres[]={{0,1.5,0},{0,2.5,0},{0,3.5,1},{0,1.5,1},{0,1.5,2}};
    for(int i=0;i<5;i++) {
        int owner=swat_world_box(&world,centres[i],swat_v(.05f,.5f,.5f),i==3?SWAT_INSULATION:i?SWAT_DRYWALL:SWAT_WOOD,i?100:0);
        world.objects[owner].wall_group=1;world.objects[owner].part=i?SWAT_PART_SKIN:SWAT_PART_SUPPORT;
    }
    swat_world_support_enable(&world,1);
    SwatSupportPlan* plan=swat_world_support_prepare(&world,NULL,swat_world_support_mask(&world,1));assert(plan);swat_world_support_commit(&world,plan,true);
    assert(world.objects[0].active && world.objects[1].active && !world.objects[2].active && world.objects[3].active && !world.objects[4].active);
    assert(world.rubble && world.rubble->count==2);swat_world_close(&world);
    puts("PASS wall support topology: a shared sheet edge carries support; point contact and insulation cannot bridge disconnected material");
}
static void run(int mission) {
    SwatConfig cfg=swat_default_config();cfg.mission=mission;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=30000;
    swat_sim_init(&sim,cfg,81);sim.extraction=(b3Pos){500,0,500};
    for(int i=1;i<sim.actor_count;i++)if(sim.actors[i].present){b3DestroyBody(sim.actors[i].controller.body.body);memset(&sim.actors[i],0,sizeof(sim.actors[i]));}
    int group=-1;float centre_z=0,plane=4;
    if(mission==SWAT_MOTEL){group=sim.world.objects[106].wall_group;centre_z=-3;plane=sim.world.objects[106].center.x;}
    else for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].support_role==SWAT_SUPPORT_MEMBER){group=sim.world.objects[i].wall_group;break;}
    assert(group>0);int owners=sim.world.count;
    assert(swat_world_ray(&sim.world,(b3Pos){plane-1,1.3f,centre_z},swat_v(1,0,0),2,b3_nullBodyId).hit);
    wall_support_ring(&sim.world,group,1.3f,centre_z,.5f,.7f);
    int active=0;float area=0;
    if(sim.world.rubble)for(int i=0;i<sim.world.rubble->count;i++)if(sim.world.rubble->pieces[i].object.active){active++;for(int p=0;p<sim.world.rubble->pieces[i].state.section_count;p++)area+=swat_polygon_area(&sim.world.rubble->pieces[i].state.sections[p]);}
    fprintf(stderr,"Wall mission%d group%d: %d rubble pieces, %.6fm2 detached\n",mission,group,active,area);
    assert(active>0 && active<=SWAT_RUBBLE_CAPACITY && area>.2f && sim.world.count==owners);
    synchronize(true);
    for(int t=0;t<600;t++){swat_sim_step(&sim,&(SwatInput){0});synchronize(false);}
    int awake=0;for(int i=0;i<sim.world.rubble->count;i++)awake+=sim.world.rubble->pieces[i].object.active && b3Body_IsAwake(sim.world.rubble->pieces[i].object.body);
    fprintf(stderr,"Wall mission%d settled awake=%d\n",mission,awake);assert(!awake);
    SwatHit air=swat_world_ray(&sim.world,(b3Pos){plane-1,1.3f,centre_z},swat_v(1,0,0),2,b3_nullBodyId);
    if(air.hit)fprintf(stderr,"Settled physical debris at aperture: kind%d index%d at %.5f,%.5f,%.5f\n",air.kind,air.index,(float)air.point.x,(float)air.point.y,(float)air.point.z);
    assert(!air.hit || air.kind==SWAT_HIT_RUBBLE); // Settled rubble can obstruct a real opening.
    assert(swat_world_ray(&sim.world,(b3Pos){plane-1,2.4f,centre_z},swat_v(1,0,0),2,b3_nullBodyId).hit);
    swat_capture_map(&sim,1,&map);swat_apply_map(&late,&map);assert(swat_apply_snapshot(&late,&decoded));assert(swat_replay_digest(&late)==swat_replay_digest(&sim));geometry(&sim.world,&late.world);
    bad=state;bad.rubble[0].rotation.s*=1.0001f;reject();
    bad=state;bad.rubble[0].sections[0].vertices[0][0]=500;reject();
    bad=state;bad.rubble[1].serial=bad.rubble[0].serial;reject();
    bad=state;bad.rubble[0].health+=1;reject();
    uint32_t serial=sim.world.rubble->serial;
    wall_support_ring(&sim.world,group,1.3f,centre_z-1.12f,.5f,.27f);
    wall_support_ring(&sim.world,group,1.3f,centre_z+1.12f,.5f,.27f);
    synchronize(false);assert(sim.world.rubble->serial>serial && sim.world.rubble->count<=SWAT_RUBBLE_CAPACITY);
    if(mission==SWAT_BREACH_LAB)assert(sim.world.rubble->serial>SWAT_RUBBLE_CAPACITY);
    serial=sim.world.rubble->serial;
    wall_support_ring(&sim.world,group,1.3f,centre_z-1.12f,.5f,.27f);
    wall_support_ring(&sim.world,group,1.3f,centre_z+1.12f,.5f,.27f);
    assert(sim.world.rubble->serial==serial);synchronize(false);
    printf("PASS repeated wall damage mission%d: %u detached pieces through %d bounded slots, old cuts stay empty and repeated cuts spawn no duplicates\n",mission,serial,sim.world.rubble->count);
    for(int i=0;i<sim.world.rubble->count;i++)if(sim.world.rubble->pieces[i].object.active) {
        SwatRubble* r=&sim.world.rubble->pieces[i];
        SwatRubbleState removed=r->state;
        assert(swat_world_hit_impact(&sim.world,(SwatHit){.kind=SWAT_HIT_RUBBLE,.index=i,.point=b3Body_GetWorldCenter(r->object.body),.normal={1,0,0}},r->state.health+1,false));
        synchronize(false);bad=state;bad.rubble[i]=removed;reject();break;
    }
    printf("PASS wall support mission%d: connected retained sheets/frame, isolated island falls, anchored upper material remains, %d physical pieces, floor sleep, exact live/late replicas and atomic malformed pose rejection\n",mission,active);
    swat_sim_close(&late);swat_sim_close(&replica);swat_sim_close(&sim);
}
static SwatReplay journal;
static void recorded_step(SwatInput in) {swat_sim_step(&sim,&in);assert(sim.end==SWAT_RUNNING);assert(swat_replay_append(&journal,&in,&sim));}
static void recorded_idle(int ticks) {for(int t=0;t<ticks;t++)recorded_step(swat_neutral_input());}
static SwatInput recorded_look(b3Pos target) {
    SwatController* c=&sim.actors[0].controller;SwatInput in=swat_neutral_input();b3Vec3 d=b3SubPos(target,swat_controller_eye(c));
    in.yaw_delta=swat_clamp(swat_angle(atan2f(d.z,d.x)-c->yaw-c->recoil_yaw),-6*SWAT_RAD,6*SWAT_RAD);
    in.pitch_delta=swat_clamp(atan2f(d.y,hypotf(d.x,d.z))-c->pitch-c->recoil_pitch,-6*SWAT_RAD,6*SWAT_RAD);return in;
}
static void recorded_walk(b3Pos goal) {
    for(int t=0;t<900;t++) {
        SwatController* c=&sim.actors[0].controller;b3Vec3 d=b3SubPos(goal,swat_body_feet_position(&c->body));
        if(hypotf(d.x,d.z)<.07f){recorded_idle(15);return;}
        SwatInput in=swat_neutral_input();float cy=cosf(c->yaw),sy=sinf(c->yaw);
        in.forward=swat_clamp((cy*d.x+sy*d.z)*1.5f,-.8f,.8f);in.strafe=swat_clamp((-sy*d.x+cy*d.z)*1.5f,-.8f,.8f);recorded_step(in);
    }
    fprintf(stderr,"Wall input walk stopped at %.4f,%.4f\n",(float)swat_body_feet_position(&sim.actors[0].controller.body).x,(float)swat_body_feet_position(&sim.actors[0].controller.body).z);assert(false);
}
static void recorded_shot(b3Pos target) {
    for(int t=0;t<14;t++){SwatInput in=recorded_look(target);in.aim=true;recorded_step(in);}
    int first=sim.actors[0].arsenal.shots;
    for(int t=0;t<600 && sim.actors[0].arsenal.shots==first;t++) {
        SwatInput in=recorded_look(target);in.aim=true;SwatWeapon* weapon=&sim.actors[0].arsenal.slots[0];
        in.fire=!sim.actors[0].arsenal.last_fire && !weapon->reload_remaining;in.reload=!weapon->chambered && !weapon->reload_remaining;recorded_step(in);
    }
    assert(sim.actors[0].arsenal.shots==first+1);
}
static void ordinary_input(const char* path) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_BREACH_LAB;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=30000;
    swat_sim_init(&sim,cfg,81);assert(swat_replay_record(&journal,path,&sim));recorded_walk((b3Pos){1.8f,0,.4f});
    for(int side=0;side<4;side++) {
        int samples=side<2?12:22;
        for(int k=0;k<samples;k++) {
            float t=(float)k/(samples-1);b3Pos target={3.94f,side<2?(side?1.60f:1.18f):1.16f+.46f*t,side<2?.28f+.24f*t:side==2?.30f:.50f};
            recorded_shot(target);
        }
    }
    fprintf(stderr,"Wall ordinary outline: shots%d rubble%u slots%d\n",sim.actors[0].arsenal.shots,sim.world.rubble?sim.world.rubble->serial:0,sim.world.rubble?sim.world.rubble->count:0);
    assert(sim.world.rubble && sim.world.rubble->serial);
    char save[4096];assert(snprintf(save,sizeof(save),"%s.save",path)<(int)sizeof(save));
    int awake=0;for(int i=0;i<sim.world.rubble->count;i++)awake+=sim.world.rubble->pieces[i].object.active && b3Body_IsAwake(sim.world.rubble->pieces[i].object.body);
    assert(awake>0);
    int saved_tick=sim.tick;assert(swat_replay_checkpoint(&journal,save));
    recorded_idle(600);
    recorded_walk((b3Pos){2.8f,0,.4f});b3Pos mount={3.94f,1.02f,.72f};
    for(int t=0;t<30;t++)recorded_step(recorded_look(mount));
    int stock=sim.actors[0].gear.wall_charges,owner=-1;
    for(int t=0;t<120 && owner<0;t++) {
        SwatInput in=recorded_look(mount);in.door_tool=SWAT_PLACE_CHARGE;recorded_step(in);
        for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].breach_owner==0)owner=i;
    }
    assert(owner>=0 && sim.actors[0].gear.wall_charges==stock-1);
    recorded_walk((b3Pos){.6f,0,.4f});SwatInput remote=swat_neutral_input();remote.door_tool=SWAT_DETONATE_CHARGE;recorded_step(remote);recorded_idle(600);
    b3Pos next;assert(swat_navigation_next_for_actor(&sim,0,(b3Pos){5.3f,0,.72f},&next));int navigation_tick=sim.navigation->built_tick;
    recorded_walk((b3Pos){5.3f,0,.72f});assert(swat_body_feet_position(&sim.actors[0].controller.body).x>5.2f);
    assert(swat_navigation_next_for_actor(&sim,0,(b3Pos){2.8f,0,.72f},&next) && sim.navigation->built_tick==navigation_tick);
    assert(swat_replay_close(&journal));uint32_t final=swat_replay_digest(&sim);int frames=sim.tick;
    SwatReplay reader;assert(swat_replay_open(&reader,path));swat_sim_init(&replica,reader.config,reader.seed);
    SwatInput in;uint32_t expected;int status;
    while((status=swat_replay_next(&reader,&in,&expected))==1){swat_sim_step(&replica,&in);assert(swat_replay_digest(&replica)==expected);}
    assert(!status && swat_replay_close(&reader) && final==swat_replay_digest(&replica));geometry(&sim.world,&replica.world);
    char error[256];assert(swat_replay_restore(&late,save,error,sizeof(error)) && late.tick==saved_tick);assert(swat_replay_open(&reader,path));
    while((status=swat_replay_next(&reader,&in,&expected))==1)if((int)reader.frame>saved_tick){swat_sim_step(&late,&in);assert(swat_replay_digest(&late)==expected);}
    assert(!status && swat_replay_close(&reader) && final==swat_replay_digest(&late));geometry(&sim.world,&late.world);
    printf("PASS ordinary-input wall support: %d finite rifle shots detach material, falling-state save/resume, finite mounted charge/retreat/remote breach and physical crossing; %d exact replay ticks\n",sim.actors[0].arsenal.shots,frames);
    swat_sim_close(&late);swat_sim_close(&replica);swat_sim_close(&sim);
}
int main(int argc,char** argv){assert(argc<=2);b3SetLogFcn(physics_log);topology();run(SWAT_BREACH_LAB);run(SWAT_MOTEL);udp();ordinary_input(argc>1?argv[1]:"wall-support-test.sgrp");assert(!overflows);return 0;}
