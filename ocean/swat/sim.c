#include "sim.h"
#include <assert.h>
#include <string.h>

SwatConfig swat_default_config(void) {
    return (SwatConfig){1800,true,true};
}

static void swat_spawn_actor(SwatSim* s, int index, SwatRole role, b3Pos feet, float yaw) {
    SwatActor* a = &s->actors[index];
    a->tag = (SwatTag){SWAT_HIT_ACTOR,index};
    a->role = role; a->health = 100; a->alive = true; a->last_shot_tick = -100;
    swat_controller_init(&a->controller,s->world.id,feet,yaw);
    b3Body_SetUserData(a->controller.body.body,&a->tag);
    swat_weapons_init(&a->arsenal,s->rng ^ ((uint32_t)(index+1)*0x85ebca6bu));
}

void swat_sim_init(SwatSim* s, SwatConfig config, uint32_t seed) {
    memset(s,0,sizeof(*s));
    s->config = config;
    if (s->config.max_ticks < 1) s->config.max_ticks = 1800;
    s->rng = seed ? seed : 0x12345678u;
    swat_sim_reset(s);
}

void swat_sim_reset(SwatSim* s) {
    SwatConfig config = s->config;
    uint32_t rng = s->rng;
    int episode = s->episode+1;
#pragma omp critical(swat_world_lifecycle)
    {
        swat_world_close(&s->world);
        memset(s,0,sizeof(*s));
        s->config = config; s->rng = rng; s->episode = episode;
        swat_world_init(&s->world);
        swat_world_build_range(&s->world,&s->rng,config.randomize);
        float shift = config.randomize ? (swat_rand01(&s->rng)-0.5f)*1.5f : 0;
        s->actor_count = 3;
        swat_spawn_actor(s,0,SWAT_OFFICER,(b3Pos){0,0,0},0);
        swat_spawn_actor(s,1,SWAT_SUSPECT,(b3Pos){17,0,2.9f+shift},SWAT_PI);
        swat_spawn_actor(s,2,SWAT_CIVILIAN,(b3Pos){19,0,-4.8f},SWAT_PI);
        s->extraction = (b3Pos){21,0,0};
        // Settle initial contacts without advancing game time, using the same
        // body controller and substeps as ordinary play.
        SwatInput neutral = swat_neutral_input();
        for (int t=0;t<6;t++) {
            for (int i=0;i<s->actor_count;i++)
                swat_controller_pre_step(&s->actors[i].controller,&neutral,false);
            b3World_Step(s->world.id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);
            for (int i=0;i<s->actor_count;i++)
                swat_controller_post_step(&s->actors[i].controller,&neutral);
        }
    }
}

void swat_sim_close(SwatSim* s) {
#pragma omp critical(swat_world_lifecycle)
    { swat_world_close(&s->world); }
}

int swat_sim_hostiles(const SwatSim* s) {
    int alive = 0;
    for (int i=0;i<s->actor_count;i++) alive += s->actors[i].alive && s->actors[i].role == SWAT_SUSPECT;
    return alive;
}

void swat_sim_damage_actor(SwatSim* s, int victim, int shooter, float damage) {
    if (victim < 0 || victim >= s->actor_count || damage <= 0) return;
    SwatActor* a = &s->actors[victim];
    if (!a->alive) return;
    float amount = fminf(damage,a->health);
    a->health -= amount;
    if (a->role == SWAT_OFFICER) s->events.officer_damage += amount;
    if (a->role == SWAT_CIVILIAN) s->events.civilian_damage += amount;
    if (a->role == SWAT_SUSPECT && shooter == 0) s->events.hostile_damage += amount;
    if (a->health <= 0) {
        a->alive = false;
        b3Body_Disable(a->controller.body.body);
        if (a->role == SWAT_SUSPECT) s->events.hostile_down++;
    }
}

