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
        .position=o->center,.rotation=swat_object_rotation(o),.awake=true,
        .hinged=o->latch_broken,.intent=o->latch_broken && o->door_open};
    o->motion=motion;
}
static int door_hinge_group(const SwatWorld* w,const SwatObject* o) {return -(int)(1+(o-w->objects));}
static void door_shapes_group(b3BodyId body,int group) {
    b3ShapeId shapes[64];int count=b3Body_GetShapes(body,shapes,64);
    for(int i=0;i<count;i++){b3Filter f=b3Shape_GetFilter(shapes[i]);if(f.groupIndex!=group){f.groupIndex=group;b3Shape_SetFilter(shapes[i],f,false);}}
}
// Static objects touching the closed leaf, 6 cm around: jambs, stop, header,
// adjoining wall pieces. Found from geometry, so replicas agree.
static void door_find_frame(SwatWorld* w,SwatObject* o) {
    SwatDoorMotion* m=o->motion;m->frame_count=0;
    b3Quat closed=swat_door_rotation(o->closed_yaw,0);b3Pos center=swat_door_center(o,o->closed_yaw);
    b3Vec3 grow=swat_v(o->half.x+.06f,o->half.y+.06f,o->half.z+.06f);
    for(int i=0;i<w->count && m->frame_count<64;i++) {
        SwatObject* p=&w->objects[i];
        if(p==o || !p->active || B3_IS_NULL(p->body) || b3Body_GetType(p->body)!=b3_staticBody)continue;
        b3Vec3 local=b3InvRotateVector(closed,b3SubPos(p->center,center));
        float r=b3Length(p->half);
        if(fabsf(local.x)>grow.x+r || fabsf(local.y)>grow.y+r || fabsf(local.z)>grow.z+r)continue;
        // Exact test in the leaf frame against the neighbour's rotated box.
        b3Quat q=swat_object_rotation(p);bool near=true;
        b3Vec3 axes[3]={b3InvRotateVector(closed,b3RotateVector(q,swat_v(1,0,0))),b3InvRotateVector(closed,b3RotateVector(q,swat_v(0,1,0))),b3InvRotateVector(closed,b3RotateVector(q,swat_v(0,0,1)))};
        const float g[3]={grow.x,grow.y,grow.z},l[3]={local.x,local.y,local.z};
        for(int a=0;a<3 && near;a++) {
            float extent=fabsf((a==0?axes[0].x:a==1?axes[0].y:axes[0].z))*p->half.x+fabsf((a==0?axes[1].x:a==1?axes[1].y:axes[1].z))*p->half.y+
                fabsf((a==0?axes[2].x:a==1?axes[2].y:axes[2].z))*p->half.z;
            near=fabsf(l[a])<=g[a]+extent;
        }
        if(near)m->frame[m->frame_count++]=i;
    }
}
static void door_frame_filter(SwatWorld* w,SwatObject* o,bool on) {
    SwatDoorMotion* m=o->motion;int group=on?door_hinge_group(w,o):0;
    if(o->active && B3_IS_NON_NULL(o->body))door_shapes_group(o->body,group);
    for(int i=0;i<m->frame_count;i++){SwatObject* p=&w->objects[m->frame[i]];if(p->active && B3_IS_NON_NULL(p->body))door_shapes_group(p->body,group);}
}
static void door_motion_support(SwatWorld* w,SwatObject* o) {
    SwatDoorMotion* m=o->motion;int support=swat_door_mode(&m->state);
    if(support==m->support)return;
    if(B3_IS_NON_NULL(m->joint))b3DestroyJoint(m->joint,true);
    if(m->support==SWAT_DOOR_HINGED){door_frame_filter(w,o,false);if(o->active && B3_IS_NON_NULL(o->body))b3Body_SetGravityScale(o->body,1);}
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
    if(support==SWAT_DOOR_HINGED) {
        // Revolute hinge along the leaf's hinge edge (frame z = world up),
        // limited between the frame stop (closed) and the wall (90 degrees).
        // The motor at zero speed with a small torque cap is hinge friction;
        // pushes raise the cap briefly. Bodies, rounds and blasts move it.
        b3Body_SetAngularDamping(o->body,1.5f);
        // A vertical hinge gets no swing torque from gravity, only sag that
        // the solver turns into slow creep. The leaf hangs on its hinges.
        b3Body_SetGravityScale(o->body,0);
        b3Quat up=b3MakeQuatFromAxisAngle(swat_v(1,0,0),-SWAT_PI*.5f);
        b3Quat closed=swat_door_rotation(o->closed_yaw,0);
        b3RevoluteJointDef joint=b3DefaultRevoluteJointDef();
        joint.base.bodyIdA=w->door_reference;joint.base.bodyIdB=o->body;
        joint.base.localFrameA.p=swat_v(o->hinge.x,o->hinge.y,o->hinge.z);joint.base.localFrameA.q=b3MulQuat(closed,up);
        joint.base.localFrameB.p=swat_v(0,0,-o->half.z);joint.base.localFrameB.q=up;
        // The stops are enforced exactly by the per-tick projection (clamped
        // to [0, 90] degrees). Limits sit just outside: a soft limit right at
        // the rest angle keeps nudging a leaf resting against it.
        joint.enableLimit=true;joint.lowerAngle=-.02f;joint.upperAngle=SWAT_PI*.5f+.02f;
        joint.enableMotor=true;joint.motorSpeed=0;
        joint.maxMotorTorque=SWAT_DOOR_FRICTION*b3Body_GetMass(o->body)*4*o->half.z*o->half.z/3;
        m->joint=b3CreateRevoluteJoint(w->id,&joint);
        o->peek=false;door_find_frame(w,o);door_frame_filter(w,o,true);
        return;
    }
    b3Vec3 local=support==SWAT_DOOR_LATCH?swat_door_latch(o):swat_door_hinge_pivot(o,support-SWAT_DOOR_BOTTOM);
    b3Quat closed=swat_door_rotation(o->closed_yaw,0);
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
    if(geometry && w->motel && o==&w->objects[16])broken|=swat_motel_hinge_frame_broken(w,w->objects);
    if(!broken && !o->motion && !o->latch_broken)return;
    if(!o->motion) {
        SwatDoorMotion* motion=swat_world_door_prepare();if(!motion)return;
        door_motion_begin(o,motion);
    }
    SwatDoorMotion* m=o->motion;m->state.broken_hinges|=broken;
    if(o->latch_broken || o->door_open)m->state.latched=false;
    if(o->latch_broken && !m->state.hinged){m->state.hinged=true;m->state.intent=o->door_open;m->state.push_ticks=0;}
    int old=m->support;door_motion_support(w,o);
    // Bullet cuts rebuild shapes with default filters; keep the frame group.
    if(m->support==SWAT_DOOR_HINGED)door_frame_filter(w,o,true);
    if(m->support==SWAT_DOOR_FREE && old!=SWAT_DOOR_FREE &&
       b3LengthSquared(m->state.angular_velocity)<.01f) {
        // Small authored release impulse ensures an unsupported upright leaf
        // tips instead of balancing indefinitely on its thin bottom edge.
        m->state.angular_velocity=b3RotateVector(m->state.rotation,swat_v(0,0,-.8f));
        b3Body_SetAngularVelocity(o->body,m->state.angular_velocity);
    }
    m->state.awake=b3Body_IsAwake(o->body);
}
static void door_frame_refresh(SwatWorld* w) {
    // Only Room 101 currently has an authored fixed-side mounting contract.
    // Run on geometry changes, not on every simulation tick or door movement.
    if(!w->motel || w->count<=16 || !w->objects[16].active)return;
    SwatObject* o=&w->objects[16];
    unsigned int previous=o->motion?o->motion->state.broken_hinges:0;
    if(swat_motel_hinge_frame_broken(w,w->objects)&~previous)door_motion_refresh(w,o,true);
}
static void door_motion_drive(SwatObject* o) {
    SwatDoorMotion* m=o->motion;
    if(m->support==SWAT_DOOR_FREE || m->support==SWAT_DOOR_LATCH)return;
    if(m->support==SWAT_DOOR_HINGED) {
        // A use/AI/ram command (door_open toggled) pushes toward its target
        // for at most a second, like a hand on the leaf; afterwards the leaf
        // only has hinge friction and stays wherever it was left or pushed.
        if(o->door_open!=m->state.intent){m->state.intent=o->door_open;m->state.push_ticks=SWAT_DOOR_PUSH_TICKS;}
        // Torques scale with the leaf's own inertia about its hinge edge, so
        // a light interior door and a heavy fixture door behave alike.
        float inertia=b3Body_GetMass(o->body)*4*o->half.z*o->half.z/3;
        float speed=0,torque=inertia*SWAT_DOOR_FRICTION;
        if(o->wedge_owner>=0){speed=-2;torque=inertia*40;}
        else if(m->state.push_ticks) {
            float angle=b3RevoluteJoint_GetAngle(m->joint),target=o->door_open?SWAT_PI*.5f:0;
            float error=target-angle;m->state.push_ticks--;
            if(fabsf(error)<.02f)m->state.push_ticks=0;
            else {
                // Proportional, with a floor so the leaf reaches the frame
                // stop or the wall (both joint limits) instead of creeping.
                speed=copysignf(swat_clamp(fabsf(error)*6,.8f,3),error);
                torque=inertia*SWAT_DOOR_PUSH;b3Body_SetAwake(o->body,true);
            }
        }
        b3RevoluteJoint_SetMotorSpeed(m->joint,speed);b3RevoluteJoint_SetMaxMotorTorque(m->joint,torque);
        return;
    }
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
    assert(m && (state->broken_hinges || state->hinged));m->state=*state;
    if(!o->active){m->joint=b3_nullJointId;m->support=swat_door_mode(state);return;}
    o->center=state->position;
    b3Vec3 normal=b3RotateVector(state->rotation,swat_v(1,0,0));
    if(normal.x*normal.x+normal.z*normal.z>1e-8f)o->yaw=b3Atan2(-normal.z,normal.x);
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
        if(m->support==SWAT_DOOR_HINGED) {
            // Keep only the hinge's degree of freedom: the solver's sag and
            // wobble are dropped by projecting back onto the exact hinge pose
            // and swing rate. Same pose math as the scripted sweep, so queries,
            // drawing, replicas and attached charges agree exactly.
            // The joint's own angle is rotation about the hinge axis only; a
            // face normal would fold gravity's small tilt into the swing.
            float angle=swat_clamp(b3RevoluteJoint_GetAngle(m->joint),0,SWAT_PI*.5f);
            float spin=b3Body_GetAngularVelocity(o->body).y;
            // At rest: keep the exact previous angle so float noise in the
            // solver cannot accumulate into a slow creep.
            if(fabsf(spin)<1e-3f && fabsf(angle-o->door_angle)<1e-4f){angle=o->door_angle;spin=0;}
            if((angle<=0 && spin<0) || (angle>=SWAT_PI*.5f && spin>0))spin=0;
            o->door_angle=angle;o->yaw=o->closed_yaw+angle;o->center=swat_door_center(o,o->yaw);
            b3Quat rotation=swat_door_rotation(o->yaw,0);
            b3Vec3 omega=swat_v(0,spin,0);
            b3Body_SetTransform(o->body,o->center,rotation);
            // Box3D's linear velocity is the centre of mass's; a cut leaf's
            // mass centre is off its origin, and the origin's rate here
            // would inject a spurious hinge torque.
            b3Vec3 velocity=b3Cross(omega,b3SubPos(b3Body_GetWorldCenter(o->body),o->hinge));
            b3Body_SetLinearVelocity(o->body,velocity);b3Body_SetAngularVelocity(o->body,omega);
            m->state.position=o->center;m->state.rotation=rotation;m->state.velocity=velocity;m->state.angular_velocity=omega;
            m->state.awake=b3Body_IsAwake(o->body);
            if(o->breach_owner>=0)o->breach_position=b3OffsetPos(o->center,b3RotateVector(rotation,m->mount));
            continue;
        }
        b3WorldTransform pose=b3Body_GetTransform(o->body);
        o->center=m->state.position=pose.p;m->state.rotation=pose.q;
        m->state.velocity=b3Body_GetLinearVelocity(o->body);m->state.angular_velocity=b3Body_GetAngularVelocity(o->body);
        m->state.awake=b3Body_IsAwake(o->body);
        b3Vec3 normal=b3RotateVector(pose.q,swat_v(1,0,0));
        if(normal.x*normal.x+normal.z*normal.z>1e-8f)o->yaw=b3Atan2(-normal.z,normal.x);
        o->door_angle=swat_clamp(swat_angle(o->yaw-o->closed_yaw),0,SWAT_PI*.5f);
        if(o->breach_owner>=0)o->breach_position=b3OffsetPos(o->center,b3RotateVector(pose.q,m->mount));
        if(o->center.y< -8 || b3Distance(o->center,o->hinge)>60)swat_world_damage(w,i,o->max_health);
    }
}
#endif
