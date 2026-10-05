#include "sim.h"
#include <string.h>

static bool clear_to(const SwatSim* s,b3Pos from,b3Pos to,int target) {
    (void)target;
    return swat_world_visible(&s->world,from,to);
}

static bool contextual_hit(const SwatSim* s,SwatHit hit) {
    if(hit.kind==SWAT_HIT_WORLD && hit.index>=0) return s->world.objects[hit.index].door || swat_world_breachable(&s->world.objects[hit.index]);
    if(hit.kind==SWAT_HIT_DEVICE) return hit.index>=0 && hit.index<SWAT_MAX_DEVICES && s->devices[hit.index].active;
    if(hit.kind!=SWAT_HIT_ACTOR || hit.index<0 || hit.index>=s->actor_count) return false;
    const SwatActor* a=&s->actors[hit.index];
    return a->present && a->alive && (a->role==SWAT_SUSPECT || a->role==SWAT_CIVILIAN);
}

SwatHit swat_context_hit(const SwatSim* s,int actor,float range) {
    if(actor<0 || actor>=s->actor_count || !s->actors[actor].present) return (SwatHit){.index=-1};
    const SwatController* c=&s->actors[actor].controller;
    b3Pos eye=swat_controller_eye(c); b3Vec3 aim=swat_controller_aim(c);
    SwatHit direct=swat_world_ray(&s->world,eye,aim,range,c->body.body);
    if(contextual_hit(s,direct)) return direct;
    // Three degrees, not a magnet to the nearest person. An intervening wall
    // or actor is still the first hit for each individual sample.
    b3Vec3 right=swat_v(-sinf(c->yaw),0,cosf(c->yaw));
    b3Vec3 up=swat_direction(c->yaw,c->pitch+SWAT_PI*.5f);
    const float radius=.0524f;
    for(int i=0;i<8;i++) {
        float angle=i*SWAT_PI*.25f;
        b3Vec3 offset=swat_add(swat_mul(right,cosf(angle)*radius),swat_mul(up,sinf(angle)*radius));
        SwatHit hit=swat_world_ray(&s->world,eye,swat_normalize(swat_add(aim,offset)),range,c->body.body);
        if(contextual_hit(s,hit)) return hit;
    }
    return direct;
}

SwatContext swat_context(const SwatSim* s,int actor) {
    SwatContext out={.hit={.index=-1}};
    if(actor<0 || actor>=s->actor_count || !s->actors[actor].present) return out;
    out.hit=swat_context_hit(s,actor,9);
    if(out.hit.kind==SWAT_HIT_DEVICE && out.hit.index>=0 && out.hit.index<SWAT_MAX_DEVICES) {
        out.action=SWAT_CONTEXT_DEVICE; out.ready=out.hit.distance<2.2f && s->devices[out.hit.index].owner==actor; return out;
    }
    if(!contextual_hit(s,out.hit)) {
        if(s->config.tactical_rules) {
            b3Pos eye=swat_controller_eye(&s->actors[actor].controller); b3Vec3 aim=swat_controller_aim(&s->actors[actor].controller);
            for(int i=0;i<SWAT_MAX_ACTORS;i++) {
                const SwatEvidence* evidence=&s->evidence[i]; if(!evidence->dropped || evidence->collected) continue;
                b3Vec3 delta=b3SubPos(evidence->position,eye);
                if(b3Length(delta)<2.2f && b3Dot(aim,swat_normalize(delta))>cosf(12*SWAT_RAD) && clear_to(s,eye,evidence->position,-1)) {
                    out.action=SWAT_CONTEXT_EVIDENCE; out.ready=true; out.hit.index=i; out.hit.point=evidence->position; out.hit.distance=b3Length(delta); break;
                }
            }
        }
        return out;
    }
    if(out.hit.kind==SWAT_HIT_WORLD) {
        const SwatObject* door=&s->world.objects[out.hit.index];
        if(!door->door) {
            out.action=door->breach_owner>=0 ? SWAT_CONTEXT_CHARGE : SWAT_CONTEXT_WALL;
            out.ready=out.hit.distance<=1.7f; return out;
        }
        out.action=door->wedge_owner>=0 ? SWAT_CONTEXT_WEDGED :
            (door->trapped && (door->trap_known&(1u<<actor))) ? SWAT_CONTEXT_TRAP : door->breach_owner>=0 ? SWAT_CONTEXT_CHARGE : (door->locked ? SWAT_CONTEXT_LOCKED :
            (door->door_open ? SWAT_CONTEXT_CLOSE : SWAT_CONTEXT_OPEN));
        out.ready=out.hit.distance<=(door->locked ? 1.7f : 2.2f);
        if(door->locked && (door->door_open || door->door_angle>=.01f)) out.ready=false;
    } else {
        const SwatActor* target=&s->actors[out.hit.index];
        out.action=target->gear.restrained ? SWAT_CONTEXT_SECURED :
            (target->gear.surrendered ? SWAT_CONTEXT_CUFF : SWAT_CONTEXT_COMPLY);
        out.ready=out.action!=SWAT_CONTEXT_CUFF || out.hit.distance<1.7f;
    }
    return out;
}

