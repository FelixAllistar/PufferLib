#include "sim.h"
#include "pose.h"
#include "motel.h"
#include <assert.h>
#include <string.h>

SwatConfig swat_default_config(void) {
    return (SwatConfig){.max_ticks=1800,.randomize=true,.hostile_fire=true,.mission=SWAT_ANNEX,
        .layout_seed=1,.generator=SWAT_LAYOUT_NEURAL,.difficulty=1};
}
const SwatMissionDef* swat_sim_mission(const SwatSim* s) { return &s->mission; }

void swat_sim_spawn_actor(SwatSim* s, int index, SwatRole role, b3Pos feet, float yaw) {
    SwatActor* a = &s->actors[index];
    if(a->present) b3DestroyBody(a->controller.body.body);
    memset(a,0,sizeof(*a));
    a->tag = (SwatTag){SWAT_HIT_ACTOR,index};
    a->role = role; a->health = 100; a->present = a->alive = true; a->last_shot_tick = -100;
    a->mind.target=a->mind.escort_owner=-1;
    a->mind.resolve=.2f+.75f*((s->reset_seed^(index*2654435761u))%1000)/1000.0f;
    a->target_actor = -1; a->last_foot_position = feet;
    swat_equipment_init(&a->gear);
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
        s->config = config; s->rng = rng; s->reset_seed=rng; s->episode = episode;
        swat_world_init(&s->world);
        s->mission=*swat_mission(config.mission);
        if(config.mission==SWAT_HOUSE) swat_mission_build_house(&s->world);
        else if(config.mission==SWAT_MOTEL) swat_motel_build(&s->world);
        else if(config.mission==SWAT_RANGE) swat_mission_build_test_range(&s->world);
        else if(config.mission==SWAT_GENERATED) {
            bool valid=swat_layout_generate(&s->layout,config.layout_seed,config.difficulty,(SwatGenerator)config.generator);
            // A known valid layout is the last-resort fallback for a badly
            // modified model. Keep the accepted tokens for exact reproduction.
            if(!valid) {
                valid=swat_layout_generate(&s->layout,config.layout_seed,config.difficulty,SWAT_LAYOUT_UNIFORM);
                s->config.generator=SWAT_LAYOUT_UNIFORM;
            }
            assert(valid);
            swat_layout_build(&s->world,&s->layout); s->mission=s->layout.mission;
        }
        else swat_world_build_range(&s->world,&s->rng,config.randomize);
        float shift = config.randomize ? (swat_rand01(&s->rng)-0.5f)*1.5f : 0;
        s->actor_count = 3;
        swat_sim_spawn_actor(s,0,SWAT_OFFICER,(b3Pos){0,0,0},0);
        swat_sim_spawn_actor(s,1,SWAT_SUSPECT,(b3Pos){17,0,2.9f+shift},SWAT_PI);
        swat_sim_spawn_actor(s,2,SWAT_CIVILIAN,(b3Pos){19,0,-4.8f},SWAT_PI);
        s->extraction = (b3Pos){21,0,0};
        if(config.mission==SWAT_HOUSE) {
            swat_sim_spawn_actor(s,0,SWAT_OFFICER,swat_mission(SWAT_HOUSE)->staging,0);
            swat_sim_spawn_actor(s,1,SWAT_SUSPECT,(b3Pos){15,0,-2},SWAT_PI);
            swat_sim_spawn_actor(s,2,SWAT_CIVILIAN,(b3Pos){16.5f,0,-3.2f},SWAT_PI);
            swat_sim_spawn_actor(s,6,SWAT_CIVILIAN,(b3Pos){16.5f,0,-1.3f},SWAT_PI);
            swat_sim_spawn_actor(s,7,SWAT_CIVILIAN,(b3Pos){16.5f,0,2.5f},SWAT_PI);
            swat_sim_spawn_actor(s,8,SWAT_SUSPECT,(b3Pos){13,0,3.4f},-SWAT_PI*.5f);
            s->actor_count=9; s->extraction=swat_mission(SWAT_HOUSE)->extraction;
        }
        if(config.mission==SWAT_GENERATED) {
            for(int i=0;i<3;i++) {
                b3DestroyBody(s->actors[i].controller.body.body); memset(&s->actors[i],0,sizeof(s->actors[i]));
            }
            swat_sim_spawn_actor(s,0,SWAT_OFFICER,s->mission.staging,0);
            s->actor_count=3;
            for(int i=0;i<s->layout.spawn_count;i++) {
                const SwatPlanSpawn* p=&s->layout.spawns[i];
                swat_sim_spawn_actor(s,p->actor,(SwatRole)p->role,p->feet,p->yaw);
                if(p->actor>=s->actor_count) s->actor_count=p->actor+1;
            }
            s->extraction=s->mission.extraction;
        }
        if(config.mission==SWAT_MOTEL) {
            swat_sim_spawn_actor(s,0,SWAT_OFFICER,s->mission.staging,-SWAT_PI*.5f);
            swat_sim_spawn_actor(s,1,SWAT_SUSPECT,(b3Pos){-2,0,-2},SWAT_PI*.5f);
            swat_sim_spawn_actor(s,2,SWAT_CIVILIAN,(b3Pos){-6,0,-2},SWAT_PI*.5f);
            swat_sim_spawn_actor(s,6,SWAT_CIVILIAN,(b3Pos){2,0,-2},SWAT_PI*.5f);
            swat_sim_spawn_actor(s,7,SWAT_CIVILIAN,(b3Pos){-10,0,-1.4f},SWAT_PI*.5f);
            swat_sim_spawn_actor(s,8,SWAT_SUSPECT,(b3Pos){6,0,-1.4f},SWAT_PI*.5f);
            s->actor_count=9; s->extraction=s->mission.extraction;
        }
        if(config.mission==SWAT_RANGE) {
            for(int i=1;i<s->actor_count;i++) if(s->actors[i].present) {
                b3DestroyBody(s->actors[i].controller.body.body); memset(&s->actors[i],0,sizeof(s->actors[i]));
            }
            s->actor_count=1; s->extraction=s->mission.extraction;
        }
        if(config.tactical_rules && config.mission==SWAT_GENERATED && config.difficulty==2) {
            for(int i=0;i<s->world.count;i++) if(s->world.objects[i].door && (i+config.layout_seed)%3==0) s->world.objects[i].trapped=true;
        }
        if(config.tactical_rules) for(int slot=1;slot<=config.squad_bots && slot<SWAT_MAX_PLAYERS;slot++) {
            swat_sim_set_player(s,slot,true);
            SwatMind* mind=&s->actors[swat_player_actor(slot)].mind; mind->bot=true; mind->team=slot<3 ? 1 : 2; mind->order=SWAT_ORDER_FALL_IN;
        }
        // Settle initial contacts without advancing game time, using the same
        // body controller and substeps as ordinary play.
        SwatInput neutral = swat_neutral_input();
        for (int t=0;t<6;t++) {
            for (int i=0;i<s->actor_count;i++) if(s->actors[i].present)
                swat_controller_pre_step(&s->actors[i].controller,&neutral,false);
            b3World_Step(s->world.id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);
            for (int i=0;i<s->actor_count;i++) if(s->actors[i].present)
                swat_controller_post_step(&s->actors[i].controller,&neutral);
        }
    }
}

