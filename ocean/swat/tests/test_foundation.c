#include "replay.h"
#include "pose.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void reload_interruptions(void) {
    for(int inventory=0;inventory<2;inventory++) for(int profile=0;profile<SWAT_WEAPON_PROFILES;profile++) {
        for(int cancel_tick=0;cancel_tick<220;cancel_tick++) {
            SwatArsenal a; swat_weapons_init(&a,41);
            if(profile!=1) swat_weapons_primary(&a,profile); else a.active=1;
            a.equip_remaining=0;
            SwatWeapon* w=&a.slots[a.active];
            w->magazine=3; w->chambered=cancel_tick%2;
            int capacity=swat_arsenal_def(&a,a.active)->capacity;
            swat_weapons_magazines(w,inventory!=0,capacity);
            int total=swat_weapon_rounds(w);
            SwatInput input=swat_neutral_input(); input.reload=true;
            swat_weapons_step(&a,&input,1,0,true,false); input.reload=false;
            for(int t=0;t<cancel_tick;t++) {
                swat_weapons_step(&a,&input,1,0,true,false);
                assert(swat_weapon_rounds(w)==total);
            }
            input.cancel_reload=true; swat_weapons_step(&a,&input,1,0,true,false);
            assert(!w->reload_remaining && w->reload_stage==SWAT_RELOAD_IDLE && swat_weapon_rounds(w)==total);
            input.cancel_reload=false; input.reload=true; swat_weapons_step(&a,&input,1,0,true,false); input.reload=false;
            for(int t=0;t<240;t++) swat_weapons_step(&a,&input,1,0,true,false);
            assert(swat_weapon_rounds(w)==total && w->chambered && w->magazine_seated);
            swat_weapons_magazines(w,false,capacity); assert(swat_weapon_rounds(w)==total);
        }
    }
    puts("PASS staged reloads: every interruption tick, all profiles, pooled/retained magazines, resume and conservation");
}

static void poses_and_range(void) {
    static SwatSim sim,replica;
    SwatConfig config=swat_default_config(); config.mission=SWAT_RANGE; config.randomize=false; config.hostile_fire=false;
    swat_sim_init(&sim,config,51);
    SwatInput in=swat_neutral_input(); in.aim=true;
    for(int t=0;t<50;t++) swat_sim_step(&sim,&in);
    SwatPose pose=swat_pose(&sim.actors[0].controller,&sim.actors[0].arsenal);
    assert(b3Length(b3Cross(b3SubPos(pose.sight,pose.eye),pose.forward))<.002f);
    in.aim=false; in.ready=SWAT_LOW_READY;
    for(int t=0;t<30;t++) swat_sim_step(&sim,&in);
    pose=swat_pose(&sim.actors[0].controller,&sim.actors[0].arsenal);
    assert(pose.muzzle.y<pose.sight.y && sim.actors[0].controller.ready_blend<-.95f);
    in.ready=SWAT_HIGH_READY;
    for(int t=0;t<40;t++) swat_sim_step(&sim,&in);
    assert(sim.actors[0].controller.ready_blend>.95f);
    in.fire=true; int shots=sim.actors[0].arsenal.shots;
    for(int t=0;t<40;t++) swat_sim_step(&sim,&in);
    assert(sim.actors[0].arsenal.shots==shots+1 && sim.actors[0].controller.ready==SWAT_READY);
    static SwatMap map; static SwatSnapshot snapshot;
    swat_capture_map(&sim,1,&map); swat_apply_map(&replica,&map);
    swat_capture_snapshot(&sim,1,&snapshot); assert(swat_apply_snapshot(&replica,&snapshot));
    int tilted=0;
    for(int i=0;i<sim.world.count;i++) if(sim.world.objects[i].pitch) {
        tilted++;
        b3Pos from=b3OffsetPos(sim.world.objects[i].center,swat_v(0,5,0));
        SwatHit a=swat_world_ray(&sim.world,from,swat_v(0,-1,0),10,b3_nullBodyId);
        SwatHit b=swat_world_ray(&replica.world,from,swat_v(0,-1,0),10,b3_nullBodyId);
        assert(a.kind==b.kind && fabsf(a.distance-b.distance)<1e-5f);
    }
    assert(tilted==2);
    SwatController* c=&sim.actors[0].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){2,c->body.totalHeight*.5f+.02f,0},b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0)); c->yaw=0;
    in=swat_neutral_input(); in.forward=1;
    float peak=0;
    for(int t=0;t<130;t++) {
        swat_sim_step(&sim,&in); peak=fmaxf(peak,(float)swat_body_feet_position(&c->body).y);
    }
    b3Pos feet=swat_body_feet_position(&c->body);
    assert(feet.x>6 && peak>1.0f);
    b3Body_SetTransform(c->body.body,(b3Pos){2,c->body.totalHeight*.5f+.02f,-5},b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
    peak=0; bool grounded_on_ramp=false;
    for(int t=0;t<100;t++) {
        swat_sim_step(&sim,&in); feet=swat_body_feet_position(&c->body);
        peak=fmaxf(peak,(float)feet.y);
        if(feet.y>.5f && c->body.onGround) grounded_on_ramp=true;
    }
    feet=swat_body_feet_position(&c->body);
    assert(feet.x>8 && peak>1 && grounded_on_ramp);
    b3Body_SetTransform(c->body.body,(b3Pos){2,c->body.totalHeight*.5f+.02f,-10},b3Quat_identity);
    b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
    for(int t=0;t<130;t++) swat_sim_step(&sim,&in);
    feet=swat_body_feet_position(&c->body);
    assert(feet.x<6 && feet.y<1.5f);
    swat_sim_close(&replica); swat_sim_close(&sim);
    puts("PASS poses/range: ADS alignment, high/low ready, raise-before-fire, replicated ramps, stairs, shallow ramp traversal and steep-slope rejection");
}

