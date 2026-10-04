#include "sim.h"
#include <string.h>

int swat_sniper_actor(int unit) { return unit>=0 && unit<SWAT_SNIPERS ? 9+unit : -1; }
const char* swat_sniper_status(SwatSniperStatus status) {
    static const char* names[]={"UNASSIGNED","REPOSITIONING","HOLDING / NO MARK","TARGET READY",
        "LINE OF SIGHT BLOCKED","HOLD / FRIENDLY IN LINE","NO ARMED TARGET","CYCLING / RELOADING","SNIPER DOWN","STEADYING AIM"};
    return names[status>=0 && status<SWAT_SNIPER_STATUSES ? status : 0];
}
// Glass is optically transparent. Visibility never penetrates opaque cover;
// firing still uses the ordinary material/thickness/energy shot path.
static SwatHit optical(const SwatSim* s,int actor,b3Pos origin,b3Vec3 direction,float range) {
    SwatHit hit={0}; float travelled=0;
    for(int pass=0;pass<8;pass++) {
        hit=swat_world_ray(&s->world,origin,direction,range,s->actors[actor].controller.body.body);
        if(hit.kind!=SWAT_HIT_WORLD || hit.index<0 || s->world.objects[hit.index].material!=SWAT_GLASS) break;
        float distance=swat_world_exit_distance(&s->world.objects[hit.index],hit.point,direction)+.003f;
        travelled+=hit.distance+distance; range-=hit.distance+distance;
        origin=b3OffsetPos(hit.point,swat_mul(direction,distance));
    }
    hit.distance+=travelled; return hit;
}
bool swat_sniper_safe(const SwatSim* s,int unit,b3Vec3 direction,float distance) {
    int actor=swat_sniper_actor(unit); if(actor<0 || !s->actors[actor].present) return false;
    b3Pos origin=swat_controller_eye(&s->actors[actor].controller);
    for(int i=0;i<s->actor_count;i++) {
        const SwatActor* a=&s->actors[i];
        if(i==actor || !a->present || !a->alive || (a->role==SWAT_SUSPECT && !a->gear.surrendered && !a->gear.restrained)) continue;
        b3Pos feet=swat_body_feet_position(&a->controller.body);
        // Check a conservative capsule around every friendly/compliant actor.
        // This also catches a hostage beside the reticle, before random spread.
        for(int sample=0;sample<5;sample++) {
            b3Vec3 delta=b3SubPos(b3OffsetPos(feet,swat_v(0,.25f+sample*(a->controller.body.totalHeight-.4f)/4,0)),origin);
            float along=b3Dot(delta,direction);
            if(along<0 || along>distance+.5f) continue;
            if(b3Length(swat_add(delta,swat_mul(direction,-along)))<a->controller.body.bodyRadius+.12f) return false;
        }
    }
    return true;
}
static void place_sniper(SwatSim* s,int unit) {
    SwatSniper* sniper=&s->snipers[unit];
    const SwatOverwatch* post=&swat_sim_mission(s)->overwatch[sniper->post];
    b3Vec3 aim=b3SubPos(post->target,post->position);
    int actor=swat_sniper_actor(unit);
    b3Pos feet=b3OffsetPos(post->position,swat_v(0,-1.6256f,0));
    bool existed=s->actors[actor].present;
    if(!existed) swat_sim_spawn_actor(s,actor,SWAT_SNIPER,feet,atan2f(aim.z,aim.x));
    SwatActor* a=&s->actors[actor];
    b3Body_SetTransform(a->controller.body.body,b3OffsetPos(feet,swat_v(0,a->controller.body.totalHeight*.5f+.01f,0)),b3Quat_identity);
    b3Body_SetLinearVelocity(a->controller.body.body,swat_v(0,0,0));
    a->controller.yaw=atan2f(aim.z,aim.x); a->controller.pitch=atan2f(aim.y,hypotf(aim.x,aim.z));
    if(!existed || a->arsenal.primary!=3+sniper->rifle) swat_weapons_primary(&a->arsenal,3+sniper->rifle);
    if(actor>=s->actor_count) s->actor_count=actor+1;
    sniper->status=SWAT_SNIPER_WATCHING;
}
void swat_overwatch_inputs(SwatSim* s,SwatInput* inputs) {
    int commander=s->commander_actor;
    if(commander<0 || commander>=s->actor_count || !s->actors[commander].alive) return;
    SwatInput requested=inputs[commander];
    bool edge=requested.sniper_order && !s->last_sniper_order[commander];
    s->last_sniper_order[commander]=requested.sniper_order;
    int selected=requested.sniper_unit;
    if(selected<0 || selected>=SWAT_SNIPERS) selected=0;
    if(requested.sniper_order==SWAT_SNIPER_ASSIGN) {
        SwatSniper* sniper=&s->snipers[selected];
        int post=requested.sniper_post,rifle=requested.sniper_rifle;
        bool occupied=false;
        for(int i=0;i<SWAT_SNIPERS;i++) if(i!=selected && s->snipers[i].deployed && s->snipers[i].post==post) occupied=true;
        bool unchanged=sniper->deployed && sniper->post==post && sniper->rifle==rifle;
        bool down=sniper->deployed && !s->actors[swat_sniper_actor(selected)].alive;
        if(post>=0 && post<swat_sim_mission(s)->overwatch_count && rifle>=0 && rifle<2 && !occupied && !unchanged && !down) {
            bool used=sniper->deployed && s->actors[swat_sniper_actor(selected)].arsenal.shots>0;
            if(!used || rifle==sniper->rifle) {
                // Moving preserves spent ammunition. Rifles can be selected
                // before the sniper's first shot, without a reload exploit.
                sniper->post=post; sniper->rifle=rifle; sniper->target=-1;
                bool redeploy=sniper->deployed;
                sniper->deployed=true;
                sniper->travel_ticks=redeploy && s->tick>2 ? 180 : 0;
                if(!sniper->travel_ticks) place_sniper(s,selected);
            }
        }
    }
    if(edge && requested.sniper_order==SWAT_SNIPER_HOLD)
        for(int i=0;i<SWAT_SNIPERS;i++) s->snipers[i].target=-1;
    for(int unit=0;unit<SWAT_SNIPERS;unit++) {
        SwatSniper* sniper=&s->snipers[unit]; if(!sniper->deployed) continue;
        int actor=swat_sniper_actor(unit); SwatActor* a=&s->actors[actor];
        SwatInput* input=&inputs[actor]; *input=swat_neutral_input(); input->aim=true;
        if(!a->alive) { sniper->status=SWAT_SNIPER_DOWN; continue; }
        if(sniper->travel_ticks>0) {
            sniper->travel_ticks--; sniper->status=SWAT_SNIPER_MOVING;
            if(!sniper->travel_ticks) place_sniper(s,unit);
            continue;
        }
        bool manual=requested.sniper_control && unit==selected;
        b3Vec3 velocity=b3Body_GetLinearVelocity(a->controller.body.body);
        bool steady=a->controller.ads>=.98f && a->controller.body.onGround && hypotf(velocity.x,velocity.z)<.05f &&
            fabsf(a->controller.recoil_yaw)<.002f && fabsf(a->controller.recoil_pitch)<.002f;
        if(manual) {
            input->yaw_delta=requested.yaw_delta; input->pitch_delta=requested.pitch_delta;
            input->reload=requested.reload;
        }
        b3Pos eye=swat_controller_eye(&a->controller);
        b3Vec3 aim=swat_direction(a->controller.yaw+input->yaw_delta,a->controller.pitch+input->pitch_delta);
        if(manual && edge && requested.sniper_order==SWAT_SNIPER_DESIGNATE) {
            SwatHit hit=optical(s,actor,eye,aim,150);
            if(hit.kind==SWAT_HIT_ACTOR && hit.index>=0 && s->actors[hit.index].role==SWAT_SUSPECT &&
               !s->actors[hit.index].gear.surrendered && !s->actors[hit.index].gear.restrained) {
                sniper->target=hit.index;
                b3Pos feet=swat_body_feet_position(&s->actors[hit.index].controller.body);
                sniper->target_height=swat_clamp((float)(hit.point.y-feet.y)/s->actors[hit.index].controller.body.totalHeight,.2f,.95f);
            } else { sniper->target=-1; sniper->status=SWAT_SNIPER_NO_TARGET; }
        }
        bool ready=false;
        if(sniper->target>=0 && sniper->target<s->actor_count) {
            SwatActor* target=&s->actors[sniper->target];
            if(!target->alive || target->gear.surrendered || target->gear.restrained) { sniper->target=-1; sniper->status=SWAT_SNIPER_NO_TARGET; }
            else {
                b3Pos point=b3OffsetPos(swat_body_feet_position(&target->controller.body),swat_v(0,target->controller.body.totalHeight*sniper->target_height,0));
                b3Vec3 delta=b3SubPos(point,eye); float distance=b3Length(delta); b3Vec3 direction=swat_normalize(delta);
                SwatHit hit=optical(s,actor,eye,direction,distance+.5f);
                bool visible=hit.kind==SWAT_HIT_ACTOR && hit.index==sniper->target && distance<swat_arsenal_def(&a->arsenal,0)->range;
                bool safe=swat_sniper_safe(s,unit,direction,distance);
                sniper->status=!visible ? SWAT_SNIPER_BLOCKED : (!safe ? SWAT_SNIPER_CROSS_FIRE : SWAT_SNIPER_READY);
                if(visible && (!manual || (edge && requested.sniper_order==SWAT_SNIPER_EXECUTE))) {
                    input->yaw_delta=swat_clamp(swat_angle(atan2f(delta.z,delta.x)-a->controller.yaw),-.4f,.4f);
                    input->pitch_delta=swat_clamp(atan2f(delta.y,hypotf(delta.x,delta.z))-a->controller.pitch,-.3f,.3f);
                    ready=safe && fabsf(swat_angle(atan2f(delta.z,delta.x)-a->controller.yaw))<.01f &&
                        fabsf(atan2f(delta.y,hypotf(delta.x,delta.z))-a->controller.pitch)<.01f;
                }
            }
        } else if(sniper->status!=SWAT_SNIPER_NO_TARGET) sniper->status=SWAT_SNIPER_WATCHING;
        // The scope is visible while the rifle comes up, but that transition
        // still has the ordinary hip-fire spread. Wait for a settled aim before
        // either firing route so READY and the friendly corridor remain useful.
        if(edge && requested.sniper_order==SWAT_SNIPER_EXECUTE && ready && steady) input->fire=true;
        if(manual && requested.fire && steady) {
            SwatHit hit=optical(s,actor,eye,aim,swat_arsenal_def(&a->arsenal,0)->range);
            if(swat_sniper_safe(s,unit,aim,hit.distance)) input->fire=true;
            else sniper->status=SWAT_SNIPER_CROSS_FIRE;
        }
        if(!steady && (sniper->status==SWAT_SNIPER_READY || sniper->status==SWAT_SNIPER_WATCHING)) sniper->status=SWAT_SNIPER_STEADYING;
        if(!a->arsenal.slots[0].chambered && !a->arsenal.slots[0].reload_remaining) input->reload=true;
        if(swat_weapons_busy(&a->arsenal) || a->arsenal.slots[0].cooldown) sniper->status=SWAT_SNIPER_RECOVERING;
    }
    if(requested.sniper_control) {
        inputs[commander]=swat_neutral_input(); inputs[commander].loadout=requested.loadout;
    }
}