void swat_sim_close(SwatSim* s) {
#pragma omp critical(swat_world_lifecycle)
    { swat_world_close(&s->world); }
}

int swat_player_actor(int slot) {
    return slot>=0 && slot<SWAT_MAX_PLAYERS ? (slot==0 ? 0 : slot+2) : -1;
}

bool swat_sim_set_player(SwatSim* s, int slot, bool present) {
    int index=swat_player_actor(slot);
    if(index<0) return false;
    SwatActor* actor=&s->actors[index];
    bool replace_bot=!present && s->config.tactical_rules && slot>0 && slot<=s->config.squad_bots;
    if(actor->present==present && !replace_bot) { if(present) actor->mind.bot=false; return true; }
    if(actor->present) b3DestroyBody(actor->controller.body.body);
    memset(actor,0,sizeof(*actor));
    if(present || replace_bot) {
        static const b3Pos spawn[SWAT_MAX_PLAYERS]={{0,0,0},{0,0,-1.4f},{-1.4f,0,1.4f},{-1.4f,0,-1.4f}};
        b3Pos feet=spawn[slot];
        if(s->config.mission!=SWAT_ANNEX) feet=b3OffsetPos(s->mission.staging,swat_v((float)feet.x,(float)feet.y,(float)feet.z));
        swat_sim_spawn_actor(s,index,SWAT_OFFICER,feet,0);
        if(replace_bot) { actor->mind.bot=true; actor->mind.team=slot<3 ? 1 : 2; actor->mind.order=SWAT_ORDER_FALL_IN; }
        if(index>=s->actor_count) s->actor_count=index+1;
    }
    return true;
}

int swat_sim_hostiles(const SwatSim* s) {
    int alive = 0;
    for (int i=0;i<s->actor_count;i++) alive += s->actors[i].alive && s->actors[i].role == SWAT_SUSPECT && !s->actors[i].gear.restrained;
    return alive;
}

int swat_sim_unsecured(const SwatSim* s) {
    int count=0;
    for(int i=0;i<s->actor_count;i++) if(s->actors[i].present && s->actors[i].alive &&
        s->actors[i].role==SWAT_CIVILIAN && !s->actors[i].gear.restrained) count++;
    return count;
}