void swat_sim_shoot(SwatSim* s, int actor, b3Pos origin, b3Vec3 direction, SwatShot shot) {
    if (!shot.fired) return;
    SwatActor* a = &s->actors[actor];
    direction = swat_normalize(direction);
    a->tracer_start = origin;
    a->tracer_end = b3OffsetPos(origin,swat_mul(direction,shot.range));
    a->last_shot_tick = s->tick;
    float remaining = shot.range, energy = shot.energy;
    // Bounded material traversal. World casts, actor hits, and damage all use
    // the live Box3D scene; destroying a cell immediately removes its collider.
    for (int pass=0;pass<8 && remaining > 0.01f;pass++) {
        SwatHit hit = swat_world_ray(&s->world,origin,direction,remaining,a->controller.body.body);
        a->tracer_end = hit.point;
        if (!hit.hit) break;
        if (hit.kind == SWAT_HIT_ACTOR) {
            SwatActor* victim = &s->actors[hit.index];
            b3Pos feet = swat_body_feet_position(&victim->controller.body);
            float head_bonus = hit.point.y > feet.y+victim->controller.body.totalHeight-0.30f ? 1.5f : 1.0f;
            swat_sim_damage_actor(s,hit.index,actor,shot.damage*(energy/shot.energy)*head_bonus);
            break;
        }
        if (hit.index < 0) break;
        SwatObject* object = &s->world.objects[hit.index];
        float thickness = swat_world_exit_distance(object,hit.point,direction);
        float cost = swat_material_resistance(object->material)*fmaxf(thickness,0.01f);
        if (swat_world_damage(&s->world,hit.index,shot.damage*(energy/shot.energy)))
            s->events.destroyed++;
        energy -= cost;
        if (energy <= 0.01f || thickness <= 0) break;
        float advance = hit.distance+thickness+0.003f;
        remaining -= advance;
        origin = b3OffsetPos(hit.point,swat_mul(direction,thickness+0.003f));
    }
}

void swat_sim_bot_inputs(SwatSim* s, SwatInput inputs[SWAT_MAX_ACTORS]) {
    for (int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i] = swat_neutral_input();
    if (!s->actors[0].alive || !s->config.hostile_fire) return;
    for (int i=1;i<s->actor_count;i++) {
        SwatActor* a = &s->actors[i];
        if (!a->alive || a->role != SWAT_SUSPECT) continue;
        b3Pos eye = swat_controller_eye(&a->controller);
        b3Pos target = swat_controller_eye(&s->actors[0].controller);
        b3Vec3 delta = b3SubPos(target,eye);
        float distance = b3Length(delta);
        b3Vec3 direction = swat_normalize(delta);
        SwatHit hit = swat_world_ray(&s->world,eye,direction,distance+0.15f,a->controller.body.body);
        bool visible = distance < 26 && b3Dot(swat_controller_aim(&a->controller),direction) > cosf(50*SWAT_RAD) &&
            hit.kind == SWAT_HIT_ACTOR && hit.index == 0;
        if (!visible) { a->visible_ticks = 0; continue; }
        a->last_seen = target;
        a->visible_ticks++;
        float yaw = atan2f(delta.z,delta.x);
        float pitch = atan2f(delta.y,sqrtf(delta.x*delta.x+delta.z*delta.z));
        float yaw_error = swat_angle(yaw-a->controller.yaw);
        inputs[i].yaw_delta = swat_clamp(yaw_error,-3*SWAT_RAD,3*SWAT_RAD);
        inputs[i].pitch_delta = swat_clamp(pitch-a->controller.pitch,-2*SWAT_RAD,2*SWAT_RAD);
        inputs[i].aim = true;
        inputs[i].fire = a->visible_ticks > 36 && fabsf(yaw_error) < 2*SWAT_RAD && s->tick%18 == 0;
        inputs[i].reload = !a->arsenal.slots[a->arsenal.active].chambered;
    }
}

static void swat_actor_interact(SwatSim* s, int actor, const SwatInput* in) {
    SwatActor* a = &s->actors[actor];
    bool pressed = in->interact && !a->last_interact;
    a->last_interact = in->interact;
    if (!pressed) return;
    SwatHit hit = swat_world_ray(&s->world,swat_controller_eye(&a->controller),
        swat_controller_aim(&a->controller),2.2f,a->controller.body.body);
    if (hit.kind == SWAT_HIT_WORLD && hit.index >= 0 && s->world.objects[hit.index].door)
        s->world.objects[hit.index].door_open = !s->world.objects[hit.index].door_open;
}

