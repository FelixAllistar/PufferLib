#include "sim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void controller_tick(SwatWorld* world, SwatController* c, SwatInput in) {
    swat_controller_pre_step(c,&in,false);
    b3World_Step(world->id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);
    swat_controller_post_step(c,&in);
}

static void controller_fixture(SwatWorld* world, SwatController* c) {
    swat_world_init(world);
    swat_world_box(world,(b3Pos){0,-0.5f,0},swat_v(30,0.5f,30),SWAT_CONCRETE,0);
    swat_controller_init(c,world->id,(b3Pos){0,0,0},0);
    for (int i=0;i<10;i++) controller_tick(world,c,swat_neutral_input());
    assert(c->body.onGround);
}

static void test_movement_and_gates(void) {
    SwatWorld world; SwatController c;
    controller_fixture(&world,&c);
    SwatInput in=swat_neutral_input(); in.forward=1; in.strafe=1;
    for (int t=0;t<120;t++) controller_tick(&world,&c,in);
    b3Vec3 velocity=b3Body_GetLinearVelocity(c.body.body);
    float horizontal_speed=sqrtf(velocity.x*velocity.x+velocity.z*velocity.z);
    assert(horizontal_speed <= 2.82f && horizontal_speed > 2.5f);
    in.strafe=0; in.gait=SWAT_SPRINT;
    for (int t=0;t<60;t++) controller_tick(&world,&c,in);
    assert(c.sprinting && c.stamina < 0.90f);
    in.aim=true; controller_tick(&world,&c,in);
    assert(!c.sprinting && c.ads > 0);
    in.aim=false; in.lean=1; controller_tick(&world,&c,in);
    assert(!c.sprinting);
    in.lean=0; in.crouch=true; controller_tick(&world,&c,in);
    assert(!c.sprinting && c.body.crouched);
    in=swat_neutral_input();
    for (int t=0;t<20;t++) controller_tick(&world,&c,in);
    in.jump=true;
    int jumps=0; float peak=0;
    for (int t=0;t<120;t++) {
        controller_tick(&world,&c,in);
        jumps += c.jumped;
        peak=fmaxf(peak,(float)swat_body_feet_position(&c.body).y);
    }
    assert(jumps==1 && peak>0.4f && c.body.onGround);
    swat_world_close(&world);
    puts("PASS movement normalization, sprint/ADS/stance gates, stamina, edge-triggered jump");
}

static void test_stance_clearance(void) {
    SwatWorld world; SwatController c;
    controller_fixture(&world,&c);
    swat_world_box(&world,(b3Pos){3,2,0},swat_v(1.5f,0.8f,2),SWAT_CONCRETE,0);
    SwatInput in=swat_neutral_input(); in.forward=1;
    for (int t=0;t<100;t++) controller_tick(&world,&c,in);
    assert(swat_body_feet_position(&c.body).x < 1.4f);
    in.crouch=true;
    for (int t=0;t<70;t++) controller_tick(&world,&c,in);
    assert(swat_body_feet_position(&c.body).x > 2);
    in.forward=0; in.crouch=false;
    for (int t=0;t<20;t++) controller_tick(&world,&c,in);
    assert(c.body.crouched);
    b3Body_SetTransform(c.body.body,(b3Pos){0,c.body.totalHeight*0.5f+0.02f,0},b3Quat_identity);
    b3Body_SetLinearVelocity(c.body.body,swat_v(0,0,0));
    for (int t=0;t<4;t++) controller_tick(&world,&c,in);
    assert(!c.body.crouched);
    swat_world_close(&world);
    puts("PASS stand blockage, crouch traversal, blocked stand-up, clearance recovery");
}