SwatHitRegion swat_sim_hit_region(const SwatActor* actor,b3Pos point) {
    b3Pos feet=swat_body_feet_position(&actor->controller.body);
    float height=actor->controller.body.totalHeight,y=(float)(point.y-feet.y);
    if(y>height-.30f) return SWAT_HEAD;
    if(y<height*.46f) return SWAT_LEGS;
    b3Vec3 delta=b3SubPos(point,feet);
    float lateral=fabsf(b3Dot(delta,swat_controller_right(&actor->controller)));
    return lateral>.20f && y>height*.55f ? SWAT_ARMS : SWAT_TORSO;
}

void swat_sim_damage_region(SwatSim* s,int victim,int shooter,float damage,SwatHitRegion region,bool less_lethal) {
    if(victim<0 || victim>=s->actor_count || !s->actors[victim].alive || region<0 || region>=SWAT_HIT_REGIONS) return;
    SwatActor* a=&s->actors[victim];
    static const float multiplier[SWAT_HIT_REGIONS]={1.5f,1,.7f,.75f};
    float amount=damage*multiplier[region];
    if(region==SWAT_TORSO) amount*=1-swat_kit(a->gear.kit)->torso_protection;
    a->gear.wounds[region]=fminf(100,a->gear.wounds[region]+amount);
    if(less_lethal && (a->role==SWAT_SUSPECT || a->role==SWAT_CIVILIAN)) {
        a->gear.stunned_ticks=240;
        a->gear.surrendered=true;
    }
    swat_sim_damage_actor(s,victim,shooter,amount);
}

static b3Pos wand_sweep(const SwatSim* s,const SwatController* c,b3Pos start,b3Vec3 extension,bool* blocked) {
    SwatHit hit=swat_world_sphere_cast(&s->world,start,extension,.018f,c->body.body);
    if(blocked) *blocked=hit.hit;
    return b3OffsetPos(start,swat_mul(extension,hit.hit ? fmaxf(0,hit.fraction-.012f) : 1));
}
b3Vec3 swat_sim_inspection_direction(const SwatSim* s,int actor) {
    const SwatActor* a=&s->actors[actor];
    return swat_direction(a->controller.yaw+a->gear.wand_yaw,a->gear.wand_pitch);
}
b3Pos swat_sim_inspection_camera(const SwatSim* s,int actor) {
    const SwatController* c=&s->actors[actor].controller;
    const SwatEquipment* g=&s->actors[actor].gear;
    b3Pos start=swat_controller_eye(c);
    b3Vec3 forward=swat_direction(c->yaw,0),right=swat_v(-sinf(c->yaw),0,cosf(c->yaw));
    int mode=g->inspecting ? g->wand_mode : (c->body.crouched ? SWAT_WAND_UNDER : SWAT_WAND_FORWARD);
    if(mode==SWAT_WAND_UNDER) {
        start=b3OffsetPos(swat_body_feet_position(&c->body),swat_v(c->body.upperOffset.x,.045f,c->body.upperOffset.z));
    } else if(mode==SWAT_WAND_LEFT || mode==SWAT_WAND_RIGHT || mode==SWAT_WAND_OVER) {
        bool blocked=false;
        b3Vec3 offset=mode==SWAT_WAND_OVER ? swat_v(0,.55f,0) : swat_mul(right,mode==SWAT_WAND_LEFT ? -.85f : .85f);
        start=wand_sweep(s,c,start,offset,&blocked);
        if(blocked) return start;
        return wand_sweep(s,c,start,swat_mul(forward,.65f),NULL);
    }
    return wand_sweep(s,c,start,swat_mul(forward,1.15f),NULL);
}