static void breach(SwatSim* s,int object,int owner) {
    SwatObject* door=&s->world.objects[object]; b3Pos origin=door->breach_position;
    bool leaf=door->door; int removed=swat_world_breach(&s->world,object,origin);
    if(!removed) return;
    door->door_open=leaf; door->breach_ticks=24; s->events.destroyed+=removed;
    swat_sound_surface(&s->sounds,s->tick,owner,SWAT_SOUND_FLASH,origin,2.5f,80,door->material);
    // Gameplay blast exposure uses the same intact-wall visibility boundary
    // as throwables. Removing the leaf opens the aperture; its frame remains.
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i]; if(!a->present || !a->alive) continue;
        b3Pos chest=b3OffsetPos(swat_body_feet_position(&a->controller.body),swat_v(0,a->controller.body.totalHeight*.65f,0));
        float distance=b3Distance(origin,chest);
        if(distance>=3 || !swat_world_visible(&s->world,origin,chest)) continue;
        a->gear.flash_ticks=(int)fmaxf(a->gear.flash_ticks,(1-distance/3)*120);
        a->gear.stunned_ticks=(int)fmaxf(a->gear.stunned_ticks,(1-distance/3)*150);
        if(distance<1.5f) swat_sim_damage_region(s,i,owner,(1-distance/1.5f)*100,SWAT_TORSO,false);
    }
}