static void test_lean_collision_and_exposure(void) {
    SwatWorld world; SwatController c;
    controller_fixture(&world,&c);
    SwatInput in=swat_neutral_input(); in.lean=1;
    for (int t=0;t<20;t++) controller_tick(&world,&c,in);
    assert(c.lean > 0.95f);
    assert(fabsf((float)swat_body_feet_position(&c.body).z) < 0.08f);
    b3Capsule cap=b3Shape_GetCapsule(c.body.capsuleId);
    assert(cap.center2.z > 0.39f);
    SwatHit exposure=swat_world_ray(&world,(b3Pos){0,1.65f,2},swat_v(0,0,-1),3,b3_nullBodyId);
    assert(exposure.hit && exposure.point.z > 0.5f);
    swat_world_close(&world);

    controller_fixture(&world,&c);
    swat_world_box(&world,(b3Pos){0,1.5f,0.65f},swat_v(3,1.5f,0.1f),SWAT_CONCRETE,0);
    for (int t=0;t<30;t++) controller_tick(&world,&c,in);
    cap=b3Shape_GetCapsule(c.body.capsuleId);
    b3Pos center=b3Body_GetPosition(c.body.body);
    assert(c.lean > 0.1f && c.lean < 0.75f);
    assert(center.z+cap.center2.z+cap.radius < 0.556f);
    assert(swat_controller_eye(&c).z < 0.55f);
    in.lean=0;
    for (int t=0;t<20;t++) controller_tick(&world,&c,in);
    assert(fabsf(c.lean)<0.02f);
    swat_world_close(&world);
    puts("PASS lean moves real hit geometry and eye, clamps at cover, and recenters");
}

static void test_leaned_stand_clearance(void) {
    SwatWorld world; SwatController c;
    controller_fixture(&world,&c);
    swat_world_box(&world,(b3Pos){0,1.7f,0.9f},swat_v(2,0.5f,0.35f),SWAT_CONCRETE,0);
    SwatInput in=swat_neutral_input(); in.crouch=true; in.lean=1;
    for(int t=0;t<24;t++) controller_tick(&world,&c,in);
    assert(c.body.crouched && c.lean>0.95f);
    in.crouch=false;
    for(int t=0;t<8;t++) controller_tick(&world,&c,in);
    assert(c.body.crouched && swat_controller_eye(&c).y<1.2f);
    in.lean=0;
    for(int t=0;t<30;t++) controller_tick(&world,&c,in);
    assert(!c.body.crouched && fabsf(c.lean)<0.02f);
    assert(fabsf(b3Body_GetMass(c.body.body)-c.body.mass)<0.02f);
    swat_world_close(&world);
    puts("PASS leaned head blocks stand-up under offset overhang and clears after recentering");
}

static SwatShot weapon_tick(SwatArsenal* a, SwatInput in) {
    return swat_weapons_step(a,&in,1,0,true,false);
}