void swat_sim_damage_actor(SwatSim* s, int victim, int shooter, float damage) {
    if (victim < 0 || victim >= s->actor_count || damage <= 0) return;
    SwatActor* a = &s->actors[victim];
    if (!a->alive) return;
    float amount = fminf(damage,a->health);
    if(s->config.tactical_rules && shooter>=0 && shooter<s->actor_count &&
       (s->actors[shooter].role==SWAT_OFFICER || s->actors[shooter].role==SWAT_SNIPER) &&
       (a->role==SWAT_CIVILIAN || a->gear.surrendered || a->gear.restrained)) {
        s->debrief.roe_violations++; s->debrief.unlawful_damage+=amount;
    }
    a->health -= amount;
    if (a->role == SWAT_OFFICER || a->role==SWAT_SNIPER) s->events.officer_damage += amount;
    if (a->role == SWAT_CIVILIAN) s->events.civilian_damage += amount;
    if (a->role == SWAT_SUSPECT && shooter>=0 && shooter<s->actor_count &&
        (s->actors[shooter].role==SWAT_OFFICER || s->actors[shooter].role==SWAT_SNIPER)) s->events.hostile_damage += amount;
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
    swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_SHOT,origin,3.0f,100);
    float remaining = shot.range, energy = shot.energy;
    // Bounded material traversal. World casts, actor hits, and damage all use
    // the live Box3D scene; destroying a cell immediately removes its collider.
    for (int pass=0;pass<8 && remaining > 0.01f;pass++) {
        SwatHit hit = swat_world_ray(&s->world,origin,direction,remaining,a->controller.body.body);
        a->tracer_end = hit.point;
        if (!hit.hit) break;
        if(hit.kind==SWAT_HIT_DEVICE) { swat_device_damage(s,hit.index,shot.damage); break; }
        SwatMaterial material=hit.kind==SWAT_HIT_WORLD && hit.index>=0 ? s->world.objects[hit.index].material : SWAT_CARPET;
        if(pass==0) swat_sound_surface(&s->sounds,s->tick,actor,SWAT_SOUND_IMPACT,hit.point,0.8f,25,material);
        if (hit.kind == SWAT_HIT_ACTOR) {
            SwatActor* victim = &s->actors[hit.index];
            bool less_lethal=a->arsenal.active==0 && swat_launcher_kind(a->arsenal.primary)>=0;
            swat_sim_damage_region(s,hit.index,actor,shot.damage*(energy/shot.energy),swat_sim_hit_region(victim,hit.point),less_lethal);
            break;
        }
        if (hit.index < 0) break;
        if(a->arsenal.active==0 && swat_launcher_kind(a->arsenal.primary)>=0) break;
        SwatObject* object = &s->world.objects[hit.index];
        float thickness = swat_world_exit_distance(object,hit.point,direction);
        float cost = swat_material_resistance(object->material)*fmaxf(thickness,0.01f);
        if (swat_world_damage(&s->world,hit.index,shot.damage*(energy/shot.energy)*(object->door && a->arsenal.active==0 && a->arsenal.primary==6 ? 3 : 1))) {
            s->events.destroyed++;
            swat_sound_surface(&s->sounds,s->tick,actor,SWAT_SOUND_BREAK,hit.point,1.2f,35,material);
        }
        energy -= cost;
        if (energy <= 0.01f || thickness <= 0) break;
        float advance = hit.distance+thickness+0.003f;
        remaining -= advance;
        origin = b3OffsetPos(hit.point,swat_mul(direction,thickness+0.003f));
    }
}

