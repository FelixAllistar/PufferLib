#include "protocol.h"
#include "replay.h"
#include "motel.h"
#include "door_damage.h"
#include "fracture_geometry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim,replica,late;
static SwatMap map;
static SwatSnapshot state,decoded,bad,before,after;
static unsigned char bytes[SWAT_NET_PACKET_MAX];
static FILE* trace;
static int overflows;
static double max_anchor_drift;
static void physics_log(const char* message) {
    if(strstr(message,"triangle buffer capacity")){overflows++;fprintf(stderr,"%s\n",message);}
}
static b3Pos leaf_point(b3Vec3 p) {
    const SwatObject* o=&sim.world.objects[16];
    return b3OffsetPos(o->center,b3RotateVector(swat_object_rotation(o),p));
}
static void fixture(float angle,bool latched) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);sim.extraction=(b3Pos){500,0,500};
    for(int i=1;i<sim.actor_count;i++)if(sim.actors[i].present) {
        b3DestroyBody(sim.actors[i].controller.body.body);memset(&sim.actors[i],0,sizeof(sim.actors[i]));
    }
    SwatObject* o=&sim.world.objects[16];
    if(!latched)assert(swat_world_break_latch(&sim.world,16));
    o->door_angle=angle;o->door_open=angle>0;o->yaw=o->closed_yaw+angle;
    o->center=swat_door_center(o,o->yaw);b3Body_SetTransform(o->body,o->center,swat_object_rotation(o));
    assert(!swat_motel_hinge_frame_broken(&sim.world,sim.world.objects) && !o->motion && !o->cuts);
    swat_capture_map(&sim,1,&map);swat_apply_map(&replica,&map);
    assert(replica.world.motel);
}
static void synchronize(void) {
    swat_capture_snapshot(&sim,1,&state);
    size_t n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n));
    if(!swat_apply_snapshot(&replica,&decoded)) {
        const SwatObject* o=&sim.world.objects[16];
        fprintf(stderr,"Frame replica rejected tick%d angle%.9g mask%u support%d\n",sim.tick,o->door_angle,
            o->motion?o->motion->state.broken_hinges:0,o->motion?o->motion->support:-1);assert(false);
    }
    uint32_t digest=swat_replay_digest(&sim);assert(digest==swat_replay_digest(&replica));
    if(trace)fprintf(trace,"%08x\n",digest);
}
static void reject(void) {
    swat_capture_snapshot(&replica,1,&before);
    assert(!swat_apply_snapshot(&replica,&bad));
    swat_capture_snapshot(&replica,1,&after);assert(!memcmp(&before,&after,sizeof(before)));
}
static void fresh(void) {
    // A late peer receives canonical intact geometry followed by one damaged
    // snapshot, including frame cuts whose indices come after the leaf.
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(bytes,sizeof(bytes),&map);
    assert(n && swat_decode_map(&map,bytes,n));swat_apply_map(&late,&map);
    swat_capture_snapshot(&sim,1,&state);n=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    assert(n && swat_decode_snapshot(&decoded,bytes,n) && swat_apply_snapshot(&late,&decoded));
    assert(swat_replay_digest(&sim)==swat_replay_digest(&late));swat_sim_close(&late);
}
static b3Pos mount(int h) {
    const float heights[]={.23f,1.10f,1.92f};const SwatObject* o=&sim.world.objects[16];
    return b3OffsetPos(o->hinge,b3RotateVector(swat_door_rotation(o->closed_yaw,0),swat_v(-.008f,heights[h]-o->half.y,-.05f)));
}
static void cut_frame(int h,float dy) {
    int owner=swat_motel_hinge_frame_owner(&sim.world,h);assert(owner>=0);
    const SwatObject* frame=&sim.world.objects[owner];
    b3Pos p=mount(h);p.y+=dy;
    b3Vec3 local=b3InvRotateVector(swat_object_rotation(frame),b3SubPos(p,frame->center));
    assert(swat_world_cut(&sim.world,owner,(SwatCut){.y=local.y,.z=local.z,.height=.055f,.width=.035f,.seed=81,.kind=SWAT_CUT_BREACH})>0);
}
static void step(int count) {
    SwatInput neutral=swat_neutral_input();
    for(int t=0;t<count;t++){swat_sim_step(&sim,&neutral);synchronize();}
}
static void close_fixture(void) {swat_sim_close(&replica);swat_sim_close(&sim);}
static void local_damage(void) {
    fixture(0,false);synchronize();
    // Nearby damage that leaves the actual mounting point solid cannot fail it.
    cut_frame(1,.13f);assert(!sim.world.objects[16].motion);synchronize();
    b3BodyId body=sim.world.objects[16].body;b3Pos start=sim.world.objects[16].center;
    for(int h=0;h<3;h++) {
        cut_frame(h,0);SwatObject* o=&sim.world.objects[16];
        assert(!o->cuts && !swat_world_door_hinges(o));
        assert(o->motion && o->motion->state.broken_hinges==((1u<<(h+1))-1));
        assert(B3_ID_EQUALS(body,o->body) && b3Distance(start,o->center)<1e-6f);
        // Reject an omitted frame failure before accepting the real update.
        swat_capture_snapshot(&sim,1,&bad);bad.objects[16].door.broken_hinges&=~(1u<<h);reject();
        synchronize();fresh();
    }
    assert(swat_door_loose(&sim.world.objects[16]));step(180);
    assert(b3Distance(start,sim.world.objects[16].center)>.1f);fresh();close_fixture();
    puts("PASS jamb cuts: nearby holes retain support; three local backing losses release an uncut leaf immediately, retain its owner/body, fall physically, and match live/late replicas");
}
static void last_hinge(void) {
    for(int last=0;last<3;last++)for(int pose=0;pose<3;pose++) {
        fixture(pose*SWAT_PI*.25f,false);synchronize();
        SwatObject* o=&sim.world.objects[16];b3Pos start=o->center;
        for(int h=0;h<3;h++)if(h!=last){cut_frame(h,0);synchronize();}
        assert(o->motion->support==SWAT_DOOR_BOTTOM+last && b3Distance(start,o->center)<1e-6f);
        b3Vec3 local=swat_door_hinge_pivot(o,last);
        b3Transform a=b3Joint_GetLocalFrameA(o->motion->joint),b=b3Joint_GetLocalFrameB(o->motion->joint);
        assert(b3Length(b3Sub(b.p,local))<1e-6f && b3Distance(leaf_point(local),(b3Pos){a.p.x,a.p.y,a.p.z})<1e-6f);
        step(60);
        double error=b3Distance(leaf_point(local),(b3Pos){a.p.x,a.p.y,a.p.z});
        // The uncut slab can press against the floor/casing while hanging.
        // Bound the existing soft joint under contact, separately from the
        // exact coincident-frame/no-pose-jump assertions before stepping.
        if(error>max_anchor_drift)max_anchor_drift=error;
        if(error>=.01)fprintf(stderr,"Jamb last hinge%d pose%d anchor drift %.9f\n",last,pose,error);
        assert(error<.01);
        cut_frame(last,0);assert(swat_door_loose(o));synchronize();fresh();close_fixture();
    }
    puts("PASS jamb last-hinge constraints: all three surviving hinges at closed/45/90 degrees hold the original opening axis without a pose jump, then release on final backing loss");
    printf("Maximum stepped anchor drift under contact: %.6f m\n",max_anchor_drift);
}
static void latch_and_removal(void) {
    fixture(0,true);synchronize();
    SwatObject* o=&sim.world.objects[16];int group=sim.world.objects[13].wall_group,owners[3],latch_owner=-1;
    for(int h=0;h<3;h++)owners[h]=swat_motel_hinge_frame_owner(&sim.world,h);
    // Keep the opposite jamb physically intact: deleting the entire facade
    // would not demonstrate an independently supported latch.
    b3Quat q=swat_door_rotation(o->closed_yaw,0);
    b3Pos latch=b3OffsetPos(o->hinge,b3RotateVector(q,swat_v(-.3f,.06f,2*o->half.z+.05f)));
    b3Vec3 ray=b3RotateVector(q,swat_v(.6f,0,0));
    for(int i=group-1;i<sim.world.count && sim.world.objects[i].wall_group==group;i++)
        if(swat_world_object_ray(&sim.world.objects[i],latch,ray)){latch_owner=i;break;}
    assert(latch_owner>=0);
    swat_world_support_begin(&sim.world);
    for(int h=0;h<3;h++) {
        int owner=owners[h];assert(owner>=0 && owner!=latch_owner);
        if(sim.world.objects[owner].active)assert(swat_world_damage(&sim.world,owner,sim.world.objects[owner].max_health));
    }
    swat_world_support_end(&sim.world);
    assert(swat_world_object_ray(&sim.world.objects[latch_owner],latch,ray));
    assert(o->motion && o->motion->state.broken_hinges==7 && o->motion->support==SWAT_DOOR_LATCH && !o->cuts);
    synchronize();fresh();step(12);
    assert(swat_world_break_latch(&sim.world,16) && swat_door_loose(o));synchronize();fresh();close_fixture();
    puts("PASS removed jamb: batched backing removal releases every hinge while the opposite intact latch holds; breaking that final latch frees the leaf");
}
static void mixed_and_rejection(void) {
    fixture(0,false);synchronize();
    // A fabricated failure with intact leaf and frame is still invalid.
    bad=state;bad.objects[16].door=(SwatDoorState){.broken_hinges=1,.position=sim.world.objects[16].center,.rotation=swat_object_rotation(&sim.world.objects[16])};reject();
    cut_frame(0,0);synchronize();
    SwatObject* o=&sim.world.objects[16];b3Vec3 wood=swat_door_hinge(o,1);
    assert(swat_world_cut(&sim.world,16,(SwatCut){.y=wood.y,.z=wood.z,.height=.08f,.width=.035f,.kind=SWAT_CUT_BREACH})>0);
    assert(o->motion->state.broken_hinges==3 && swat_world_door_hinges(o)==2);synchronize();fresh();
    // A snapshot must not regrow masonry or repair a failed attachment.
    bad=state;bad.objects[16].door.broken_hinges=2;reject();
    cut_frame(2,0);assert(swat_door_loose(o));synchronize();fresh();close_fixture();
    puts("PASS mixed ownership and atomic rejection: leaf/frame failures combine, fabricated or omitted failures and repaired hinge state leave replicas unchanged");
}
static void charge_aperture(void) {
    fixture(0,false);synchronize();
    int owner=swat_motel_hinge_frame_owner(&sim.world,1);assert(swat_world_breachable(&sim.world.objects[owner]));
    // Exercise the normal wall-charge geometry path rather than removing
    // backing by hand. Its finite aperture reaches the three hinge heights.
    assert(swat_world_breach(&sim.world,owner,mount(1))>0);
    const SwatObject* o=&sim.world.objects[16];
    assert(o->active && !o->cuts && o->motion && o->motion->state.broken_hinges==7 && swat_door_loose(o));
    synchronize();fresh();step(12);close_fixture();
    puts("PASS wall-charge aperture: ordinary wall breach geometry releases all three unsupported hinges without cutting or deleting the leaf");
}
int main(int argc,char** argv) {
    assert(argc<=2);b3SetLogFcn(physics_log);setvbuf(stdout,NULL,_IONBF,0);
    if(argc==2){trace=fopen(argv[1],"w");assert(trace);}
    local_damage();last_hinge();latch_and_removal();mixed_and_rejection();charge_aperture();
    if(trace)assert(!fclose(trace));
    assert(!overflows);return 0;
}