static void test_weapons(void) {
    SwatArsenal a; swat_weapons_init(&a,42);
    int initial=swat_weapon_rounds(&a.slots[0]);
    SwatInput in=swat_neutral_input(); in.fire=true;
    assert(weapon_tick(&a,in).fired);
    for (int t=0;t<30;t++) assert(!weapon_tick(&a,in).fired);
    assert(a.shots==1 && swat_weapon_rounds(&a.slots[0])==initial-1);
    in.fire=false; weapon_tick(&a,in); in.fire=true;
    assert(weapon_tick(&a,in).fired);
    a.slots[0].mode=SWAT_SAFE;
    for (int t=0;t<15;t++) { in.fire=t%2; assert(!weapon_tick(&a,in).fired); }
    swat_weapons_init(&a,42); a.slots[0].mode=SWAT_AUTO; in.fire=true;
    int fired=0;
    for (int t=0;t<60;t++) fired+=weapon_tick(&a,in).fired;
    assert(fired==10 && a.slots[0].magazine==20 && a.slots[0].chambered);
    assert(swat_weapon_rounds(&a.slots[0])+fired==initial);

    in.fire=false; in.reload=true;
    int rounds=swat_weapon_rounds(&a.slots[0]);
    weapon_tick(&a,in);
    assert(a.slots[0].reload_remaining==swat_weapon_def(0)->reload_ticks);
    in.fire=true;
    for (int t=0;t<swat_weapon_def(0)->reload_ticks;t++) assert(!weapon_tick(&a,in).fired);
    assert(a.slots[0].magazine==30 && a.slots[0].chambered);
    assert(swat_weapon_rounds(&a.slots[0])==rounds);
    in=swat_neutral_input(); weapon_tick(&a,in);
    a.slots[0].magazine=0; a.slots[0].chambered=false;
    rounds=swat_weapon_rounds(&a.slots[0]);
    in.reload=true; weapon_tick(&a,in);
    in.reload=false;
    for (int t=0;t<swat_weapon_def(0)->empty_reload_ticks;t++) weapon_tick(&a,in);
    assert(a.slots[0].magazine==29 && a.slots[0].chambered);
    assert(swat_weapon_rounds(&a.slots[0])==rounds);

    a.slots[0].magazine=10; in.reload=true; weapon_tick(&a,in);
    rounds=swat_weapon_rounds(&a.slots[0]);
    in.weapon=2; in.fire=true;
    assert(!weapon_tick(&a,in).fired);
    assert(a.active==1 && a.equip_remaining>0 && a.slots[0].reload_remaining==0);
    assert(swat_weapon_rounds(&a.slots[0])==rounds && a.slots[0].magazine==10);
    swat_weapons_init(&a,10);
    in=swat_neutral_input(); in.fire=true;
    assert(!swat_weapons_step(&a,&in,1,0,true,true).fired);
    assert(a.shots==0);
    puts("PASS semi/auto/safe, fire cadence, tactical/empty reload conservation, cancellation, equip and obstruction gates");
}

static SwatShot test_shot(float energy) {
    SwatShot shot={0}; shot.fired=true; shot.damage=34; shot.range=30; shot.energy=energy;
    return shot;
}

static void test_damage_and_destruction(void) {
    SwatSim s;
    SwatConfig cfg=swat_default_config(); cfg.randomize=false; cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,55);
    int cover=swat_world_box(&s.world,(b3Pos){16,1.3f,2.9f},swat_v(0.2f,1.0f,0.6f),SWAT_WOOD,100);
    swat_sim_shoot(&s,0,(b3Pos){15,1.3f,2.9f},swat_v(1,0,0),test_shot(0.1f));
    assert(s.actors[1].health==100 && s.world.objects[cover].health==66);
    assert(swat_world_damage(&s.world,cover,100));
    assert(!s.world.objects[cover].active && s.world.generation==1);
    swat_sim_shoot(&s,0,(b3Pos){15,1.3f,2.9f},swat_v(1,0,0),test_shot(1));
    assert(s.actors[1].health==66);
    swat_sim_close(&s);

    swat_sim_init(&s,cfg,55);
    cover=swat_world_box(&s.world,(b3Pos){16,1.3f,2.9f},swat_v(0.08f,1,0.6f),SWAT_DRYWALL,100);
    swat_sim_shoot(&s,0,(b3Pos){15,1.3f,2.9f},swat_v(1,0,0),test_shot(1));
    assert(s.actors[1].health<80 && s.actors[1].health>65);
    assert(s.world.objects[cover].active);
    // A destroyed cell is gone from both the observation rays and physics.
    float obs_before[SWAT_OBS_SIZE],obs_after[SWAT_OBS_SIZE];
    swat_sim_observe(&s,0,obs_before);
    int door=-1;
    for (int i=0;i<s.world.count;i++) if(s.world.objects[i].door) door=i;
    assert(door>=0);
    swat_world_damage(&s.world,door,1000);
    swat_sim_observe(&s,0,obs_after);
    int center=SWAT_PROPRIO_SIZE+(2*SWAT_SENSOR_COLS+4)*3;
    assert(obs_after[center]>obs_before[center]+0.1f);
    assert(obs_after[center+1]!=obs_before[center+1]);
    swat_sim_close(&s);
    puts("PASS nearest-cover damage, collider removal, thickness/energy penetration and fresh sensor geometry");
}