void swat_sim_bot_inputs(SwatSim* s, SwatInput inputs[SWAT_MAX_ACTORS]) {
    for (int i=0;i<SWAT_MAX_ACTORS;i++) inputs[i] = swat_neutral_input();
    if(s->config.tactical_rules) { swat_encounter_inputs(s,inputs); return; }
    if (!s->config.hostile_fire) return;
    for (int i=1;i<s->actor_count;i++) {
        SwatActor* a = &s->actors[i];
        if (!a->alive || a->role != SWAT_SUSPECT || a->gear.surrendered || a->gear.restrained) continue;
        b3Pos eye = swat_controller_eye(&a->controller);
        int target_index=-1; float closest=26;
        b3Pos target={0};
        for(int player=0;player<s->actor_count;player++) {
            const SwatActor* candidate=&s->actors[player];
            if(!candidate->present || !candidate->alive || (candidate->role!=SWAT_OFFICER && candidate->role!=SWAT_SNIPER)) continue;
            b3Pos position=swat_controller_eye(&candidate->controller);
            b3Vec3 delta=b3SubPos(position,eye);
            float distance=b3Length(delta);
            b3Vec3 direction=swat_normalize(delta);
            if(distance>=closest || b3Dot(swat_controller_aim(&a->controller),direction)<=cosf(50*SWAT_RAD)) continue;
            SwatHit hit=swat_world_ray(&s->world,eye,direction,distance+0.15f,a->controller.body.body);
            if(hit.kind==SWAT_HIT_ACTOR && hit.index==player) {
                target_index=player; closest=distance; target=position;
            }
        }
        SwatHeardSound heard;
        for(int n=0;n<4 && swat_hearing_next(&s->world,&s->sounds,s->tick,eye,i,&a->hearing,&heard);n++) {
            if(heard.gain>=0.015f && (heard.kind==SWAT_SOUND_SHOT || heard.kind==SWAT_SOUND_STEP ||
               heard.kind==SWAT_SOUND_DOOR || heard.kind==SWAT_SOUND_BREAK || heard.kind==SWAT_SOUND_FLASH)) {
                a->heard_yaw=heard.bearing; a->hearing_ticks=90;
            }
        }
        if(a->hearing_ticks>0) a->hearing_ticks--;
        if(target_index<0) {
            a->visible_ticks=0; a->target_actor=-1;
            if(a->hearing_ticks>0) inputs[i].yaw_delta=swat_clamp(swat_angle(a->heard_yaw-a->controller.yaw),-1.5f*SWAT_RAD,1.5f*SWAT_RAD);
            continue;
        }
        if(a->target_actor!=target_index) a->visible_ticks=0;
        a->target_actor=target_index;
        b3Vec3 delta=b3SubPos(target,eye);
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

static void swat_actor_interact(SwatSim* s, int actor, SwatInput* in) {
    SwatActor* a = &s->actors[actor];
    bool pressed = in->interact && !a->last_interact;
    a->last_interact = in->interact;
    if(!in->interact) { a->gear.cuff_ticks=0; a->gear.cuff_target=-1; return; }
    if(!pressed && !a->gear.cuff_ticks) { a->gear.cuff_target=-1; return; }
    if(pressed && (swat_device_recover(s,actor) || swat_collect_evidence(s,actor))) return;
    SwatHit hit = s->config.mission==SWAT_ANNEX ?
        swat_world_ray(&s->world,swat_controller_eye(&a->controller),
            swat_controller_aim(&a->controller),2.2f,a->controller.body.body) : swat_context_hit(s,actor,2.2f);
    if(hit.kind==SWAT_HIT_ACTOR && hit.index>=0 && a->role==SWAT_OFFICER && hit.distance<1.7f) {
        SwatActor* target=&s->actors[hit.index];
        if(s->config.tactical_rules && pressed && target->alive && target->role==SWAT_CIVILIAN && target->gear.restrained) {
            target->mind.escort_owner=target->mind.escort_owner==actor ? -1 : actor; return;
        }
        if(target->alive && (target->role==SWAT_SUSPECT || target->role==SWAT_CIVILIAN) && target->gear.surrendered && !target->gear.restrained) {
            in->fire=in->reload=false; in->forward=in->strafe=0;
            if(a->gear.cuff_target!=hit.index) {
                if(!pressed && a->gear.cuff_target>=0) { a->gear.cuff_ticks=0; a->gear.cuff_target=-1; return; }
                a->gear.cuff_ticks=0;
            }
            a->gear.cuff_target=hit.index;
            if(++a->gear.cuff_ticks>=72) {
                target->gear.restrained=true; a->gear.cuff_ticks=0;
                swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,hit.point,.3f,10);
            }
            return;
        }
    }
    a->gear.cuff_ticks=0; a->gear.cuff_target=-1;
    if (pressed && hit.kind == SWAT_HIT_WORLD && hit.index >= 0 && s->world.objects[hit.index].door) {
        if(s->world.objects[hit.index].locked || s->world.objects[hit.index].wedge_owner>=0 || s->world.objects[hit.index].breach_owner>=0) {
            swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,hit.point,.2f,8); return;
        }
        SwatObject* door=&s->world.objects[hit.index];
        if(in->peek) { door->door_open=!(door->door_open && door->peek); door->peek=true; }
        else { door->door_open=door->peek || !door->door_open; door->peek=false; }
        swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_DOOR,s->world.objects[hit.index].center,0.6f,18);
    }
}