void swat_door_tools(SwatSim* s,int actor,SwatInput* in) {
    SwatActor* a=&s->actors[actor]; SwatEquipment* g=&a->gear;
    int mode=in->door_tool,previous=g->last_door_tool; g->last_door_tool=mode;
    bool allowed=a->alive && a->role==SWAT_OFFICER && !g->inspecting && !g->stunned_ticks &&
        !g->restrained && !g->surrendered && !g->cuff_ticks && !g->throw_cooldown && g->taser_cooldown<=150 &&
        !swat_weapons_busy(&a->arsenal);
    if(mode>SWAT_DOOR_NONE && mode<SWAT_DOOR_TOOLS) in->fire=in->reload=in->melee=false;
    if(allowed && mode==SWAT_DETONATE_CHARGE && previous!=mode) {
        for(int i=0;i<s->world.count;i++) {
            const SwatObject* door=&s->world.objects[i];
            if(swat_world_breachable(door) && door->breach_owner==actor) breach(s,i,actor);
        }
        in->fire=in->reload=in->melee=false;
    }
    if(mode==SWAT_DOOR_NONE || mode!=previous) g->door_completed=false;
    if(!allowed || g->door_completed || (mode!=SWAT_LOCKPICK && mode!=SWAT_PLACE_CHARGE &&
       mode!=SWAT_WEDGE && mode!=SWAT_REMOVE_WEDGE && mode!=SWAT_DISARM)) {
        g->door_ticks=0; g->door_target=-1; g->door_mode=SWAT_DOOR_NONE; return;
    }
    SwatHit hit=swat_context_hit(s,actor,1.7f);
    SwatObject* door=hit.kind==SWAT_HIT_WORLD && hit.index>=0 ? &s->world.objects[hit.index] : NULL;
    bool usable=door && swat_world_breachable(door) && (door->door || mode==SWAT_PLACE_CHARGE) && !door->door_open && door->door_angle<.01f && door->breach_owner<0 &&
        (mode==SWAT_LOCKPICK ? door->locked : mode==SWAT_PLACE_CHARGE ? (g->breaching_charges>0 && door->max_health>0) :
         mode==SWAT_WEDGE ? (door->wedge_owner<0 && g->wedges>0) : mode==SWAT_REMOVE_WEDGE ? door->wedge_owner>=0 :
         (door->trapped && (door->trap_known&(1u<<actor))));
    if(!usable) { g->door_ticks=0; g->door_target=-1; g->door_mode=SWAT_DOOR_NONE; return; }
    if(g->door_target!=hit.index || g->door_mode!=mode) g->door_ticks=0;
    g->door_target=hit.index; g->door_mode=mode;
    if(!g->door_ticks) swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,hit.point,.12f,5);
    bool crouch=in->crouch; float yaw=in->yaw_delta,pitch=in->pitch_delta;
    *in=swat_neutral_input(); in->crouch=crouch; in->yaw_delta=yaw; in->pitch_delta=pitch;
    int duration=mode==SWAT_LOCKPICK ? 180 : mode==SWAT_DISARM ? 120 : mode==SWAT_PLACE_CHARGE ? 90 : 45;
    if(++g->door_ticks>=duration) {
        if(mode==SWAT_LOCKPICK) door->locked=false;
        else if(mode==SWAT_PLACE_CHARGE) { door->breach_owner=actor; door->breach_position=hit.point; g->breaching_charges--; }
        else if(mode==SWAT_WEDGE) { door->wedge_owner=actor; g->wedges--; }
        else if(mode==SWAT_REMOVE_WEDGE) { door->wedge_owner=-1; g->wedges=(int)fminf(2,g->wedges+1); }
        else { door->trapped=false; door->trap_known=0; }
        g->door_completed=true; s->world.generation++;
        g->used_tools=true; g->door_ticks=0; g->door_target=-1; g->door_mode=SWAT_DOOR_NONE;
        swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,hit.point,.18f,6);
    }
}
bool swat_throw(SwatSim* s,int actor,SwatProjectileKind kind) {
    if(actor<0 || actor>=s->actor_count || kind<0 || kind>SWAT_CS_GAS) return false;
    SwatActor* a=&s->actors[actor]; SwatEquipment* g=&a->gear;
    int* stock=kind==SWAT_FLASHBANG ? &g->flashbangs : &g->gas_grenades;
    if(!a->alive || a->role!=SWAT_OFFICER || *stock<=0 || g->throw_cooldown || g->inspecting ||
       g->stunned_ticks || g->restrained || g->cuff_ticks || g->taser_cooldown>150) return false;
    int slot=-1; for(int i=0;i<SWAT_MAX_PROJECTILES;i++) if(!s->projectiles[i].active) { slot=i; break; }
    if(slot<0) return false;
    b3Pos eye=swat_controller_eye(&a->controller);
    b3Vec3 direction=swat_controller_aim(&a->controller);
    b3Vec3 extension=swat_add(swat_mul(direction,.62f),swat_v(0,-.13f,0));
    if(swat_world_sphere_cast(&s->world,eye,extension,.06f,a->controller.body.body).hit) return false;
    SwatProjectile* p=&s->projectiles[slot]; memset(p,0,sizeof(*p));
    p->tag=(SwatTag){SWAT_HIT_PROJECTILE,slot}; p->active=true; p->kind=kind; p->owner=actor; p->target=-1;
    p->position=b3OffsetPos(eye,extension); p->remaining_ticks=90; p->last_impact_tick=-100;
    p->velocity=swat_add(swat_mul(direction,10),swat_v(0,2.2f,0));
    p->velocity=swat_add(p->velocity,b3Body_GetLinearVelocity(a->controller.body.body));
    b3BodyDef bd=b3DefaultBodyDef(); bd.type=b3_dynamicBody; bd.position=p->position;
    bd.linearVelocity=p->velocity; bd.angularVelocity=swat_v(0,0,12); bd.angularDamping=.4f;
    bd.isBullet=true; bd.userData=&p->tag;
    p->body=b3CreateBody(s->world.id,&bd);
    b3ShapeDef sd=b3DefaultShapeDef(); sd.baseMaterial=swat_physics_material(SWAT_STEEL);
    // Effective density gives the hollow canister a 0.42 kg game mass.
    sd.density=.42f/(4.0f/3*SWAT_PI*.045f*.045f*.045f); sd.enableHitEvents=true;
    b3Sphere sphere={{0,0,0},.045f}; p->shape=b3CreateSphereShape(p->body,&sd,&sphere);
    (*stock)--; g->throw_cooldown=45; g->used_tools=true;
    swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,eye,.2f,8);
    return true;
}
bool swat_taser(SwatSim* s,int actor) {
    if(actor<0 || actor>=s->actor_count) return false;
    SwatActor* a=&s->actors[actor]; SwatEquipment* g=&a->gear;
    if(!a->alive || a->role!=SWAT_OFFICER || g->taser_charges<=0 || g->taser_cooldown || g->throw_cooldown ||
       g->inspecting || g->stunned_ticks || g->restrained || g->cuff_ticks) return false;
    b3Pos eye=swat_controller_eye(&a->controller); b3Vec3 aim=swat_controller_aim(&a->controller);
    if(swat_world_sphere_cast(&s->world,eye,swat_mul(aim,.3f),.025f,a->controller.body.body).hit) return false;
    g->taser_charges--; g->taser_cooldown=180; g->used_tools=true;
    SwatHit hit=swat_world_ray(&s->world,eye,aim,7,a->controller.body.body);
    a->tracer_start=eye; a->tracer_end=hit.point; a->last_shot_tick=s->tick;
    swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_TASER,eye,.4f,12);
    if(hit.kind==SWAT_HIT_ACTOR && hit.index>=0) {
        SwatActor* target=&s->actors[hit.index];
        target->gear.stunned_ticks=240;
        if(target->role==SWAT_SUSPECT || target->role==SWAT_CIVILIAN) target->gear.surrendered=true;
    }
    return true;
}
static bool gas_kind(SwatProjectileKind kind);
static float gas_radius(const SwatProjectile* p);
float swat_gas_at(const SwatSim* s,b3Pos position) {
    float exposure=0;
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        const SwatProjectile* p=&s->projectiles[i];
        if(!p->active || !p->detonated || !gas_kind(p->kind)) continue;
        float radius=gas_radius(p);
        b3Pos source=b3OffsetPos(p->position,swat_v(0,.18f,0));
        float distance=b3Distance(source,position);
        if(distance<radius && clear_to(s,source,position,-1))
            exposure=fmaxf(exposure,(1-distance/radius)*fminf(1,p->remaining_ticks/120.0f));
    }
    return exposure;
}
static bool gas_kind(SwatProjectileKind kind) { return kind==SWAT_CS_GAS || kind==SWAT_LAUNCH_CS || kind==SWAT_PEPPERBALL; }
static bool flash_kind(SwatProjectileKind kind) { return kind==SWAT_FLASHBANG || kind==SWAT_LAUNCH_FLASH; }
static float gas_radius(const SwatProjectile* p) { return p->kind==SWAT_PEPPERBALL ? 1.25f : fminf(3.5f,.8f+fmaxf(0,p->age-90)*SWAT_DT*.6f); }
static void detonate(SwatSim* s,SwatProjectile* p) {
    p->detonated=true; p->remaining_ticks=flash_kind(p->kind) ? 24 : p->kind==SWAT_PEPPERBALL ? 180 : gas_kind(p->kind) ? 720 : p->kind==SWAT_PROBE ? 300 : 12;
    b3DestroyBody(p->body); p->body=b3_nullBodyId; p->shape=b3_nullShapeId; p->velocity=swat_v(0,0,0);
    swat_sound_emit(&s->sounds,s->tick,p->owner,flash_kind(p->kind) ? SWAT_SOUND_FLASH : gas_kind(p->kind) ? SWAT_SOUND_GAS : SWAT_SOUND_IMPACT,
                    p->position,flash_kind(p->kind) ? 2 : .65f,flash_kind(p->kind) ? 65 : 18);
    if(!flash_kind(p->kind)) return;
    b3Pos origin=b3OffsetPos(p->position,swat_v(0,.07f,0));
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i]; if(!a->present || !a->alive) continue;
        b3Pos eye=swat_controller_eye(&a->controller); float distance=b3Distance(origin,eye);
        if(distance>8 || !clear_to(s,origin,eye,i)) continue;
        float facing=b3Dot(swat_controller_aim(&a->controller),swat_normalize(b3SubPos(origin,eye)));
        int ticks=(int)((1-distance/8)*(facing>.15f ? 240 : 65));
        if(ticks>a->gear.flash_ticks) a->gear.flash_ticks=ticks;
        if(a->role!=SWAT_OFFICER && ticks>a->gear.stunned_ticks) a->gear.stunned_ticks=ticks;
    }
}
void swat_projectiles_step(SwatSim* s) {
    // Read contact events before destroying any body: Box3D event memory and
    // shape IDs are only valid until the next world mutation.
    b3ContactEvents events=b3World_GetContactEvents(s->world.id);
    for(int i=0;i<events.hitCount;i++) {
        const b3ContactHitEvent* e=&events.hitEvents[i];
        SwatTag* a=b3Body_GetUserData(b3Shape_GetBody(e->shapeIdA));
        SwatTag* b=b3Body_GetUserData(b3Shape_GetBody(e->shapeIdB));
        SwatTag* projectile=a && a->kind==SWAT_HIT_PROJECTILE ? a : (b && b->kind==SWAT_HIT_PROJECTILE ? b : NULL);
        SwatTag* surface=projectile==a ? b : a;
        if(!projectile || !surface || surface->kind!=SWAT_HIT_WORLD) continue;
        SwatProjectile* p=&s->projectiles[projectile->index];
        if(s->tick-p->last_impact_tick<6 || e->approachSpeed<.6f) continue;
        p->last_impact_tick=s->tick;
        swat_sound_surface(&s->sounds,s->tick,p->owner,SWAT_SOUND_IMPACT,e->point,
            swat_clamp(e->approachSpeed*.1f,.1f,1),20,s->world.objects[surface->index].material);
    }
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        SwatProjectile* p=&s->projectiles[i]; if(!p->active) continue;
        p->age++; p->remaining_ticks--;
        if(!p->detonated) {
            b3Pos next=b3Body_GetPosition(p->body); p->velocity=b3Body_GetLinearVelocity(p->body);
            if(p->kind>=SWAT_PEPPERBALL) {
                SwatHit hit=swat_world_sphere_cast(&s->world,p->position,b3SubPos(next,p->position),p->kind==SWAT_PEPPERBALL ? .018f : .035f,p->body);
                if(hit.hit) {
                    next=b3OffsetPos(hit.point,swat_mul(hit.normal,.04f));
                    if(hit.kind==SWAT_HIT_ACTOR && hit.index>=0 && hit.index!=p->owner) {
                        if(p->kind==SWAT_PROBE) { p->target=hit.index; p->attachment=b3InvRotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),s->actors[hit.index].controller.yaw),b3SubPos(hit.point,swat_body_feet_position(&s->actors[hit.index].controller.body))); p->attachment.y/=s->actors[hit.index].controller.body.totalHeight; }
                        else if(p->kind==SWAT_BOLA) {
                            SwatHitRegion region=swat_sim_hit_region(&s->actors[hit.index],hit.point);
                            if(region==SWAT_LEGS || region==SWAT_TORSO) { s->actors[hit.index].gear.tether_ticks=180; s->actors[hit.index].gear.surrendered=true; }
                        } else if(p->damage>0) swat_sim_damage_region(s,hit.index,p->owner,p->damage,swat_sim_hit_region(&s->actors[hit.index],hit.point),p->kind==SWAT_IMPACT_ROUND);
                    } else if(hit.kind==SWAT_HIT_DEVICE) swat_device_damage(s,hit.index,p->damage);
                    p->position=next; detonate(s,p);
                }
            }
            p->position=next;
            if(!p->detonated && !p->remaining_ticks) detonate(s,p);
        } else if(!p->remaining_ticks) p->active=false;
        if(p->active && p->detonated && p->kind==SWAT_PROBE && p->target>=0 && s->actors[p->target].present) {
            b3Vec3 offset=p->attachment; offset.y*=s->actors[p->target].controller.body.totalHeight;
            offset=b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),s->actors[p->target].controller.yaw),offset);
            p->position=b3OffsetPos(swat_body_feet_position(&s->actors[p->target].controller.body),offset);
            b3Pos source=swat_controller_eye(&s->actors[p->owner].controller);
            if(b3Distance(source,p->position)<10 && clear_to(s,source,p->position,p->target)) {
                for(int j=0;j<i;j++) {
                    const SwatProjectile* other=&s->projectiles[j];
                    if(other->active && other->detonated && other->kind==SWAT_PROBE && other->owner==p->owner && other->target==p->target &&
                       b3Distance(other->position,p->position)>.12f && clear_to(s,source,other->position,p->target)) {
                        SwatActor* target=&s->actors[p->target]; target->gear.stunned_ticks=120;
                        if(target->role==SWAT_SUSPECT || target->role==SWAT_CIVILIAN) target->gear.surrendered=true;
                    }
                }
            }
        }
        if(p->active && p->detonated && gas_kind(p->kind) && p->age%30==0)
            swat_sound_emit(&s->sounds,s->tick,p->owner,SWAT_SOUND_GAS,p->position,.3f,10);
    }
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i]; if(!a->alive || !a->present) continue;
        b3Pos chest=b3OffsetPos(swat_body_feet_position(&a->controller.body),swat_v(0,a->controller.body.totalHeight*.7f,0));
        // Ignore the exposed actor itself while checking the gas boundary.
        bool exposed=false;
        for(int j=0;j<SWAT_MAX_PROJECTILES;j++) {
            const SwatProjectile* p=&s->projectiles[j];
            if(!p->active || !p->detonated || !gas_kind(p->kind)) continue;
            b3Pos origin=b3OffsetPos(p->position,swat_v(0,.18f,0));
            float radius=gas_radius(p);
            if(b3Distance(origin,chest)<radius && clear_to(s,origin,chest,i)) exposed=true;
        }
        bool mask=(a->role==SWAT_OFFICER || a->role==SWAT_SNIPER) && swat_kit(a->gear.kit)->gas_mask;
        if(exposed && !mask) a->gear.gas_ticks=(int)fminf(180,a->gear.gas_ticks+3);
        else if(a->gear.gas_ticks>0) a->gear.gas_ticks--;
        if(a->gear.gas_ticks>30 && (a->role==SWAT_SUSPECT || a->role==SWAT_CIVILIAN)) a->gear.surrendered=true;
    }
}

