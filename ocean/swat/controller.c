#include "controller.h"
#include <string.h>

SwatInput swat_neutral_input(void) {
    SwatInput input = {0};
    input.gait = SWAT_WALK;
    return input;
}

void swat_controller_init(SwatController* c, b3WorldId world, b3Pos feet, float yaw) {
    swat_controller_init_category(c,world,feet,yaw,UINT64_MAX);
}
void swat_controller_init_category(SwatController* c, b3WorldId world, b3Pos feet, float yaw, uint64_t category) {
    memset(c, 0, sizeof(*c));
    swat_body_init_category(&c->body, world, b3OffsetPos(feet, swat_v(0,0.9144f+0.02f,0)),category);
    c->body.walkSpeed = 2.8f;
    c->body.runSpeed = 4.6f;
    c->body.crouchSpeed = 1.25f;
    c->body.jumpSpeed = 4.2f;
    c->body.stepUpHeight = 0.30f;
    c->body.stepDownHeight = 0.30f;
    c->yaw = yaw;
    c->stamina = 1.0f;
    c->mobility = 1.0f;
    c->eye_height = c->body.standHeight - 0.2032f;
}

b3Vec3 swat_controller_right(const SwatController* c) {
    b3CosSin heading=b3ComputeCosSin(c->yaw);
    return swat_v(-heading.sine,0,heading.cosine);
}

static b3RayResult mantle_ray(const SwatController* c,b3Pos from,b3Vec3 translation) {
    return b3World_CastRayClosest(c->body.world,from,translation,(b3QueryFilter){.categoryBits=UINT64_MAX,.maskBits=c->body.queryMask});
}
// The path is placed, not physically pushed, so it is swept with a body-width
// (55%) hull rather than the generous 0.41 m movement radius; that lets the
// rise travel forward over the obstacle instead of hopping straight up. The
// landing is still checked with the full hull.
#define SWAT_MANTLE_WIDTH .55f
static bool mantle_sweep(const SwatController* c,b3Pos from,b3Pos to) {
    SwatTraceResult t=swat_body_trace_body(&c->body,from,to,SWAT_MANTLE_WIDTH,1);
    return !t.startedSolid && (!t.hit || t.fraction>=.999f);
}
// Looks for a 0.40-1.25 m obstacle ahead and a clear path over it (vault) or
// onto it (climb). The body is already crouched, so every hull test uses the
// crouched hull the move will actually travel in.
static bool mantle_plan(SwatController* c,b3Vec3 forward) {
    b3Pos feet=swat_body_feet_position(&c->body);b3Vec3 up=swat_v(0,1,0);
    float distance=-1;
    for(int k=0;k<2 && distance<0;k++) {
        b3RayResult wall=mantle_ray(c,b3OffsetPos(feet,swat_v(0,k?.65f:.35f,0)),swat_mul(forward,.75f));
        if(wall.hit && fabsf(wall.normal.y)<.5f) distance=.75f*wall.fraction;
    }
    if(distance<0) return false;
    b3Pos above=b3OffsetPos(feet,swat_add(swat_mul(forward,distance+.06f),swat_mul(up,SWAT_MANTLE_MAX+.25f)));
    b3RayResult top=mantle_ray(c,above,swat_mul(up,-(SWAT_MANTLE_MAX+.25f)));
    if(!top.hit || top.normal.y<.7f) return false;
    float height=(float)(top.point.y-feet.y);
    if(height<SWAT_MANTLE_MIN || height>SWAT_MANTLE_MAX) return false;
    float lift=height+.10f,radius=c->body.bodyRadius*SWAT_MANTLE_WIDTH;
    // Rise in front of the obstacle: the hull's leading edge stays off its face.
    b3Pos rise=b3OffsetPos(feet,swat_add(swat_mul(forward,fmaxf(0,distance-radius-.03f)),swat_mul(up,lift)));
    // Vault: first clear landing beyond the obstacle near the starting level.
    for(float s=c->body.bodyRadius+.05f;s<=c->body.bodyRadius+1.25f;s+=.10f) {
        b3Pos probe=b3OffsetPos(feet,swat_add(swat_mul(forward,distance+s),swat_mul(up,lift)));
        b3RayResult floor=mantle_ray(c,probe,swat_mul(up,-(lift+.45f)));
        if(!floor.hit || floor.normal.y<.7f) continue;
        float drop=(float)(floor.point.y-feet.y);
        if(drop>height-.25f) continue;              // still on the obstacle top
        if(fabsf(drop)>.45f) break;                 // unknown drop: do not vault blind
        b3Pos land={probe.x,floor.point.y+.01f,probe.z},cross={probe.x,rise.y,probe.z};
        if(!swat_body_clear_at(&c->body,land)) continue;   // still over the far face
        if(mantle_sweep(c,feet,rise) && mantle_sweep(c,rise,cross) && mantle_sweep(c,cross,land)) {
            c->mantle=(SwatMantle){.active=true,.climb=false,.total=(int)lrintf((.42f+.25f*height)/SWAT_DT),.path={feet,rise,cross,land}};
            return true;
        }
        break;
    }
    // Climb: stand (crouched) on the top, a body radius in from its edge.
    b3Pos on=b3OffsetPos(feet,swat_mul(forward,distance+c->body.bodyRadius+.08f));on.y=top.point.y+.01f;
    b3Pos over={on.x,rise.y,on.z};
    if(swat_body_clear_at(&c->body,on) && mantle_sweep(c,feet,rise) && mantle_sweep(c,rise,over) && mantle_sweep(c,over,on)) {
        c->mantle=(SwatMantle){.active=true,.climb=true,.total=(int)lrintf((.50f+.30f*height)/SWAT_DT),.path={feet,rise,over,on}};
        return true;
    }
    return false;
}
// Feet along the path: rise over the first 40%, cross until 70%, then land.
static b3Pos mantle_point(const SwatMantle* m,float t) {
    const float split[4]={0,.40f,.70f,1};int seg=t<split[1]?0:t<split[2]?1:2;
    float u=(t-split[seg])/(split[seg+1]-split[seg]);u=u*u*(3-2*u);
    b3Vec3 d=b3SubPos(m->path[seg+1],m->path[seg]);return b3OffsetPos(m->path[seg],swat_mul(d,u));
}
bool swat_controller_mantling(const SwatController* c) { return c->mantle.active; }