static void swat_actor_weapon(SwatSim* s, int actor, const SwatInput* in) {
    SwatActor* a = &s->actors[actor];
    SwatController* c = &a->controller;
    b3Pos eye = swat_controller_eye(c);
    b3Vec3 forward,right,up;
    swat_controller_view(c,&forward,&right,&up);
    float barrel = a->arsenal.active == 0 ? 0.48f : 0.28f;
    b3Vec3 offset = swat_add(swat_mul(forward,barrel),
        swat_add(swat_mul(right,0.12f*(1.0f-c->ads)),swat_mul(up,-0.16f)));
    SwatHit clearance = swat_world_sphere_cast(&s->world,eye,offset,0.035f,c->body.body);
    c->muzzle_blocked = clearance.hit;
    b3Pos muzzle = b3OffsetPos(eye,offset);
    b3Vec3 velocity = b3Body_GetLinearVelocity(c->body.body);
    float speed = sqrtf(velocity.x*velocity.x+velocity.z*velocity.z);
    SwatShot shot = swat_weapons_step(&a->arsenal,in,c->ads,speed,c->body.onGround,
        c->muzzle_blocked || c->sprinting);
    if (!shot.fired) return;
    if (actor == 0) s->events.shots++;
    // Camera aim selects a target; the actual shot starts at the checked muzzle.
    b3Vec3 aim = swat_direction(c->yaw+c->recoil_yaw+shot.yaw_offset,
                                c->pitch+c->recoil_pitch+shot.pitch_offset);
    SwatHit sight = swat_world_ray(&s->world,eye,aim,shot.range,c->body.body);
    b3Vec3 direction = swat_normalize(b3SubPos(sight.point,muzzle));
    swat_sim_shoot(s,actor,muzzle,direction,shot);
    swat_controller_recoil(c,swat_weapon_def(a->arsenal.active)->recoil*SWAT_RAD,shot.recoil_yaw);
}

void swat_sim_step_inputs(SwatSim* s, const SwatInput inputs[SWAT_MAX_ACTORS]) {
    if (s->end != SWAT_RUNNING) return;
    memset(&s->events,0,sizeof(s->events));
    s->tick++;
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive)
        swat_actor_interact(s,i,&inputs[i]);
    swat_world_step_doors(&s->world);
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive)
        swat_controller_pre_step(&s->actors[i].controller,&inputs[i],swat_weapons_busy(&s->actors[i].arsenal));
    b3World_Step(s->world.id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive)
        swat_controller_post_step(&s->actors[i].controller,&inputs[i]);
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive && s->actors[i].role != SWAT_CIVILIAN)
        swat_actor_weapon(s,i,&inputs[i]);
    s->totals.hostile_damage += s->events.hostile_damage;
    s->totals.civilian_damage += s->events.civilian_damage;
    s->totals.officer_damage += s->events.officer_damage;
    s->totals.shots += s->events.shots;
    s->totals.destroyed += s->events.destroyed;
    s->totals.hostile_down += s->events.hostile_down;
    b3Pos feet = swat_body_feet_position(&s->actors[0].controller.body);
    if (s->totals.civilian_damage > 0) s->end = SWAT_CIVILIAN_HARMED;
    else if (!s->actors[0].alive) s->end = SWAT_OFFICER_DOWN;
    else if (feet.y < -8) s->end = SWAT_FALL;
    else if (swat_sim_hostiles(s) == 0 && b3Distance(feet,s->extraction) < 1.4f) s->end = SWAT_SUCCESS;
    else if (s->tick >= s->config.max_ticks) s->end = SWAT_TIMEOUT;
}

void swat_sim_step(SwatSim* s, const SwatInput* player) {
    SwatInput inputs[SWAT_MAX_ACTORS];
    swat_sim_bot_inputs(s,inputs);
    inputs[0] = *player;
    swat_sim_step_inputs(s,inputs);
}

static int swat_action(const float* action, int index, int max) {
    float value = isfinite(action[index]) ? action[index] : 0;
    return (int)swat_clamp(value,0,(float)max);
}

SwatInput swat_decode_action(const float action[SWAT_ACTION_HEADS]) {
    static const float yaw[] = {-6,-1,0,1,6};
    static const float pitch[] = {-4,-0.5f,0,0.5f,4};
    SwatInput in = swat_neutral_input();
    in.yaw_delta = yaw[swat_action(action,0,4)]*SWAT_RAD;
    in.pitch_delta = pitch[swat_action(action,1,4)]*SWAT_RAD;
    in.forward = (float)swat_action(action,2,2)-1;
    in.strafe = (float)swat_action(action,3,2)-1;
    in.lean = (float)swat_action(action,4,2)-1;
    in.crouch = swat_action(action,5,1) != 0;
    in.gait = (SwatGait)swat_action(action,6,2);
    in.aim = swat_action(action,7,1) != 0;
    in.fire = swat_action(action,8,1) != 0;
    in.reload = swat_action(action,9,1) != 0;
    in.weapon = swat_action(action,10,2);
    in.interact = swat_action(action,11,1) != 0;
    in.jump = swat_action(action,12,1) != 0;
    in.selector = swat_action(action,13,1) != 0;
    return in;
}