void swat_pepper_spray(SwatSim* s,int actor) {
    SwatActor* a=&s->actors[actor]; SwatEquipment* g=&a->gear;
    if(!a->present || !a->alive || a->role!=SWAT_OFFICER || g->spray_ticks<=0 ||
       g->stunned_ticks || g->restrained || g->inspecting || g->cuff_ticks || swat_weapons_busy(&a->arsenal)) return;
    b3Pos eye=swat_controller_eye(&a->controller);
    b3Vec3 aim=swat_controller_aim(&a->controller);
    if(swat_world_sphere_cast(&s->world,eye,swat_mul(aim,.2f),.025f,a->controller.body.body).hit) return;
    g->spray_ticks--; g->used_tools=true;
    if(s->tick%15==0) swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_GAS,eye,.15f,5);
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* target=&s->actors[i]; if(i==actor || !target->present || !target->alive) continue;
        b3Pos head=swat_controller_eye(&target->controller); b3Vec3 delta=b3SubPos(head,eye);
        if(b3Length(delta)>2.5f || b3Dot(aim,swat_normalize(delta))<cosf(12*SWAT_RAD) || !clear_to(s,eye,head,i)) continue;
        if((target->role==SWAT_OFFICER || target->role==SWAT_SNIPER) && swat_kit(target->gear.kit)->gas_mask) continue;
        target->gear.gas_ticks=(int)fminf(180,target->gear.gas_ticks+5);
        if(target->gear.gas_ticks>30 && (target->role==SWAT_SUSPECT || target->role==SWAT_CIVILIAN)) {
            target->gear.surrendered=true; target->gear.stunned_ticks=(int)fmaxf(target->gear.stunned_ticks,90);
        }
    }
}
void swat_traps_step(SwatSim* s) {
    for(int i=0;i<s->world.count;i++) {
        SwatObject* door=&s->world.objects[i];
        if(!door->active || !door->door || !door->trapped || door->door_angle<4*SWAT_RAD) continue;
        door->trapped=false; door->trap_known=0;
        swat_sound_emit(&s->sounds,s->tick,-1,SWAT_SOUND_FLASH,door->center,2,60);
        for(int j=0;j<s->actor_count;j++) {
            SwatActor* a=&s->actors[j]; if(!a->present || !a->alive) continue;
            b3Pos eye=swat_controller_eye(&a->controller); float distance=b3Distance(eye,door->center);
            b3Vec3 normal=swat_v(cosf(door->yaw),0,-sinf(door->yaw));
            float side=b3Dot(normal,b3SubPos(eye,door->center))>=0 ? 1 : -1;
            b3Pos source=b3OffsetPos(door->center,swat_mul(normal,side*(door->half.x+.03f)));
            if(distance<5 && clear_to(s,source,eye,j)) {
                a->gear.flash_ticks=(int)fmaxf(a->gear.flash_ticks,(1-distance/5)*240);
                a->gear.stunned_ticks=(int)fmaxf(a->gear.stunned_ticks,(1-distance/5)*180);
            }
        }
    }
}