static void test_door_and_muzzle(void) {
    SwatSim s; SwatConfig cfg=swat_default_config(); cfg.randomize=false; cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,17);
    SwatController* c=&s.actors[0].controller;
    b3Body_SetTransform(c->body.body,(b3Pos){5.4f,c->body.standHeight/2+0.02f,0},b3Quat_identity);
    SwatInput in=swat_neutral_input(); in.interact=true;
    swat_sim_step(&s,&in);
    int door=-1;
    for(int i=0;i<s.world.count;i++) if(s.world.objects[i].door) door=i;
    assert(door>=0 && s.world.objects[door].door_open);
    for(int t=0;t<60;t++) swat_sim_step(&s,&in);
    assert(s.world.objects[door].door_open); // held input never retriggers
    SwatHit through=swat_world_ray(&s.world,(b3Pos){5.4f,1.6f,0},swat_v(1,0,0),4,c->body.body);
    assert(!through.hit);
    // Muzzle volume near a concrete wall cannot fire through it.
    swat_world_box(&s.world,(b3Pos){5.80f,1.6f,0},swat_v(0.08f,1.0f,0.6f),SWAT_CONCRETE,0);
    in=swat_neutral_input(); in.fire=true;
    int shots=s.actors[0].arsenal.shots;
    swat_sim_step(&s,&in);
    assert(c->muzzle_blocked && s.actors[0].arsenal.shots==shots);
    swat_sim_close(&s);
    puts("PASS door interaction edge, physical opening and muzzle-wall firing prevention");
}

static void test_door_obstruction(void) {
    SwatSim s; SwatConfig cfg=swat_default_config(); cfg.randomize=false; cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,17);
    SwatController* c=&s.actors[0].controller;
    int door=-1;
    for(int i=0;i<s.world.count;i++) if(s.world.objects[i].door) door=i;
    SwatObject* d=&s.world.objects[door];
    float height=c->body.totalHeight*0.5f+0.02f;
    for(int closing=0;closing<2;closing++) {
        b3Body_SetTransform(c->body.body,(b3Pos){7.9f,height,0.1f},b3Quat_identity);
        d->door_open=!closing;
        for(int t=0;t<90;t++) swat_world_step_doors(&s.world);
        float angle=d->door_angle;
        assert(angle>0.1f && angle<1.5f);
        for(int t=0;t<10;t++) swat_world_step_doors(&s.world);
        assert(d->door_angle==angle);
        b3Body_SetTransform(c->body.body,(b3Pos){5.4f,height,0},b3Quat_identity);
        for(int t=0;t<90;t++) swat_world_step_doors(&s.world);
        assert(fabsf(d->door_angle-(closing ? 0 : SWAT_PI*0.5f))<1e-6f);
    }
    swat_sim_close(&s);
    puts("PASS opening/closing doors stop for actors and resume after obstruction clears");
}

static void test_guard_visibility(void) {
    SwatSim s; SwatConfig cfg=swat_default_config(); cfg.randomize=false;
    swat_sim_init(&s,cfg,31);
    SwatInput in=swat_neutral_input();
    for(int t=0;t<90;t++) swat_sim_step(&s,&in);
    assert(s.actors[1].visible_ticks==0 && s.actors[1].arsenal.shots==0);
    SwatController* c=&s.actors[0].controller;
    float height=c->body.totalHeight*0.5f+0.02f;
    b3Body_SetTransform(c->body.body,(b3Pos){14.5f,height,2.9f},b3Quat_identity);
    for(int t=0;t<36;t++) swat_sim_step(&s,&in);
    assert(s.actors[1].visible_ticks==36 && s.actors[1].arsenal.shots==0);
    for(int t=0;t<18;t++) swat_sim_step(&s,&in);
    assert(s.actors[1].arsenal.shots==1 && s.actors[0].health<100);
    swat_world_box(&s.world,(b3Pos){15.5f,1.5f,2.9f},swat_v(0.2f,1.5f,1),SWAT_CONCRETE,0);
    int shots=s.actors[1].arsenal.shots;
    for(int t=0;t<30;t++) swat_sim_step(&s,&in);
    assert(s.actors[1].visible_ticks==0 && s.actors[1].arsenal.shots==shots);
    swat_sim_close(&s);
    puts("PASS guard respects occlusion and reaction time, fires through shared weapons, loses sight behind cover");
}