void swat_controller_pre_step(SwatController* c, const SwatInput* in, bool weapon_busy) {
    c->yaw = swat_angle(c->yaw + swat_clamp(in->yaw_delta, -0.4f, 0.4f));
    c->pitch = swat_clamp(c->pitch + swat_clamp(in->pitch_delta,-0.3f,0.3f),
                         -85.0f*SWAT_RAD,85.0f*SWAT_RAD);
    c->recoil_pitch *= expf(-12.0f*SWAT_DT);
    c->recoil_yaw *= expf(-12.0f*SWAT_DT);

    // A stand request checks the feet hull and the actual leaned capsule.
    // The actual upper capsule is then swept to its desired lean after physics.
    if(c->mantle.active) {
        // Scripted, pre-validated move: no locomotion, stance change or jump.
        SwatMantle* m=&c->mantle;m->tick++;
        float t=swat_clamp((float)m->tick/(float)(m->total>0?m->total:1),0,1);
        b3Pos feet=mantle_point(m,t),now=b3Body_GetPosition(c->body.body);
        b3Pos centre=b3OffsetPos(feet,m->offset);
        b3Body_SetTransform(c->body.body,centre,b3Body_GetRotation(c->body.body));
        b3Body_SetLinearVelocity(c->body.body,swat_mul(b3SubPos(centre,now),1/SWAT_DT));
        c->sprinting=false;c->body.sprint=false;c->last_jump=in->jump;
        c->ready_blend+=(-1-c->ready_blend)*(1-expf(-12*SWAT_DT));c->ads*=expf(-18*SWAT_DT);
        if(m->tick>=m->total) {
            m->active=false; // the next body pre-step sets its own gravity scale
            b3Body_SetLinearVelocity(c->body.body,swat_v(0,0,0));
        }
        return;
    }
    swat_body_set_crouch(&c->body, in->crouch);
    c->sprinting = in->gait == SWAT_SPRINT && in->forward > 0.1f &&
        !c->body.crouched && !in->aim && !in->fire && !in->reload &&
        !weapon_busy && fabsf(in->lean) < 0.01f && c->stamina > 0.05f;
    c->stamina = swat_clamp(c->stamina + (c->sprinting ? -0.16f : 0.10f)*SWAT_DT,0,1);
    c->ready=(SwatReady)(in->ready>=0 && in->ready<SWAT_READY_STATES ? in->ready : SWAT_READY);
    if(in->aim || in->fire) c->ready=SWAT_READY;
    float ready_target=c->sprinting ? -1 : (c->ready==SWAT_HIGH_READY ? 1 : (c->ready==SWAT_LOW_READY ? -1 : 0));
    c->ready_blend+=(ready_target-c->ready_blend)*(1-expf(-12*SWAT_DT));
    float target_ads = in->aim && c->ready==SWAT_READY && !weapon_busy && !in->reload ? 1.0f : 0.0f;
    c->ads += (target_ads - c->ads) * (1.0f-expf(-18.0f*SWAT_DT));
    c->body.sprint = c->sprinting;
    c->body.walkSpeed = in->gait == SWAT_SLOW ? 1.2f : (in->aim ? 1.7f : 2.8f);
    c->body.walkSpeed *= c->mobility;
    c->body.runSpeed = 4.6f*c->mobility;
    c->body.crouchSpeed = 1.25f*c->mobility;
    // Jump into a waist-high obstacle while moving toward it: clamber.
    if(in->jump && !c->last_jump && c->body.onGround && in->forward>.3f && c->stamina>=.15f && !weapon_busy) {
        b3CosSin h=b3ComputeCosSin(c->yaw);b3Vec3 f=swat_v(h.cosine,0,h.sine);
        bool was=c->body.crouched;swat_body_set_crouch(&c->body,true);
        if(mantle_plan(c,f)) {
            c->mantle.offset=b3SubPos(b3Body_GetPosition(c->body.body),swat_body_feet_position(&c->body));
            b3Body_SetGravityScale(c->body.body,0);
            c->stamina=fmaxf(0,c->stamina-.15f);c->last_jump=in->jump;c->jumped=false;
            return;
        }
        if(!was) swat_body_set_crouch(&c->body,in->crouch);
    }
    c->jumped = in->jump && !c->last_jump && c->body.onGround &&
        c->body.jumpCooldown <= 0.0f && !c->body.crouched && c->stamina >= 0.1f;
    if (c->jumped) {
        swat_body_jump(&c->body);
        c->stamina = fmaxf(0.0f,c->stamina-0.1f);
    }
    c->last_jump = in->jump;
    // Physical translation must use the same heading on each platform.
    b3CosSin heading=b3ComputeCosSin(c->yaw);
    b3Vec3 forward=swat_v(heading.cosine,0,heading.sine);
    b3Vec2 throttle = {swat_clamp(in->forward,-1,1),swat_clamp(in->strafe,-1,1)};
    swat_body_pre_step(&c->body,SWAT_DT,forward,swat_controller_right(c),throttle);
}