int swat_launcher_kind(int profile) {
    switch(profile) { case 2:return SWAT_IMPACT_ROUND; case 5:return SWAT_PEPPERBALL; case 8:return SWAT_LAUNCH_CS; case 9:return SWAT_LAUNCH_FLASH; case 10:return SWAT_PROBE; case 11:return SWAT_BOLA; default:return -1; }
}
bool swat_launch(SwatSim* s,int actor,SwatProjectileKind kind,b3Pos muzzle,b3Vec3 direction,float damage,float range) {
    if(actor<0 || actor>=s->actor_count || kind<SWAT_PEPPERBALL || kind>=SWAT_PROJECTILE_KINDS) return false;
    int slot=-1; for(int i=0;i<SWAT_MAX_PROJECTILES;i++) if(!s->projectiles[i].active) { slot=i; break; }
    if(slot<0) return false;
    SwatProjectile* p=&s->projectiles[slot]; memset(p,0,sizeof(*p)); p->tag=(SwatTag){SWAT_HIT_PROJECTILE,slot};
    p->active=true; p->owner=actor; p->target=-1; p->kind=kind; p->position=muzzle; p->damage=damage;
    float speed=kind==SWAT_PROBE ? 45 : kind==SWAT_PEPPERBALL ? 30 : 24;
    p->remaining_ticks=(int)ceilf(range/speed/SWAT_DT); p->last_impact_tick=-100;
    p->velocity=swat_add(swat_mul(direction,speed),b3Body_GetLinearVelocity(s->actors[actor].controller.body.body));
    b3BodyDef bd=b3DefaultBodyDef(); bd.type=b3_dynamicBody; bd.position=muzzle; bd.linearVelocity=p->velocity; bd.isBullet=true; bd.userData=&p->tag;
    p->body=b3CreateBody(s->world.id,&bd); b3ShapeDef sd=b3DefaultShapeDef(); sd.baseMaterial=swat_physics_material(SWAT_CARPET); sd.enableHitEvents=true;
    float radius=kind==SWAT_PEPPERBALL ? .018f : .035f; sd.density=.04f/(4.0f/3*SWAT_PI*radius*radius*radius);
    b3Sphere sphere={{0,0,0},radius}; p->shape=b3CreateSphereShape(p->body,&sd,&sphere); return true;
}
