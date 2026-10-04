#include "sim.h"
#include <string.h>
const char* swat_device_name(SwatDeviceKind kind) {
    static const char* names[]={"Throwable camera","Ground robot","Communication ball","Drone"};
    return names[kind>=0 && kind<SWAT_DEVICE_KINDS ? kind : 0];
}
b3Pos swat_device_eye(const SwatDevice* d) { return b3OffsetPos(d->position,swat_v(0,d->kind==SWAT_ROBOT ? .14f : .09f,0)); }
bool swat_feed_present(const SwatSim* s,int unit) {
    if(unit<0 || unit>=SWAT_SNIPERS+SWAT_MAX_DEVICES) return false;
    if(unit<SWAT_SNIPERS) return s->mission.overwatch_count>0 && s->snipers[unit].deployed && s->actors[swat_sniper_actor(unit)].alive;
    const SwatDevice* d=&s->devices[unit-SWAT_SNIPERS]; return d->active && d->health>0 && d->battery_ticks>0;
}
bool swat_device_deploy(SwatSim* s,int actor,SwatDeviceKind kind) {
    if(actor<0 || actor>=s->actor_count || kind<0 || kind>=SWAT_DEVICE_KINDS) return false;
    SwatActor* a=&s->actors[actor]; SwatEquipment* g=&a->gear;
    if(!a->present || !a->alive || a->role!=SWAT_OFFICER || g->devices[kind]<=0 || g->throw_cooldown || g->stunned_ticks || g->inspecting || g->cuff_ticks || swat_weapons_busy(&a->arsenal)) return false;
    int index=-1; for(int i=0;i<SWAT_MAX_DEVICES;i++) if(!s->devices[i].active) { index=i; break; }
    if(index<0) return false;
    b3Pos eye=swat_controller_eye(&a->controller); b3Vec3 aim=swat_controller_aim(&a->controller);
    b3Vec3 extension=swat_add(swat_mul(aim,.65f),swat_v(0,-.12f,0));
    float radius=kind==SWAT_DRONE ? .18f : .10f;
    if(swat_world_sphere_cast(&s->world,eye,extension,radius,a->controller.body.body).hit) return false;
    SwatDevice* d=&s->devices[index]; memset(d,0,sizeof(*d));
    d->tag=(SwatTag){SWAT_HIT_DEVICE,index}; d->active=true; d->kind=kind; d->owner=actor; d->health=40;
    d->battery_ticks=18000; d->yaw=a->controller.yaw; d->pitch=0; d->last_command_tick=-100;
    d->position=b3OffsetPos(eye,extension);
    d->velocity=swat_add(swat_mul(aim,kind==SWAT_DRONE ? 0 : 6),b3Body_GetLinearVelocity(a->controller.body.body));
    b3BodyDef bd=b3DefaultBodyDef(); bd.type=b3_dynamicBody; bd.position=d->position; bd.linearVelocity=d->velocity;
    bd.gravityScale=kind==SWAT_DRONE ? 0 : 1; bd.linearDamping=kind==SWAT_DRONE ? 3 : .3f; bd.angularDamping=3; bd.isBullet=true; bd.userData=&d->tag;
    d->body=b3CreateBody(s->world.id,&bd);
    b3ShapeDef sd=b3DefaultShapeDef(); sd.baseMaterial=swat_physics_material(SWAT_CARPET); sd.density=2/(4.0f/3*SWAT_PI*radius*radius*radius);
    b3Sphere sphere={{0,0,0},radius}; d->shape=b3CreateSphereShape(d->body,&sd,&sphere);
    g->devices[kind]--; g->throw_cooldown=45; g->used_tools=true;
    swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,eye,.2f,8); return true;
}
bool swat_device_recover(SwatSim* s,int actor) {
    if(actor<0 || actor>=s->actor_count || !s->actors[actor].alive || s->actors[actor].role!=SWAT_OFFICER) return false;
    SwatActor* a=&s->actors[actor]; b3Pos eye=swat_controller_eye(&a->controller); b3Vec3 aim=swat_controller_aim(&a->controller);
    (void)eye; (void)aim; SwatHit hit=swat_context_hit(s,actor,2.2f);
    if(hit.kind!=SWAT_HIT_DEVICE || hit.index<0 || hit.index>=SWAT_MAX_DEVICES) return false;
    SwatDevice* d=&s->devices[hit.index];
    if(!d->active || d->owner!=actor || d->health<=0 || a->gear.devices[d->kind]>=1) return false;
    b3DestroyBody(d->body); d->body=b3_nullBodyId; d->shape=b3_nullShapeId; d->active=false;
    a->gear.devices[d->kind]++; swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_HANDLE,d->position,.2f,6); return true;
}
void swat_device_damage(SwatSim* s,int index,float damage) {
    if(index<0 || index>=SWAT_MAX_DEVICES || !isfinite(damage) || damage<=0) return;
    SwatDevice* d=&s->devices[index]; if(!d->active) return;
    d->health=fmaxf(0,d->health-damage);
    if(d->health<=0 && B3_IS_NON_NULL(d->body)) { b3DestroyBody(d->body); d->body=b3_nullBodyId; d->shape=b3_nullShapeId; d->active=false; }
}
void swat_devices_inputs(SwatSim* s,SwatInput inputs[]) {
    for(int actor=0;actor<s->actor_count;actor++) {
        SwatActor* a=&s->actors[actor]; if(!a->present || !a->alive || a->role!=SWAT_OFFICER) continue;
        SwatInput* in=&inputs[actor];
        bool deploy=in->device_deploy && !a->gear.last_device; a->gear.last_device=in->device_deploy!=0;
        if(deploy && in->device_deploy<=SWAT_DEVICE_KINDS) swat_device_deploy(s,actor,(SwatDeviceKind)(in->device_deploy-1));
        if(!in->device_control) continue;
        int index=in->device_unit; SwatInput requested=*in;
        *in=swat_neutral_input(); in->crouch=a->controller.body.crouched; // Officer stays physical and vulnerable.
        if(index<0 || index>=SWAT_MAX_DEVICES || a->gear.stunned_ticks || a->gear.restrained) continue;
        SwatDevice* d=&s->devices[index];
        if(!d->active || d->owner!=actor || d->health<=0 || d->battery_ticks<=0) continue;
        d->yaw=swat_angle(d->yaw+requested.yaw_delta); d->pitch=swat_clamp(d->pitch+requested.pitch_delta,-80*SWAT_RAD,80*SWAT_RAD);
        b3Vec3 velocity=b3Body_GetLinearVelocity(d->body);
        b3Vec3 forward=swat_direction(d->yaw,0),right=swat_v(-sinf(d->yaw),0,cosf(d->yaw));
        if(d->kind==SWAT_ROBOT || d->kind==SWAT_DRONE) {
            float speed=d->kind==SWAT_DRONE ? 1.8f : .9f;
            b3Vec3 wish=swat_add(swat_mul(forward,requested.forward),swat_mul(right,requested.strafe));
            if(b3Length(wish)>1) wish=swat_normalize(wish);
            velocity.x=wish.x*speed; velocity.z=wish.z*speed;
            if(d->kind==SWAT_DRONE) velocity.y=((float)requested.jump-(float)requested.crouch)*speed;
            b3Body_SetLinearVelocity(d->body,velocity);
        }
        if(requested.command && s->tick-d->last_command_tick>=90 && d->kind==SWAT_BALL) {
            d->last_command_tick=s->tick;
            swat_sound_emit(&s->sounds,s->tick,actor,SWAT_SOUND_COMMAND,d->position,1.2f,20);
            for(int i=0;i<s->actor_count;i++) {
                SwatActor* target=&s->actors[i]; if(!target->present || !target->alive || (target->role!=SWAT_SUSPECT && target->role!=SWAT_CIVILIAN)) continue;
                b3Pos head=swat_controller_eye(&target->controller);
                if(b3Distance(d->position,head)<9 && swat_world_visible(&s->world,swat_device_eye(d),head) &&
                   (target->role==SWAT_CIVILIAN || target->mind.resolve<.45f || target->gear.stunned_ticks)) target->gear.surrendered=true;
            }
        }
    }
}
void swat_devices_step(SwatSim* s) {
    for(int i=0;i<SWAT_MAX_DEVICES;i++) {
        SwatDevice* d=&s->devices[i]; if(!d->active) continue;
        d->age++; if(d->battery_ticks>0) d->battery_ticks--;
        d->position=b3Body_GetPosition(d->body); d->velocity=b3Body_GetLinearVelocity(d->body);
        if(d->position.y<-8 || fabsf((float)d->position.x)>100 || fabsf((float)d->position.z)>100) { swat_device_damage(s,i,100); continue; }
        if(!d->battery_ticks && d->kind==SWAT_DRONE) b3Body_SetGravityScale(d->body,1);
        // A device emits physical noise at its actual location, perceived through the ordinary path model.
        if((d->kind==SWAT_DRONE || (d->kind==SWAT_ROBOT && hypotf(d->velocity.x,d->velocity.z)>.1f)) && s->tick%15==0)
            swat_sound_emit(&s->sounds,s->tick,d->owner,SWAT_SOUND_HANDLE,d->position,.4f,12);
    }
}