static void swat_actor_equipment(SwatSim* s,int actor,SwatInput* in) {
    SwatActor* a=&s->actors[actor]; SwatEquipment* gear=&a->gear;
    if(gear->stunned_ticks>0) gear->stunned_ticks--;
    if(gear->tether_ticks>0) { gear->tether_ticks--; in->forward*=.15f; in->strafe*=.15f; in->jump=false; in->gait=SWAT_SLOW; }
    if(gear->melee_cooldown>0) gear->melee_cooldown--;
    if(gear->flash_ticks>0) gear->flash_ticks--;
    if(gear->taser_cooldown>0) gear->taser_cooldown--;
    if(gear->throw_cooldown>0) gear->throw_cooldown--;
    if(a->role==SWAT_OFFICER && in->loadout>0 && in->loadout<=SWAT_KIT_COUNT &&
        a->arsenal.shots==0 && !gear->used_tools && a->health==100 &&
        b3Distance(swat_body_feet_position(&a->controller.body),s->mission.staging)<3) {
        int kit=in->loadout-1;
        if(kit!=gear->kit) { swat_equipment_kit(gear,kit); swat_weapons_primary(&a->arsenal,swat_kit(kit)->less_lethal ? 2 : 0); }
    }
    bool staging=a->role==SWAT_OFFICER && !a->arsenal.shots && !gear->used_tools && a->health==100 &&
        b3Distance(swat_body_feet_position(&a->controller.body),s->mission.staging)<3;
    if(staging) {
        if(in->primary_profile>0 && in->primary_profile<=SWAT_WEAPON_PROFILES && in->primary_profile!=2 &&
           a->arsenal.primary!=in->primary_profile-1) swat_weapons_primary(&a->arsenal,in->primary_profile-1);
        if(in->sight_profile>0 && in->sight_profile<=SWAT_SIGHTS) a->arsenal.sight=in->sight_profile-1;
        if(in->primary_profile>0 || in->magazine_inventory) for(int slot=0;slot<2;slot++)
            swat_weapons_magazines(&a->arsenal.slots[slot],in->magazine_inventory,swat_arsenal_def(&a->arsenal,slot)->capacity);
    }
    bool was_inspecting=gear->inspecting;
    gear->inspecting=in->inspect && swat_kit(gear->kit)->optiwand && a->role==SWAT_OFFICER &&
        !gear->restrained && !gear->stunned_ticks && !gear->cuff_ticks && !gear->throw_cooldown;
    b3Pos eye=swat_controller_eye(&a->controller);
    if(gear->inspecting) {
        if(!was_inspecting) {
            SwatHit ahead=swat_world_ray(&s->world,eye,swat_direction(a->controller.yaw,0),1.25f,a->controller.body.body);
            bool door=ahead.kind==SWAT_HIT_WORLD && ahead.index>=0 && s->world.objects[ahead.index].door;
            gear->wand_mode=(door || in->crouch) ? SWAT_WAND_UNDER : SWAT_WAND_FORWARD;
            gear->wand_yaw=0; gear->wand_pitch=door ? 12*SWAT_RAD : a->controller.pitch;
        }
        if(in->crouch) gear->wand_mode=SWAT_WAND_UNDER;
        if(in->lean<-.5f) gear->wand_mode=SWAT_WAND_LEFT;
        if(in->lean>.5f) gear->wand_mode=SWAT_WAND_RIGHT;
        if(in->jump) gear->wand_mode=SWAT_WAND_OVER;
        gear->wand_yaw=swat_clamp(gear->wand_yaw+in->yaw_delta,-110*SWAT_RAD,110*SWAT_RAD);
        gear->wand_pitch=swat_clamp(gear->wand_pitch+in->pitch_delta,-80*SWAT_RAD,80*SWAT_RAD);
        *in=swat_neutral_input(); in->crouch=gear->wand_mode==SWAT_WAND_UNDER || a->controller.body.crouched;
    }
    if(gear->inspecting) {
        SwatHit inspected=swat_context_hit(s,actor,1.7f);
        if(inspected.kind==SWAT_HIT_WORLD && inspected.index>=0 && s->world.objects[inspected.index].trapped)
            s->world.objects[inspected.index].trap_known|=1u<<actor;
    }
    if(in->pepper_spray) { swat_pepper_spray(s,actor); in->fire=in->reload=in->melee=false; }
    bool throwing=in->throwable>0 && !gear->last_throw; gear->last_throw=in->throwable>0;
    bool tasing=in->taser && !gear->last_taser; gear->last_taser=in->taser;
    bool door_tool=in->door_tool!=SWAT_DOOR_NONE;
    swat_door_tools(s,actor,in);
    if(throwing && !door_tool) swat_throw(s,actor,(SwatProjectileKind)(in->throwable-1));
    if(tasing && !door_tool) swat_taser(s,actor);
    if(gear->throw_cooldown || gear->taser_cooldown>150) in->fire=in->reload=in->melee=false;
    bool command=in->command && !gear->last_command; gear->last_command=in->command;
    if(command && a->role==SWAT_OFFICER) {
        swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_COMMAND,eye,1.2f,22);
        SwatHit focused=swat_context_hit(s,actor,9);
        for(int i=0;i<s->actor_count;i++) {
            SwatActor* target=&s->actors[i];
            if(!target->alive || (target->role!=SWAT_SUSPECT && target->role!=SWAT_CIVILIAN) || target->gear.restrained) continue;
            b3Pos head=swat_controller_eye(&target->controller); b3Vec3 d=b3SubPos(head,eye); float distance=b3Length(d);
            bool aimed=focused.kind==SWAT_HIT_ACTOR && focused.index==i;
            if(!aimed && (distance>9 || b3Dot(swat_controller_aim(&a->controller),swat_normalize(d))<cosf(15*SWAT_RAD))) continue;
            SwatHit hit=aimed ? focused : swat_world_ray(&s->world,eye,swat_normalize(d),distance+.15f,a->controller.body.body);
            if(hit.kind==SWAT_HIT_ACTOR && hit.index==i &&
                (target->role==SWAT_CIVILIAN || target->gear.stunned_ticks || target->health<40 ||
                 (s->config.tactical_rules && target->mind.resolve<.48f))) target->gear.surrendered=true;
        }
    }
    bool melee=in->melee && !gear->last_melee; gear->last_melee=in->melee;
    if(melee && a->role==SWAT_OFFICER && !gear->melee_cooldown && a->controller.stamina>.08f && !gear->inspecting) {
        gear->melee_cooldown=48; a->controller.stamina-=.08f;
        SwatHit hit=swat_world_ray(&s->world,eye,swat_controller_aim(&a->controller),1.7f,a->controller.body.body);
        if(hit.hit) {
            SwatMaterial material=hit.kind==SWAT_HIT_WORLD && hit.index>=0 ? s->world.objects[hit.index].material : SWAT_CARPET;
            swat_sound_surface(&s->sounds,s->tick,actor,SWAT_SOUND_IMPACT,hit.point,1,25,material);
            if(hit.kind==SWAT_HIT_ACTOR) swat_sim_damage_region(s,hit.index,actor,5,swat_sim_hit_region(&s->actors[hit.index],hit.point),true);
            else if(hit.index>=0 && swat_world_damage(&s->world,hit.index,swat_kit(gear->kit)->ram ? 160 : 18)) {
                s->events.destroyed++; swat_sound_surface(&s->sounds,s->tick,actor,SWAT_SOUND_BREAK,hit.point,1.2f,35,material);
            }
        }
    }
    a->controller.mobility=swat_equipment_mobility(gear);
    if(gear->inspecting) { in->fire=in->reload=false; in->gait=SWAT_SLOW; in->aim=false; }
    bool escorted=s->config.tactical_rules && a->role==SWAT_CIVILIAN && gear->restrained && a->mind.escort_owner>=0;
    if(!escorted && (gear->restrained || gear->surrendered || gear->stunned_ticks)) {
        *in=swat_neutral_input(); in->crouch=true;
    }
    if(gear->cuff_ticks>0) { in->forward=in->strafe=0; in->fire=in->reload=false; }
}