void swat_controller_post_step(SwatController* c, const SwatInput* in) {
    // Mid-clamber the body follows its path; ground snapping would pull it down.
    if(c->mantle.active) { c->eye_height=fminf(c->body.totalHeight-.2032f,c->eye_height); return; }
    swat_body_post_step(&c->body,SWAT_DT);
    b3Vec3 right = swat_controller_right(c);
    b3Vec3 goal = swat_mul(right, c->sprinting ? 0.0f : swat_clamp(in->lean,-1,1)*SWAT_MAX_LEAN);
    b3Vec3 difference = swat_add(goal,swat_mul(c->body.upperOffset,-1));
    float length = b3Length(difference);
    float max_step = 2.5f * SWAT_DT;
    if (length > max_step) difference = swat_mul(difference,max_step/length);
    b3Vec3 desired = swat_add(c->body.upperOffset,difference);
    b3Vec3 achieved = swat_body_lean(&c->body,desired);
    c->lean = swat_clamp(b3Dot(achieved,right)/SWAT_MAX_LEAN,-1,1);
    float eye_height = c->body.totalHeight - 0.2032f;
    // Lower immediately for clearance; ease upwards inside the standing hull.
    c->eye_height = fminf(eye_height,c->eye_height + 3.0f*SWAT_DT);
}

b3Pos swat_controller_eye(const SwatController* c) {
    b3Vec3 offset = c->body.upperOffset;
    offset.y = c->eye_height;
    return b3OffsetPos(swat_body_feet_position(&c->body),offset);
}

b3Vec3 swat_controller_aim(const SwatController* c) {
    return swat_direction(c->yaw+c->recoil_yaw,
        swat_clamp(c->pitch+c->recoil_pitch,-89.0f*SWAT_RAD,89.0f*SWAT_RAD));
}

void swat_controller_recoil(SwatController* c, float vertical, float horizontal) {
    c->recoil_pitch = swat_clamp(c->recoil_pitch+vertical,0,12.0f*SWAT_RAD);
    c->recoil_yaw = swat_clamp(c->recoil_yaw+horizontal,-6.0f*SWAT_RAD,6.0f*SWAT_RAD);
}

void swat_controller_view(const SwatController* c, b3Vec3* forward, b3Vec3* right, b3Vec3* up) {
    *forward = swat_controller_aim(c);
    b3CosSin heading=swat_sincos(c->yaw+c->recoil_yaw);
    b3Vec3 r = swat_v(-heading.sine,0,heading.cosine);
    b3Vec3 u = b3Cross(r,*forward);
    float angle = -c->lean*8.0f*SWAT_RAD;
    b3CosSin roll=swat_sincos(angle);
    *right = swat_add(swat_mul(r,roll.cosine),swat_mul(u,-roll.sine));
    *up = swat_add(swat_mul(u,roll.cosine),swat_mul(r,roll.sine));
}