static void replay_roundtrip(const char* path) {
    static SwatSim source,playback;
    SwatConfig config=swat_default_config(); config.mission=SWAT_RANGE; config.randomize=false; config.hostile_fire=false;
    config.max_ticks=1200; swat_sim_init(&source,config,91);
    SwatReplay writer; assert(swat_replay_record(&writer,path,&source));
    for(int t=0;t<240;t++) {
        SwatInput input=swat_neutral_input();
        input.forward=t<120 ? .5f : 0; input.strafe=t<50 ? .5f : 0;
        input.yaw_delta=t<70 ? .001f : 0; input.crouch=t>150 && t<180;
        input.lean=t>180 ? -.5f : 0; input.fire=t==200 || t==215; input.reload=t==220;
        swat_sim_step(&source,&input); assert(swat_replay_append(&writer,&input,&source));
    }
    assert(swat_replay_close(&writer));
    SwatReplay reader; assert(swat_replay_open(&reader,path)); swat_sim_init(&playback,reader.config,reader.seed);
    SwatInput input; uint32_t expected; int status;
    while((status=swat_replay_next(&reader,&input,&expected))==1) {
        swat_sim_step(&playback,&input);
        assert(swat_replay_digest(&playback)==expected);
    }
    assert(status==0 && reader.frame==240 && swat_replay_close(&reader));
    assert(swat_replay_digest(&source)==swat_replay_digest(&playback));
    // A replay with trailing bytes is rejected rather than silently accepted.
    FILE* file=fopen(path,"ab"); assert(file); assert(fputc(0,file)!=EOF && fclose(file)==0);
    assert(swat_replay_open(&reader,path));
    for(int t=0;t<240;t++) assert(swat_replay_next(&reader,&input,&expected)==1);
    assert(swat_replay_next(&reader,&input,&expected)==-1 && !swat_replay_close(&reader));
    remove(path); swat_sim_close(&source); swat_sim_close(&playback);
    puts("PASS replay: exact 240-tick state match and malformed-tail rejection");
}
int main(int argc,char** argv) {
    assert(argc==2); reload_interruptions(); poses_and_range(); replay_roundtrip(argv[1]); return 0;
}