static void test_complete_missions(void) {
    // Test-only driver knows waypoints and the target pose. It still has to
    // walk, interact, acquire an unobstructed sightline and fire ordinary inputs.
    const b3Pos waypoints[]={{5.4f,0,0},{10.9f,0,0},{10.9f,0,3.9f},{15,0,3.9f},{21,0,0}};
    for(unsigned seed=1;seed<=8;seed++) {
        SwatSim s; swat_sim_init(&s,swat_default_config(),seed);
        int waypoint=0,door=-1;
        for(int i=0;i<s.world.count;i++) if(s.world.objects[i].door) door=i;
        for(int tick=0;tick<1500 && s.end==SWAT_RUNNING;tick++) {
            SwatController* c=&s.actors[0].controller;
            b3Pos feet=swat_body_feet_position(&c->body);
            b3Vec3 delta=b3SubPos(waypoints[waypoint],feet); delta.y=0;
            SwatInput in=swat_neutral_input();
            if(b3Length(delta)<0.2f && waypoint<4) {
                if(waypoint==0) in.interact=true;
                waypoint++;
            }
            b3Pos target=swat_body_feet_position(&s.actors[1].controller.body);
            target.y+=1.3f;
            b3Vec3 sight=b3SubPos(target,swat_controller_eye(c));
            SwatHit hit=swat_world_ray(&s.world,swat_controller_eye(c),sight,b3Length(sight)+0.2f,c->body.body);
            if(s.actors[1].alive && hit.kind==SWAT_HIT_ACTOR && hit.index==1) {
                float desired_yaw=atan2f(sight.z,sight.x);
                float desired_pitch=atan2f(sight.y,sqrtf(sight.x*sight.x+sight.z*sight.z));
                in.yaw_delta=swat_angle(desired_yaw-c->yaw-c->recoil_yaw);
                in.pitch_delta=desired_pitch-c->pitch-c->recoil_pitch;
                in.aim=true;
                in.fire=c->ads>0.9f && fabsf(in.yaw_delta)<0.02f && tick%10==0;
            } else {
                in.yaw_delta=swat_angle(atan2f(delta.z,delta.x)-c->yaw);
                in.pitch_delta=-c->pitch;
                in.forward=fabsf(in.yaw_delta)<0.2f ? 1 : 0;
                if(waypoint==1 && s.world.objects[door].door_angle<1.5f) in.forward=0;
            }
            swat_sim_step(&s,&in);
        }
        if(s.end!=SWAT_SUCCESS) fprintf(stderr,"mission seed=%u end=%s tick=%d waypoint=%d hp=%.1f\n",
            seed,swat_end_name(s.end),s.tick,waypoint,s.actors[0].health);
        assert(s.end==SWAT_SUCCESS && s.totals.hostile_down==1 && s.totals.shots>=2);
        assert(s.actors[2].health==100 && s.totals.civilian_damage==0);
        swat_sim_close(&s);
    }
    puts("PASS eight randomized live-fire missions: open door, traverse, engage armed target, protect civilian, extract");
}

