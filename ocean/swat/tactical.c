#include "sim.h"
#include <string.h>

static bool clear_to(const SwatSim* s,b3Pos from,b3Pos to,int target) {
    (void)target;
    return swat_world_visible(&s->world,from,to);
}
bool swat_throw(SwatSim* s,int actor,SwatProjectileKind kind) {
    if(actor<0 || actor>=s->actor_count || kind<0 || kind>=SWAT_PROJECTILE_KINDS) return false;
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
    p->tag=(SwatTag){SWAT_HIT_PROJECTILE,slot}; p->active=true; p->kind=kind; p->owner=actor;
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
float swat_gas_at(const SwatSim* s,b3Pos position) {
    float exposure=0;
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        const SwatProjectile* p=&s->projectiles[i];
        if(!p->active || !p->detonated || p->kind!=SWAT_CS_GAS) continue;
        float radius=fminf(3.5f,.8f+(p->age-90)*SWAT_DT*.6f);
        b3Pos source=b3OffsetPos(p->position,swat_v(0,.18f,0));
        float distance=b3Distance(source,position);
        if(distance<radius && clear_to(s,source,position,-1))
            exposure=fmaxf(exposure,(1-distance/radius)*fminf(1,p->remaining_ticks/120.0f));
    }
    return exposure;
}
static void detonate(SwatSim* s,SwatProjectile* p) {
    p->detonated=true; p->remaining_ticks=p->kind==SWAT_FLASHBANG ? 24 : 720;
    b3DestroyBody(p->body); p->body=b3_nullBodyId; p->shape=b3_nullShapeId; p->velocity=swat_v(0,0,0);
    swat_sound_emit(&s->sounds,s->tick,p->owner,p->kind==SWAT_FLASHBANG ? SWAT_SOUND_FLASH : SWAT_SOUND_GAS,
                    p->position,p->kind==SWAT_FLASHBANG ? 2 : .65f,p->kind==SWAT_FLASHBANG ? 65 : 18);
    if(p->kind!=SWAT_FLASHBANG) return;
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
            p->position=b3Body_GetPosition(p->body); p->velocity=b3Body_GetLinearVelocity(p->body);
            if(!p->remaining_ticks) detonate(s,p);
        } else if(!p->remaining_ticks) p->active=false;
        if(p->active && p->detonated && p->kind==SWAT_CS_GAS && p->age%30==0)
            swat_sound_emit(&s->sounds,s->tick,p->owner,SWAT_SOUND_GAS,p->position,.3f,10);
    }
    for(int i=0;i<s->actor_count;i++) {
        SwatActor* a=&s->actors[i]; if(!a->alive || !a->present) continue;
        b3Pos chest=b3OffsetPos(swat_body_feet_position(&a->controller.body),swat_v(0,a->controller.body.totalHeight*.7f,0));
        // Ignore the exposed actor itself while checking the gas boundary.
        bool exposed=false;
        for(int j=0;j<SWAT_MAX_PROJECTILES;j++) {
            const SwatProjectile* p=&s->projectiles[j];
            if(!p->active || !p->detonated || p->kind!=SWAT_CS_GAS) continue;
            b3Pos origin=b3OffsetPos(p->position,swat_v(0,.18f,0));
            float radius=fminf(3.5f,.8f+(p->age-90)*SWAT_DT*.6f);
            if(b3Distance(origin,chest)<radius && clear_to(s,origin,chest,i)) exposed=true;
        }
        bool mask=(a->role==SWAT_OFFICER || a->role==SWAT_SNIPER) && swat_kit(a->gear.kit)->gas_mask;
        if(exposed && !mask) a->gear.gas_ticks=(int)fminf(180,a->gear.gas_ticks+3);
        else if(a->gear.gas_ticks>0) a->gear.gas_ticks--;
        if(a->gear.gas_ticks>30 && (a->role==SWAT_SUSPECT || a->role==SWAT_CIVILIAN)) a->gear.surrendered=true;
    }
}