static void swat_actor_weapon(SwatSim* s, int actor, const SwatInput* in) {
    SwatActor* a = &s->actors[actor];
    SwatController* c = &a->controller;
    b3Pos eye = swat_controller_eye(c);
    b3Vec3 forward,right,up;
    swat_controller_view(c,&forward,&right,&up);
    SwatPose pose=swat_pose(c,&a->arsenal);
    SwatHit clearance=swat_world_sphere_cast(&s->world,eye,b3SubPos(pose.muzzle,eye),.035f,c->body.body);
    c->muzzle_blocked=clearance.hit;
    b3Pos muzzle=pose.muzzle;
    b3Vec3 velocity = b3Body_GetLinearVelocity(c->body.body);
    float speed = sqrtf(velocity.x*velocity.x+velocity.z*velocity.z);
    SwatReloadStage old_stage=a->arsenal.slots[a->arsenal.active].reload_stage;
    int old_slot=a->arsenal.active,old_reload=a->arsenal.slots[old_slot].reload_remaining;
    SwatFireMode old_mode=a->arsenal.slots[old_slot].mode;
    SwatInput weapon_input=*in;
    int payload=a->arsenal.active==0 ? swat_launcher_kind(a->arsenal.primary) : -1;
    bool physical=payload>=0 && (s->config.tactical_rules || a->arsenal.primary!=2);
    bool projectile_room=false; for(int i=0;i<SWAT_MAX_PROJECTILES;i++) projectile_room|=!s->projectiles[i].active;
    if(fabsf(c->ready_blend)>.12f) weapon_input.fire=false;
    SwatShot shot = swat_weapons_step(&a->arsenal,&weapon_input,c->ads,speed,c->body.onGround,
        c->muzzle_blocked || c->sprinting || (physical && !projectile_room));
    int reload=a->arsenal.slots[a->arsenal.active].reload_remaining;
    if(old_stage!=a->arsenal.slots[a->arsenal.active].reload_stage || (!old_reload && reload) || (old_reload && !reload && old_slot==a->arsenal.active))
        swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_RELOAD,eye,0.25f,8);
    if(old_slot!=a->arsenal.active || old_mode!=a->arsenal.slots[old_slot].mode)
        swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,eye,0.15f,6);
    if (!shot.fired) return;
    if (actor == 0) s->events.shots++;
    // Camera aim selects a target; the actual shot starts at the checked muzzle.
    float injury_spread=1+2*swat_clamp(a->gear.wounds[SWAT_ARMS]/50,0,1);
    b3Vec3 aim = swat_direction(c->yaw+c->recoil_yaw+shot.yaw_offset*injury_spread,
                                c->pitch+c->recoil_pitch+shot.pitch_offset*injury_spread);
    SwatHit sight = swat_world_ray(&s->world,eye,aim,shot.range,c->body.body);
    b3Vec3 direction = swat_normalize(b3SubPos(sight.point,muzzle));
    if(physical) {
        swat_launch(s,actor,(SwatProjectileKind)payload,muzzle,direction,shot.damage,shot.range);
        a->tracer_start=muzzle; a->tracer_end=muzzle; a->last_shot_tick=s->tick;
        swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_SHOT,muzzle,.7f,30);
    } else swat_sim_shoot(s,actor,muzzle,direction,shot);
    swat_controller_recoil(c,swat_arsenal_def(&a->arsenal,a->arsenal.active)->recoil*SWAT_RAD,shot.recoil_yaw);
}

