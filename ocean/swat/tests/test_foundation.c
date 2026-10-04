#include "replay.h"
#include "pose.h"
#include "rifle_geometry.h"
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
    SwatController* test=&sim.actors[0].controller;
    for(int stance=0;stance<2;stance++) for(int ready=-1;ready<=1;ready++) for(int angle=0;angle<5;angle++) {
        test->yaw=.73f*angle; test->pitch=-.4f+.2f*angle;
        test->ads=1; test->ready_blend=ready; test->lean=stance ? -.6f : .6f;
        SwatPose measured=swat_pose(test,&sim.actors[0].arsenal);
        assert(fabsf(b3Length(b3SubPos(measured.muzzle,measured.shoulder))-.9010567f)<.00001f);
        assert(b3Length(b3SubPos(measured.muzzle,swat_pose_weapon_point(&measured,swat_carbine_muzzle)))<.000001f);
        assert(fabsf(b3Dot(measured.weapon_forward,measured.weapon_up))<.000001f);
        assert(fabsf(b3Length(measured.weapon_forward)-1)<.000001f);
        if(!ready) {
            assert(b3Length(b3Cross(b3SubPos(measured.sight,measured.eye),measured.forward))<.00001f);
            b3Pos front=swat_pose_weapon_point(&measured,swat_carbine_front_sight);
            assert(b3Length(b3Cross(b3SubPos(front,measured.sight),measured.forward))<.00003f);
            assert(fabsf(b3Dot(b3SubPos(measured.muzzle,measured.sight),measured.up)+.08112239f)<.00001f);
        }
    }
    test->yaw=test->pitch=test->lean=test->ready_blend=test->ads=0;
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
static void tactical_replay(const char* path) {
    static SwatSim source,playback;
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOUSE; config.max_ticks=1800;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false;
    swat_sim_init(&source,config,177); SwatReplay writer; assert(swat_replay_record(&writer,path,&source));
    for(int t=0;t<360;t++) {
        SwatInput input=swat_neutral_input(); input.squad_order=t==10 ? SWAT_ORDER_HOLD : t==100 ? SWAT_ORDER_FALL_IN : 0;
        input.squad_queue=t==10; input.squad_execute=t==50;
        swat_sim_step(&source,&input); assert(swat_replay_append(&writer,&input,&source));
    }
    assert(swat_replay_close(&writer)); SwatReplay reader; assert(swat_replay_open(&reader,path));
    assert(reader.config.tactical_rules && reader.config.squad_bots==3); swat_sim_init(&playback,reader.config,reader.seed);
    SwatInput input; uint32_t hash; int status;
    while((status=swat_replay_next(&reader,&input,&hash))==1) { swat_sim_step(&playback,&input); assert(swat_replay_digest(&playback)==hash); }
    assert(status==0 && swat_replay_close(&reader));
    assert(source.actors[3].alive && source.actors[4].alive && source.actors[5].alive && !source.totals.civilian_damage);
    swat_sim_close(&source); swat_sim_close(&playback); remove(path);
    puts("PASS tactical replay: identical autonomous NPC/squad movement, delayed queued orders and intact civilian/officer outcomes");
}
static void saved_missions(const char* path) {
    static SwatSim source,restored;
    SwatConfig config=swat_default_config(); config.mission=SWAT_HOUSE; config.tactical_rules=true;
    config.squad_bots=3; config.hostile_fire=false; config.max_ticks=1200;
    swat_sim_init(&source,config,177); swat_sim_init(&restored,config,177);
    char save[2048],continued[2048],bad[2048],error[256];
    snprintf(save,sizeof(save),"%s.save",path); snprintf(continued,sizeof(continued),"%s.live",path); snprintf(bad,sizeof(bad),"%s.bad",path);
    SwatReplay writer={0}; assert(swat_replay_record(&writer,path,&source));
    SwatInput inputs[240]; uint32_t hashes[240];
    for(int tick=0;tick<240;tick++) {
        SwatInput in=swat_neutral_input(); in.yaw_delta=tick<80 ? .002f : 0;
        in.primary_profile=tick==0 ? 4 : 0; in.magazine_inventory=tick==0;
        in.squad_order=tick==10 ? SWAT_ORDER_HOLD : tick==160 ? SWAT_ORDER_FALL_IN : 0;
        in.fire=tick==50 || tick==90 || tick==150; in.reload=tick==100;
        swat_sim_step(&source,&in); inputs[tick]=in; hashes[tick]=swat_replay_digest(&source);
        assert(swat_replay_append(&writer,&in,&source));
        if(tick==119) assert(swat_replay_checkpoint(&writer,save));
    }
    assert(swat_replay_close(&writer));
    assert(swat_replay_restore(&restored,save,error,sizeof(error)) && restored.tick==120);
    assert(swat_replay_digest(&restored)==hashes[119]);
    for(int i=0;i<restored.world.count;i++) if(restored.world.objects[i].active)
        assert(b3Body_GetUserData(restored.world.objects[i].body)==&restored.world.objects[i].tag);
    for(int i=0;i<restored.actor_count;i++) if(restored.actors[i].present)
        assert(b3Body_GetUserData(restored.actors[i].controller.body.body)==&restored.actors[i].tag);
    assert(swat_replay_continue(&writer,continued,save,&restored));
    for(int tick=120;tick<240;tick++) {
        swat_sim_step(&restored,&inputs[tick]); assert(swat_replay_digest(&restored)==hashes[tick]);
        assert(swat_replay_append(&writer,&inputs[tick],&restored));
    }
    assert(swat_replay_checkpoint(&writer,save) && swat_replay_close(&writer));
    assert(swat_replay_restore(&source,save,error,sizeof(error)) && source.tick==240);
    assert(swat_replay_digest(&source)==hashes[239]);
    // A corrupt tail must preserve the existing live world and close its reader.
    FILE* file=fopen(bad,"wb"); FILE* good=fopen(save,"rb"); assert(file && good);
    int byte; while((byte=fgetc(good))!=EOF) assert(fputc(byte,file)!=EOF);
    assert(!fclose(good) && fputc(0,file)!=EOF && !fclose(file));
    b3WorldId world=source.world.id;
    assert(!swat_replay_restore(&source,bad,error,sizeof(error)) && error[0]);
    assert(b3StoreWorldId(world)==b3StoreWorldId(source.world.id) && swat_replay_digest(&source)==hashes[239]);
    assert(!remove(bad)); // Also proves no leaked file handle on Windows.
    swat_sim_close(&source); swat_sim_close(&restored); remove(save); remove(continued); remove(path);
    puts("PASS mission saves: atomic checkpoint, reload/loadout/squad state, retargeted physics tags, exact continuation, repeated saving and failure preserving live world");
}
int main(int argc,char** argv) {
    assert(argc==2); reload_interruptions(); poses_and_range(); replay_roundtrip(argv[1]); tactical_replay(argv[1]); saved_missions(argv[1]); return 0;
}
