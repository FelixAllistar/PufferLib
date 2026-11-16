// Box3D ragdolls for the locomotion env (loco_env.h): one b3World holding
// many ragdolls of one rig (loco_rig.h) on one flat ground. Ragdolls never
// touch each other or themselves (every body collides with the ground only),
// so the world is many independent animals sharing one solver.
//
// Joints are Box3D spherical joints (cone and twist limits) and revolute
// joints for the knees and ankles whose clip motion is a hinge. Each joint's
// spring is the PD controller: its target rotation is the PD target, its
// hertz and damping ratio the gains. Box3D's spring is implicit and
// mass-normalised (it acts against the pair's effective inertia), which is
// what keeps a stiff 20-body chain stable at a few substeps; it has no torque
// cap (docs/locomotion/README.md, "What could go wrong").
#ifndef CRET_LOCO_SIM_H
#define CRET_LOCO_SIM_H
#include "box3d/box3d.h"
#include "loco_rig.h"

#define LOCO_CAT_GROUND 1u
#define LOCO_CAT_BODY 2u

// Spring gains (Hz, damping ratio) by body class. A Box3D spring's stiffness
// scales with the pair's effective inertia, which is about the lighter
// body's, while a joint near the root carries everything beyond it: the hip
// holds the whole animal on a thigh's inertia, the tail's base the whole tail.
// So the legs need the stiffest springs, and the tail needs more than its
// small segments suggest (at 12 Hz the stand-in's tail tip sags 26 cm; at
// 40 Hz the pinned pose holds within a degree). In the passive-tail
// experiment the tail has no actions: its springs pull it toward its mean
// pose with less damping, as a tail of ligaments and tendons would swing.
typedef struct LocoGains { float leg, body, tail, passive_tail, damping, passive_damping, friction; bool passive; } LocoGains;
static LocoGains loco_default_gains(void) {
    LocoGains g; g.leg = 100.0f; g.body = 60.0f; g.tail = 40.0f; g.passive_tail = 20.0f; g.damping = 1.0f; g.passive_damping = 0.4f; g.friction = 0.9f; g.passive = false;
    return g;
}

typedef struct LocoDoll { b3BodyId body[LOCO_MAX_BODIES]; b3JointId joint[LOCO_MAX_BODIES]; b3Vec3 origin; } LocoDoll;
typedef struct LocoPhys {
    b3WorldId world; b3BodyId ground;
    const LocoRig* rig; LocoGains gains;
    int n; LocoDoll* doll;
} LocoPhys;

static inline bool loco_is_leg(const LocoBody* b) { return strstr(b->name, "thigh") || strstr(b->name, "shin") || strstr(b->name, "meta") || strstr(b->name, "foot"); }
static inline bool loco_is_tail(const LocoBody* b) { return (b->flags & LB_TAIL) != 0; }
static inline float loco_hertz(const LocoPhys* p, const LocoBody* b) {
    return loco_is_tail(b) ? (p->gains.passive ? p->gains.passive_tail : p->gains.tail) : loco_is_leg(b) ? p->gains.leg : p->gains.body;
}