void swat_sim_observe(const SwatSim* s, int actor, float out[SWAT_OBS_SIZE]) {
    memset(out,0,SWAT_OBS_SIZE*sizeof(float));
    const SwatActor* a = &s->actors[actor];
    const SwatController* c = &a->controller;
    const SwatWeapon* w = &a->arsenal.slots[a->arsenal.active];
    const SwatWeaponDef* def = swat_weapon_def(a->arsenal.active);
    b3Vec3 v = b3Body_GetLinearVelocity(c->body.body);
    b3Vec3 f = swat_direction(c->yaw,0), r = swat_controller_right(c);
    out[0]=b3Dot(v,f)/4.6f; out[1]=b3Dot(v,r)/4.6f; out[2]=swat_clamp(v.y/15,-1,1);
    out[3]=sinf(c->yaw); out[4]=cosf(c->yaw); out[5]=sinf(c->pitch); out[6]=cosf(c->pitch);
    out[7]=c->body.onGround; out[8]=c->body.crouched; out[9]=c->lean;
    out[10]=c->ads; out[11]=c->sprinting; out[12]=c->stamina; out[13]=a->health/100;
    out[14]=(float)a->arsenal.active; out[15]=(float)w->magazine/def->capacity;
    out[16]=w->chambered; out[17]=(float)w->reserve/(def->capacity*3);
    out[18]=(float)w->reload_remaining/def->empty_reload_ticks;
    out[19]=(float)a->arsenal.equip_remaining/def->equip_ticks;
    out[20]=(float)w->mode/2; out[21]=(float)w->cooldown/def->shot_ticks;
    out[22]=c->recoil_pitch/(12*SWAT_RAD); out[23]=c->recoil_yaw/(6*SWAT_RAD);
    out[24]=c->muzzle_blocked; out[25]=fmaxf(0,c->body.jumpCooldown)/c->body.jumpCooldownTime;
    out[26]=fmaxf(0,1.0f-(float)s->tick/s->config.max_ticks);
    b3Vec3 goal = b3SubPos(s->extraction,swat_body_feet_position(&c->body));
    out[27]=swat_clamp(b3Dot(goal,f)/30,-1,1); out[28]=swat_clamp(b3Dot(goal,r)/30,-1,1);
    out[29]=(float)swat_sim_hostiles(s); out[30]=a->arsenal.last_fire; out[31]=a->last_shot_tick==s->tick;
    b3Vec3 forward,right,up;
    swat_controller_view(c,&forward,&right,&up);
    float tangent = tanf((70.0f-25.0f*c->ads)*0.5f*SWAT_RAD);
    b3Pos eye = swat_controller_eye(c);
    for (int row=0;row<SWAT_SENSOR_ROWS;row++) for (int col=0;col<SWAT_SENSOR_COLS;col++) {
        float x = ((float)col/(SWAT_SENSOR_COLS-1)*2-1)*tangent*(16.0f/9.0f);
        float y = (1-(float)row/(SWAT_SENSOR_ROWS-1)*2)*tangent;
        b3Vec3 dir = swat_normalize(swat_add(forward,swat_add(swat_mul(right,x),swat_mul(up,y))));
        SwatHit hit = swat_world_ray(&s->world,eye,dir,30,c->body.body);
        int offset=SWAT_PROPRIO_SIZE+(row*SWAT_SENSOR_COLS+col)*SWAT_SENSOR_CHANNELS;
        out[offset]=hit.distance/30;
        if (!hit.hit) continue;
        if (hit.kind == SWAT_HIT_ACTOR) {
            out[offset+1]=s->actors[hit.index].role==SWAT_CIVILIAN ? 1.0f : 0.8f;
            out[offset+2]=1.0f; // visible presence, not privileged target health
        } else if (hit.index >= 0) {
            const SwatObject* object = &s->world.objects[hit.index];
            out[offset+1]=object->door ? 0.6f : (object->max_health > 0 ? 0.4f : 0.2f);
            out[offset+2]=object->max_health > 0 ? object->health/object->max_health : 1;
        }
    }
}

const char* swat_end_name(SwatEnd end) {
    static const char* names[] = {"IN PROGRESS","AREA SECURED","OFFICER DOWN","CIVILIAN HARMED","TIME LIMIT","FALL"};
    return names[(int)end];
}
