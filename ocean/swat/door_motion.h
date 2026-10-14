// World-owned door attachment failure. Included by world.c only.
#ifndef SWAT_DOOR_MOTION_H
#define SWAT_DOOR_MOTION_H
static void door_motion_stop(SwatObject* o) {
    if(!o->motion)return;
    if(B3_IS_NON_NULL(o->motion->joint))b3DestroyJoint(o->motion->joint,true);
    o->motion->joint=b3_nullJointId;o->motion->state.awake=false;
    o->motion->state.velocity=o->motion->state.angular_velocity=swat_v(0,0,0);
}
SwatDoorMotion* swat_world_door_prepare(void) {
    SwatDoorMotion* m=calloc(1,sizeof(*m));if(m)m->support=-1;return m;
}
static void door_motion_begin(SwatObject* o,SwatDoorMotion* motion) {
    motion->state=(SwatDoorState){.latched=!o->latch_broken && !o->door_open && o->door_angle<.01f,
        .position=o->center,.rotation=swat_object_rotation(o),.awake=true};
    o->motion=motion;
}
static void door_motion_support(SwatWorld* w,SwatObject* o) {
    SwatDoorMotion* m=o->motion;int support=swat_door_support(m->state.broken_hinges,m->state.latched);
    if(support==m->support)return;
    if(B3_IS_NON_NULL(m->joint))b3DestroyJoint(m->joint,true);
    m->joint=b3_nullJointId;m->support=support;
    w->generation++;
    if(!o->active)return;
    b3Body_SetType(o->body,support==SWAT_DOOR_FIXED?b3_staticBody:b3_dynamicBody);
    if(support==SWAT_DOOR_FIXED)return;
    b3Body_SetLinearDamping(o->body,.15f);b3Body_SetAngularDamping(o->body,.6f);
    o->wedge_owner=-1;
    b3Body_EnableContactRecycling(o->body,false);b3Body_SetBullet(o->body,true);
    if(B3_IS_NULL(w->door_reference)) {
        b3BodyDef def=b3DefaultBodyDef();w->door_reference=b3CreateBody(w->id,&def);
    }
    if(support==SWAT_DOOR_FREE){o->locked=false;o->peek=false;o->wedge_owner=-1;return;}
    b3Vec3 local=support==SWAT_DOOR_LATCH?swat_door_latch(o):swat_door_hinge(o,support-SWAT_DOOR_BOTTOM);
    b3Quat closed=swat_rotation(o->closed_yaw,0);
    b3Pos anchor=b3OffsetPos(o->hinge,b3RotateVector(closed,b3Add(local,swat_v(0,0,o->half.z))));
    b3SphericalJointDef joint=b3DefaultSphericalJointDef();
    joint.base.bodyIdA=w->door_reference;joint.base.bodyIdB=o->body;
    joint.base.localFrameA.p=swat_v(anchor.x,anchor.y,anchor.z);joint.base.localFrameB.p=local;
    m->joint=b3CreateSphericalJoint(w->id,&joint);
}
unsigned int swat_world_door_hinges(const SwatObject* o) {
    if(!o->cuts)return 0;
    unsigned int broken=0;
    for(int h=0;h<SWAT_DOOR_HINGES;h++) {
        b3Vec3 p=swat_door_hinge(o,h);int absent=0;
        for(int s=0;s<4;s++) {
            float y=p.y+(s&1?1:-1)*.035f*(o->half.y/1.095f);
            float z=p.z+(s&2?1:-1)*.008f*(o->half.z/.54f);
            absent+=!swat_fracture_contains(o,y,z);
        }
        if(absent>=3)broken|=1u<<h;
    }
    return broken;
}
static void door_motion_refresh(SwatWorld* w,SwatObject* o,bool geometry) {
    if(!o->door || !o->active)return;
    unsigned int broken=geometry?swat_world_door_hinges(o):0;
    if(!broken && !o->motion)return;
    if(!o->motion) {
        SwatDoorMotion* motion=swat_world_door_prepare();if(!motion)return;
        door_motion_begin(o,motion);
    }
    SwatDoorMotion* m=o->motion;m->state.broken_hinges|=broken;
    if(o->latch_broken || o->door_open)m->state.latched=false;
    int old=m->support;door_motion_support(w,o);
    if(m->support==SWAT_DOOR_FREE && old!=SWAT_DOOR_FREE &&
       b3LengthSquared(m->state.angular_velocity)<.01f) {
        // Small authored release impulse ensures an unsupported upright leaf
        // tips instead of balancing indefinitely on its thin bottom edge.
        m->state.angular_velocity=b3RotateVector(m->state.rotation,swat_v(0,0,-.8f));
        b3Body_SetAngularVelocity(o->body,m->state.angular_velocity);
    }
    m->state.awake=b3Body_IsAwake(o->body);
}
static void door_motion_drive(SwatObject* o) {
    SwatDoorMotion* m=o->motion;
    if(m->support==SWAT_DOOR_FREE || m->support==SWAT_DOOR_LATCH)return;
    float target=o->door_open?(o->peek?12*SWAT_RAD:SWAT_PI*.5f):0;
    if(o->wedge_owner>=0)target=0;
    float error=swat_angle(o->closed_yaw+target-o->yaw);
    float speed=b3Body_GetAngularVelocity(o->body).y;
    if(fabsf(error)<.025f && fabsf(speed)<.03f)return;
    float inertia=b3Body_GetMass(o->body)*o->half.z*o->half.z;
    float torque=inertia*(swat_clamp(error*4,-2,2)-speed)*5;
    float limit=fmaxf(20,inertia*8);
    b3Body_ApplyTorque(o->body,swat_v(0,swat_clamp(torque,-limit,limit),0),true);
}
static void door_motion_impulse(SwatObject* o,b3Pos point,float impulse) {
    if(!o->active || !swat_door_dynamic(o))return;
    b3Quat q=swat_object_rotation(o);b3Vec3 local=b3InvRotateVector(q,b3SubPos(point,o->center));
    b3Vec3 normal=b3RotateVector(q,swat_v(local.x>0?-1:1,0,0));
    b3Body_ApplyLinearImpulse(o->body,swat_mul(normal,impulse),point,true);
    o->motion->state.velocity=b3Body_GetLinearVelocity(o->body);
    o->motion->state.angular_velocity=b3Body_GetAngularVelocity(o->body);
    o->motion->state.awake=b3Body_IsAwake(o->body);
}
void swat_world_door_restore(SwatWorld* w,int object,const SwatDoorState* state) {
    SwatObject* o=&w->objects[object];SwatDoorMotion* m=o->motion;
    assert(m && state->broken_hinges);m->state=*state;
    if(!o->active){m->joint=b3_nullJointId;m->support=swat_door_support(state->broken_hinges,state->latched);return;}
    o->center=state->position;
    b3Vec3 normal=b3RotateVector(state->rotation,swat_v(1,0,0));
    if(normal.x*normal.x+normal.z*normal.z>1e-8f)o->yaw=atan2f(-normal.z,normal.x);
    b3Body_SetTransform(o->body,state->position,state->rotation);
    door_motion_support(w,o);
    if(m->support!=SWAT_DOOR_FIXED) {
        b3Body_SetLinearVelocity(o->body,state->velocity);b3Body_SetAngularVelocity(o->body,state->angular_velocity);
        b3Body_SetAwake(o->body,state->awake);
    }
}
void swat_world_sync_doors(SwatWorld* w) {
    if(B3_IS_NULL(w->door_reference))return;
    for(int i=0;i<w->count;i++) {
        SwatObject* o=&w->objects[i];SwatDoorMotion* m=o->motion;
        if(!o->active || !m || m->support==SWAT_DOOR_FIXED)continue;
        b3WorldTransform pose=b3Body_GetTransform(o->body);
        o->center=m->state.position=pose.p;m->state.rotation=pose.q;
        m->state.velocity=b3Body_GetLinearVelocity(o->body);m->state.angular_velocity=b3Body_GetAngularVelocity(o->body);
        m->state.awake=b3Body_IsAwake(o->body);
        b3Vec3 normal=b3RotateVector(pose.q,swat_v(1,0,0));
        if(normal.x*normal.x+normal.z*normal.z>1e-8f)o->yaw=atan2f(-normal.z,normal.x);
        o->door_angle=swat_clamp(swat_angle(o->yaw-o->closed_yaw),0,SWAT_PI*.5f);
        if(o->breach_owner>=0)o->breach_position=b3OffsetPos(o->center,b3RotateVector(pose.q,m->mount));
        if(o->center.y< -8 || b3Distance(o->center,o->hinge)>60)swat_world_damage(w,i,o->max_health);
    }
}
#endif