// Creates the world, the ground and n ragdolls at the rig's bind pose, each
// at origin[i]. Single-threaded and without sleeping, so a run is
// deterministic for its inputs. substep_hz is the solver's substep rate: Box3D
// joints are themselves soft constraints (60 Hz by default, at most a quarter
// of the substep rate), and a heavy pelvis on light legs stretches them, so
// they are set as stiff as the substep rate allows.
static void loco_phys_create(LocoPhys* p, const LocoRig* rig, int n, const b3Vec3* origin, LocoGains gains, float substep_hz) {
    memset(p, 0, sizeof *p);
    p->rig = rig; p->gains = gains; p->n = n;
    b3WorldDef wd = b3DefaultWorldDef();
    wd.gravity = lv(0, -9.81f, 0); wd.enableSleep = false; wd.workerCount = 1;
    p->world = b3CreateWorld(&wd);
    b3BodyDef gd = b3DefaultBodyDef();
    gd.position = lv(0, -1.0f, 0);
    p->ground = b3CreateBody(p->world, &gd);
    b3ShapeDef gs = b3DefaultShapeDef();
    gs.filter.categoryBits = LOCO_CAT_GROUND; gs.filter.maskBits = LOCO_CAT_BODY;
    gs.baseMaterial.friction = gains.friction;
    b3BoxHull ground = b3MakeBoxHull(2000.0f, 1.0f, 2000.0f);
    b3CreateHullShape(p->ground, &gs, &ground.base);
    p->doll = (LocoDoll*)calloc((size_t)n, sizeof(LocoDoll));
    for (int i = 0; i < n; i++) {
        LocoDoll* d = &p->doll[i]; d->origin = origin[i];
        for (int b = 0; b < rig->nbody; b++) {
            const LocoBody* B = &rig->body[b];
            b3BodyDef bd = b3DefaultBodyDef();
            bd.type = b3_dynamicBody; bd.position = b3Add(origin[i], B->bind); bd.enableSleep = false;
            d->body[b] = b3CreateBody(p->world, &bd);
            b3ShapeDef sd = b3DefaultShapeDef();
            sd.density = LOCO_DENSITY; sd.filter.categoryBits = LOCO_CAT_BODY; sd.filter.maskBits = LOCO_CAT_GROUND;
            sd.baseMaterial.friction = gains.friction;
            if (B->shape == LS_BOX) {
                b3Transform tf; tf.p = B->box_c; tf.q = B->box_q;
                b3BoxHull h = b3MakeTransformedBoxHull(B->box_h.x, B->box_h.y, B->box_h.z, tf);
                // Box3D derives mass from the shape's volume; scale the density so the foot weighs its fitted mass.
                sd.density = B->mass / (8.0f * B->box_h.x * B->box_h.y * B->box_h.z);
                b3CreateHullShape(d->body[b], &sd, &h.base);
            } else {
                b3Capsule c; c.center1 = B->c1; c.center2 = B->c2; c.radius = B->radius;
                b3CreateCapsuleShape(d->body[b], &sd, &c);
            }
        }
        for (int b = 1; b < rig->nbody; b++) {
            const LocoBody* B = &rig->body[b];
            float hz = loco_hertz(p, B), zeta = loco_is_tail(B) && gains.passive ? gains.passive_damping : gains.damping;
            if (B->jtype == LJ_HINGE) {
                b3RevoluteJointDef jd = b3DefaultRevoluteJointDef();
                jd.base.bodyIdA = d->body[B->parent]; jd.base.bodyIdB = d->body[b];
                jd.base.localFrameA.p = B->anchor_a; jd.base.localFrameA.q = B->qf_a;
                jd.base.localFrameB.p = lv(0, 0, 0); jd.base.localFrameB.q = B->qf_b; jd.base.constraintHertz = 0.25f * substep_hz;
                jd.enableSpring = true; jd.hertz = hz; jd.dampingRatio = zeta;
                jd.enableLimit = true; jd.lowerAngle = B->lo; jd.upperAngle = B->hi;
                d->joint[b] = b3CreateRevoluteJoint(p->world, &jd);
            } else {
                b3SphericalJointDef jd = b3DefaultSphericalJointDef();
                jd.base.bodyIdA = d->body[B->parent]; jd.base.bodyIdB = d->body[b];
                jd.base.localFrameA.p = B->anchor_a; jd.base.localFrameA.q = B->qf_a;
                jd.base.localFrameB.p = lv(0, 0, 0); jd.base.localFrameB.q = B->qf_b; jd.base.constraintHertz = 0.25f * substep_hz;
                jd.enableSpring = true; jd.hertz = hz; jd.dampingRatio = zeta; jd.targetRotation = b3Quat_identity;
                jd.enableConeLimit = true; jd.coneAngle = B->cone;
                jd.enableTwistLimit = true; jd.lowerTwistAngle = B->twist_lo; jd.upperTwistAngle = B->twist_hi;
                d->joint[b] = b3CreateSphericalJoint(p->world, &jd);
            }
        }
    }
}
static void loco_phys_destroy(LocoPhys* p) {
    if (b3World_IsValid(p->world)) b3DestroyWorld(p->world);
    free(p->doll); memset(p, 0, sizeof *p);
}