static void test_cooperative_mission(void) {
    SwatSim s; SwatConfig cfg=swat_default_config(); cfg.randomize=false; cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,42);
    assert(swat_sim_set_player(&s,1,true));
    int teammate=swat_player_actor(1);
    SwatInput inputs[SWAT_MAX_ACTORS];
    for(int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i]=swat_neutral_input();
    swat_sim_damage_actor(&s,1,teammate,100);
    swat_sim_spawn_actor(&s,0,SWAT_OFFICER,s.extraction,0);
    swat_sim_step_inputs(&s,inputs);
    assert(s.end==SWAT_RUNNING); // One officer at extraction cannot finish for the team.
    swat_sim_spawn_actor(&s,teammate,SWAT_OFFICER,b3OffsetPos(s.extraction,swat_v(0,0,.9f)),0);
    swat_sim_step_inputs(&s,inputs);
    assert(s.end==SWAT_SUCCESS);

    swat_sim_reset(&s); assert(swat_sim_set_player(&s,1,true));
    swat_sim_damage_actor(&s,0,1,100);
    swat_sim_step_inputs(&s,inputs); assert(s.end==SWAT_RUNNING);
    swat_sim_damage_actor(&s,teammate,1,100);
    swat_sim_step_inputs(&s,inputs); assert(s.end==SWAT_OFFICER_DOWN);

    swat_sim_reset(&s); assert(swat_sim_set_player(&s,1,true));
    swat_sim_spawn_actor(&s,teammate,SWAT_OFFICER,(b3Pos){17,0,-4.8f},0);
    inputs[teammate].fire=true;
    swat_sim_step_inputs(&s,inputs); assert(s.end==SWAT_CIVILIAN_HARMED);
    inputs[teammate]=swat_neutral_input();

    swat_sim_reset(&s); assert(swat_sim_set_player(&s,1,true)); s.config.hostile_fire=true;
    swat_sim_spawn_actor(&s,teammate,SWAT_OFFICER,(b3Pos){14.5f,0,2.9f},0);
    for(int t=0;t<54;t++) { swat_sim_bot_inputs(&s,inputs); swat_sim_step_inputs(&s,inputs); }
    assert(s.actors[1].target_actor==teammate && s.actors[1].arsenal.shots==1);
    assert(s.actors[teammate].health<100 && s.actors[0].health==100);
    assert(swat_sim_set_player(&s,1,false));
    swat_sim_bot_inputs(&s,inputs); assert(s.actors[1].target_actor==-1);
    swat_sim_close(&s);
    puts("PASS co-op: all living officers extract, teammate survival, squad wipe, shared civilian failure and guard engages a remote officer");
}

static void test_reset_and_limits(void) {
    SwatSim s; SwatConfig cfg=swat_default_config(); cfg.max_ticks=17; cfg.hostile_fire=false;
    swat_sim_init(&s,cfg,9);
    SwatInput in=swat_neutral_input(); in.crouch=true; in.lean=1;
    for(int t=0;t<17;t++) swat_sim_step(&s,&in);
    assert(s.end==SWAT_TIMEOUT && s.tick==17);
    swat_sim_reset(&s);
    assert(s.end==SWAT_RUNNING && s.tick==0 && !s.actors[0].controller.body.crouched);
    assert(s.actors[0].controller.lean==0 && s.actors[0].health==100);
    assert(s.actors[0].arsenal.shots==0 && s.actors[0].arsenal.slots[0].magazine==30);
    swat_sim_close(&s);
    puts("PASS exact timeout and full character/weapon/scene reset");
}

int main(void) {
    test_movement_and_gates();
    test_stance_clearance();
    test_lean_collision_and_exposure();
    test_leaned_stand_clearance();
    test_weapons();
    test_damage_and_destruction();
    test_door_and_muzzle();
    test_door_obstruction();
    test_guard_visibility();
    test_complete_missions();
    test_cooperative_mission();
    test_reset_and_limits();
    puts("SWAT simulation checks passed");
    return 0;
}