void swat_sim_step_inputs(SwatSim* s, const SwatInput requested[SWAT_MAX_ACTORS]) {
    if (s->end != SWAT_RUNNING) return;
    memset(&s->events,0,sizeof(s->events));
    s->tick++;
    SwatInput inputs[SWAT_MAX_ACTORS]; memcpy(inputs,requested,sizeof(inputs));
    swat_devices_inputs(s,inputs);
    swat_encounter_orders(s,inputs);
    swat_overwatch_inputs(s,inputs);
    for(int i=0;i<s->actor_count;i++) if(s->actors[i].alive) swat_actor_equipment(s,i,&inputs[i]);
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive)
        swat_actor_interact(s,i,&inputs[i]);
    swat_world_step_doors(&s->world);
    swat_traps_step(s);
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive)
        swat_controller_pre_step(&s->actors[i].controller,&inputs[i],swat_weapons_busy(&s->actors[i].arsenal));
    b3World_Step(s->world.id,SWAT_DT,SWAT_PHYSICS_SUBSTEPS);
    swat_projectiles_step(s);
    swat_devices_step(s);
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive)
        swat_controller_post_step(&s->actors[i].controller,&inputs[i]);
    for(int i=0;i<s->actor_count;i++) if(s->actors[i].alive) {
        SwatActor* a=&s->actors[i];
        b3Pos feet=swat_body_feet_position(&a->controller.body);
        float distance=b3Distance(feet,a->last_foot_position);
        a->last_foot_position=feet;
        if(a->controller.body.onGround && distance<0.3f) a->foot_distance+=distance;
        if(a->foot_distance>=0.85f) {
            a->foot_distance=0;
            float strength=a->controller.body.crouched ? 0.12f : (a->controller.sprinting ? 0.8f : 0.45f);
            SwatHit ground=swat_world_ray(&s->world,b3OffsetPos(feet,swat_v(0,.2f,0)),swat_v(0,-1,0),.6f,a->controller.body.body);
            SwatMaterial surface=ground.kind==SWAT_HIT_WORLD && ground.index>=0 ? s->world.objects[ground.index].material : SWAT_CONCRETE;
            swat_sound_surface(&s->sounds,s->tick,i,SWAT_SOUND_STEP,b3OffsetPos(feet,swat_v(0,0.1f,0)),strength*swat_material(surface)->footstep_gain,16,surface);
        }
    }
    for (int i=0;i<s->actor_count;i++) if (s->actors[i].alive && s->actors[i].role != SWAT_CIVILIAN)
        swat_actor_weapon(s,i,&inputs[i]);
    s->totals.hostile_damage += s->events.hostile_damage;
    s->totals.civilian_damage += s->events.civilian_damage;
    s->totals.officer_damage += s->events.officer_damage;
    s->totals.shots += s->events.shots;
    s->totals.destroyed += s->events.destroyed;
    s->totals.hostile_down += s->events.hostile_down;
    swat_encounter_step(s);
    int officers=0,living=0,extracted=0,fallen=0;
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i];
        if(!a->present || a->role!=SWAT_OFFICER) continue;
        officers++;
        if(!a->alive) continue;
        b3Pos feet=swat_body_feet_position(&a->controller.body);
        if(feet.y < -8) { a->alive=false; a->health=0; b3Body_Disable(a->controller.body.body); fallen++; continue; }
        living++;
        extracted+=b3Distance(feet,s->extraction)<1.4f;
    }
    if (s->totals.civilian_damage > 0) s->end = SWAT_CIVILIAN_HARMED;
    else if(officers && !living) s->end=fallen ? SWAT_FALL : SWAT_OFFICER_DOWN;
    else if(s->config.mission!=SWAT_RANGE && living && swat_sim_hostiles(s)==0 && extracted==living &&
        (s->config.mission==SWAT_ANNEX || !swat_sim_unsecured(s))) s->end=SWAT_SUCCESS;
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
    const SwatWeaponDef* def = swat_arsenal_def(&a->arsenal,a->arsenal.active);
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