// Places doll i at a pose in model space (positions and rotations of the
// body origins, velocities of the origins) offset by the doll's origin.
// Box3D's linear velocity is the centre of mass's, so the origin velocity is
// carried over to it.
static void loco_doll_set(LocoPhys* p, int i, const b3Vec3* pos, const b3Quat* rot, const b3Vec3* vel, const b3Vec3* angvel) {
    LocoDoll* d = &p->doll[i];
    for (int b = 0; b < p->rig->nbody; b++) {
        b3Body_SetTransform(d->body[b], b3Add(d->origin, pos[b]), rot[b]);
        b3Vec3 w = angvel ? angvel[b] : lv(0, 0, 0), v = vel ? vel[b] : lv(0, 0, 0);
        b3Vec3 r = lq_rot(rot[b], b3Body_GetLocalCenter(d->body[b]));
        b3Body_SetLinearVelocity(d->body[b], b3Add(v, b3Cross(w, r)));
        b3Body_SetAngularVelocity(d->body[b], w);
    }
}
// Doll i's body origins and rotations (model space: the doll's origin removed),
// centre-of-mass velocities and angular velocities. Any output may be NULL.
static void loco_doll_get(const LocoPhys* p, int i, b3Vec3* pos, b3Quat* rot, b3Vec3* vel, b3Vec3* angvel) {
    const LocoDoll* d = &p->doll[i];
    for (int b = 0; b < p->rig->nbody; b++) {
        if (pos || rot) {
            b3WorldTransform t = b3Body_GetTransform(d->body[b]);
            if (pos) pos[b] = b3Sub(t.p, d->origin);
            if (rot) rot[b] = t.q;
        }
        if (vel) vel[b] = b3Body_GetLinearVelocity(d->body[b]);
        if (angvel) angvel[b] = b3Body_GetAngularVelocity(d->body[b]);
    }
}
// PD targets: dev[b] is the target deviation of body b's joint (loco_joint_dev
// form); a hinge takes its twist about the hinge axis.
static void loco_doll_targets(LocoPhys* p, int i, const b3Quat* dev) {
    const LocoDoll* d = &p->doll[i];
    for (int b = 1; b < p->rig->nbody; b++) {
        if (p->rig->body[b].jtype == LJ_HINGE) b3RevoluteJoint_SetTargetAngle(d->joint[b], lq_twist_z(dev[b]));
        else b3SphericalJoint_SetTargetRotation(d->joint[b], dev[b]);
    }
}
// True if a body of doll i other than the feet touches the ground.
static bool loco_doll_fallen(const LocoPhys* p, int i) {
    const LocoDoll* d = &p->doll[i]; b3ContactData cd[8];
    for (int b = 0; b < p->rig->nbody; b++) {
        if (p->rig->body[b].flags & LB_CONTACT) continue;
        int n = b3Body_GetContactData(d->body[b], cd, 8);
        for (int k = 0; k < n; k++) for (int m = 0; m < cd[k].manifoldCount; m++) if (cd[k].manifolds[m].pointCount > 0) return true;
    }
    return false;
}
// Largest joint separation of doll i (m): how far the solver lets the chain come apart.
static float loco_doll_separation(const LocoPhys* p, int i) {
    float s = 0;
    for (int b = 1; b < p->rig->nbody; b++) s = fmaxf(s, b3Joint_GetLinearSeparation(p->doll[i].joint[b]));
    return s;
}
// Pins (kinematic, at rest) or frees doll i's pelvis.
static void loco_doll_pin(LocoPhys* p, int i, bool pin) {
    b3BodyId pel = p->doll[i].body[0];
    b3Body_SetType(pel, pin ? b3_kinematicBody : b3_dynamicBody);
    if (pin) { b3Body_SetLinearVelocity(pel, lv(0, 0, 0)); b3Body_SetAngularVelocity(pel, lv(0, 0, 0)); }
}
#endif
