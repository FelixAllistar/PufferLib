#ifndef SWAT_WORLD_H
#define SWAT_WORLD_H
#include "swat_math.h"
#include "materials.h"
#include <stdbool.h>
#include "fracture_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_MAX_OBJECTS 2048
#define SWAT_MOTEL_MESH_CAPACITY 63
#define SWAT_MAX_SUPPORTS 5
#define SWAT_FENCE_PART_CAPACITY 64
#define SWAT_SURROUNDINGS_PARTS 15
#define SWAT_GROUND_PARTS 54
#define SWAT_MAX_ROOMS 8
#define SWAT_RUBBLE_CAPACITY 64
#define SWAT_RUBBLE_SECTIONS 16
#define SWAT_MAX_GEOMETRY (SWAT_MAX_OBJECTS+SWAT_RUBBLE_CAPACITY)
typedef enum SwatHitKind { SWAT_HIT_NONE, SWAT_HIT_WORLD, SWAT_HIT_ACTOR, SWAT_HIT_PROJECTILE,SWAT_HIT_DEVICE,SWAT_HIT_LIGHT,SWAT_HIT_RUBBLE } SwatHitKind;
typedef enum SwatSupportRole {SWAT_SUPPORT_NONE,SWAT_SUPPORT_MEMBER,SWAT_SUPPORT_ANCHOR} SwatSupportRole;
typedef struct SwatRubble SwatRubble;
typedef struct SwatRubblePool SwatRubblePool;
typedef struct SwatWallSupport SwatWallSupport;
typedef struct SwatSupportPlan SwatSupportPlan;
typedef enum SwatPart { SWAT_PART_SOLID, SWAT_PART_SKIN, SWAT_PART_FRAME, SWAT_PART_SUPPORT, SWAT_PART_LIGHT,
    SWAT_PART_FENCE_WIRE,SWAT_PART_FENCE_RAIL,SWAT_PART_FENCE_POST,SWAT_PART_FIXTURE,
    SWAT_PART_OBSTACLE } SwatPart; // Permanent solid prop; no walking floor or structural attachment.
typedef struct SwatRoom {
    b3Pos center;
    b3Vec3 half;
    SwatMaterial surface, floor;
} SwatRoom;
typedef struct SwatRoomLight {
    b3Pos origin;
    b3Vec3 direction; // Zero for an omnidirectional source.
    float power;
} SwatRoomLight;
typedef struct SwatTag { SwatHitKind kind; int index; } SwatTag;
// Sparse state exists only after a door loses a hinge. Its original owner/body
// and retained cut geometry remain the same during hanging and free motion.
typedef struct SwatDoorState {
    unsigned int broken_hinges;
    bool latched,awake;
    b3Pos position;
    b3Quat rotation;
    b3Vec3 velocity,angular_velocity;
} SwatDoorState;
typedef struct SwatDoorMotion {
    SwatDoorState state;
    b3JointId joint;
    int support;
    b3Vec3 mount;
} SwatDoorMotion;
typedef struct SwatObject {
    SwatTag tag;
    b3BodyId body;
    b3ShapeId shape;
    b3Mesh query_mesh; // Original full mesh for thickness when contacts use bounded parts.
    b3Pos center;
    b3Vec3 half;
    float yaw, health, max_health, door_angle, closed_yaw,pitch;
    SwatMaterial material;
    SwatPart part;
    bool active, door, door_open,locked;
    bool latch_broken; // Retained leaf with a local, physically open edge notch.
    int breach_owner,breach_ticks;
    int wedge_owner;
    bool peek,trapped;
    unsigned int trap_known;
    b3Pos hinge;
    // Convex board fragments share their exact boundary with collision/render.
    // Corners are counter-clockwise in object-local (Y,Z), relative to center.
    bool fractured;
    float corners[4][2];
    SwatFracture* cuts; // Sparse damage geometry; intact objects allocate none.
    SwatDoorMotion* motion;
    SwatRubble* rubble;
    SwatSupportRole support_role;
    int wall_group;
    // Required attachment supports precede their child; -1 denotes unused slots.
    int supports[SWAT_MAX_SUPPORTS];
    float structural_thickness; // Fixture mounting section, metres; 0 for ordinary solids.
    b3Pos breach_position;
} SwatObject;

typedef struct SwatRubbleState {
    uint32_t serial;
    bool active,awake;
    int source,section_count;
    float health;
    SwatFracturePolygon sections[SWAT_RUBBLE_SECTIONS];
    b3Pos position;
    b3Quat rotation;
    b3Vec3 velocity,angular_velocity;
} SwatRubbleState;
struct SwatRubble {
    SwatObject object;
    SwatRubbleState state;
    b3Pos origin;
    float source_yaw;
};
struct SwatRubblePool {
    uint32_t serial;
    int count;
    SwatRubble pieces[SWAT_RUBBLE_CAPACITY];
};

typedef void (*SwatBuildProgress)(void* context);
typedef struct SwatWorld {
    b3WorldId id;
    b3BodyId door_reference; // Unshaped fixed joint reference, created on demand.
    SwatRubblePool* rubble;
    SwatWallSupport* support;
    bool motel,storefront;
    b3MeshData* motel_meshes[SWAT_MOTEL_MESH_CAPACITY]; // Owned static collision; released after world destruction.
    b3MeshData* storefront_meshes[40];
    b3MeshData* fence_meshes[SWAT_FENCE_PART_CAPACITY];
    b3MeshData* surroundings_meshes[SWAT_SURROUNDINGS_PARTS];
    b3MeshData* ground_meshes[SWAT_GROUND_PARTS];
    b3MeshData** motel_contact_meshes; // Shared bounded parts; owned by this world.
    int motel_contact_mesh_count;
    int motel_contact_first[SWAT_MOTEL_MESH_CAPACITY],motel_contact_parts[SWAT_MOTEL_MESH_CAPACITY];
    SwatObject objects[SWAT_MAX_OBJECTS];
    int count, generation;
    int attachment_first; // Derived lower bound; avoids scanning ordinary walls on support loss.
    SwatRoom rooms[SWAT_MAX_ROOMS];
    int room_count;
    unsigned int room_light_off_mask; // Authoritative switches; support loss is evaluated separately.
    // Temporary map-construction service hook; never serialized or simulated.
    SwatBuildProgress build_progress;
    void* build_context;
} SwatWorld;
static inline void swat_world_build_progress(SwatWorld* world){if(world->build_progress)world->build_progress(world->build_context);}

static inline b3Quat swat_door_rotation(float yaw,float pitch) {
    // Share the linked physics library's deterministic trig contract across
    // bodies, queries, drawing and attachment transitions.
    return swat_rotation(yaw,pitch);
}
static inline b3Pos swat_door_center(const SwatObject* o,float yaw) {
    return b3OffsetPos(o->hinge,b3RotateVector(swat_door_rotation(yaw,0),swat_v(0,0,o->half.z)));
}
static inline b3Quat swat_object_rotation(const SwatObject* o) {
    return o->rubble?o->rubble->state.rotation:o->motion?o->motion->state.rotation:
        o->door?swat_door_rotation(o->yaw,o->pitch):swat_rotation(o->yaw,o->pitch);
}
static inline b3Vec3 swat_object_extent(const SwatObject* o) {
    if(!o->motion && !o->rubble)return o->half;
    b3Quat q=swat_object_rotation(o);
    b3Vec3 x=b3RotateVector(q,swat_v(o->half.x,0,0)),y=b3RotateVector(q,swat_v(0,o->half.y,0)),z=b3RotateVector(q,swat_v(0,0,o->half.z));
    return swat_v(fabsf(x.x)+fabsf(y.x)+fabsf(z.x),fabsf(x.y)+fabsf(y.y)+fabsf(z.y),fabsf(x.z)+fabsf(y.z)+fabsf(z.z));
}
static inline int swat_world_geometry_count(const SwatWorld* w) {return w->count+(w->rubble?w->rubble->count:0);}
static inline const SwatObject* swat_world_geometry(const SwatWorld* w,int index) {
    return index<w->count?&w->objects[index]:&w->rubble->pieces[index-w->count].object;
}

typedef struct SwatHit {
    bool hit;
    SwatHitKind kind;
    int index;
    float fraction, distance;
    b3Pos point;
    b3Vec3 normal;
} SwatHit;
static inline const SwatObject* swat_world_hit_object(const SwatWorld* w,SwatHit hit) {
    if(hit.index<0)return NULL;
    if(hit.kind==SWAT_HIT_WORLD && hit.index<w->count)return &w->objects[hit.index];
    if(hit.kind==SWAT_HIT_RUBBLE && w->rubble && hit.index<w->rubble->count)return &w->rubble->pieces[hit.index].object;
    return NULL;
}
static inline int swat_world_hit_layer(SwatHit hit) {return hit.index+(hit.kind==SWAT_HIT_RUBBLE?SWAT_MAX_OBJECTS:0);}
static inline SwatMaterial swat_world_hit_material(const SwatWorld* w,SwatHit hit,SwatMaterial fallback) {
    const SwatObject* o=swat_world_hit_object(w,hit);return o?o->material:fallback;
}

void swat_world_init(SwatWorld* world);
void swat_world_close(SwatWorld* world);
int swat_world_box(SwatWorld* world, b3Pos center, b3Vec3 half,
                   SwatMaterial material, float health);
void swat_world_build_range(SwatWorld* world, uint32_t* seed, bool randomize);
SwatHit swat_world_ray(const SwatWorld* world, b3Pos origin, b3Vec3 direction,
                       float range, b3BodyId ignore);
// Optical queries transmit clear glass; weapons/tools/movers use physical casts.
SwatHit swat_world_sight_ray(const SwatWorld*,b3Pos,b3Vec3,float,b3BodyId ignore);
bool swat_world_sight_clear(const SwatWorld*,b3Pos from,b3Pos to);
// Ignore only the caller's currently occupied ballistic layers. The caller
// limits this cast to their nearest exit, then restores those objects.
SwatHit swat_world_layer_ray(const SwatWorld*,b3Pos,b3Vec3,float,b3BodyId,const int* layers,int count);
SwatHit swat_world_sphere_cast(const SwatWorld* world, b3Pos origin,
                              b3Vec3 translation, float radius, b3BodyId ignore);
bool swat_world_damage(SwatWorld* world, int object, float damage);
bool swat_world_impact(SwatWorld* world,int object,float damage);
bool swat_world_impact_at(SwatWorld*,int object,b3Pos point,float damage,bool door_round);
bool swat_world_attach(SwatWorld*,int object,const int* supports,int count);
float swat_world_exit_distance(const SwatObject* object, b3Pos entry, b3Vec3 direction);
float swat_material_resistance(SwatMaterial material);
void swat_world_step_doors(SwatWorld* world);
// Read-only query of the next ordinary swing step, using its actual actor sweep.
// Dynamic loose/hanging leaves and wedges keep their separate behavior.
bool swat_world_door_blocked(const SwatWorld* world,int object);
// Optional notice after an unobstructed ordinary sweep reaches closed.
void swat_world_step_doors_notify(SwatWorld*,void (*closed)(void*,int),void* context);
// Synchronize moving leaves after the shared Box3D step, including devices.
void swat_world_sync_doors(SwatWorld* world);
void swat_world_sync_rubble(SwatWorld*);
bool swat_world_hit_impact(SwatWorld*,SwatHit,float damage,bool door_round);
void swat_world_support_enable(SwatWorld*,int group);
void swat_world_support_begin(SwatWorld*);
void swat_world_support_end(SwatWorld*);
void swat_world_support_setup(SwatWorld*);
uint32_t swat_world_support_mask(const SwatWorld*,int group);
SwatSupportPlan* swat_world_support_prepare(const SwatWorld*,const SwatObject* candidate,uint32_t groups);
void swat_world_support_commit(SwatWorld*,SwatSupportPlan*,bool spawn);
void swat_world_support_close(SwatSupportPlan*);
bool swat_world_support_active_valid(const SwatSupportPlan*,const SwatObject*);
void swat_world_support_take(SwatSupportPlan*,SwatFracture* cuts[SWAT_MAX_OBJECTS]);
bool swat_world_rubble_valid(const SwatWorld*,const SwatRubbleState*);
bool swat_world_rubble_separated(const SwatObject*,const SwatFracture*,const SwatRubbleState*);
SwatFracture* swat_world_rubble_prepare(const SwatWorld*,const SwatRubbleState*);
void swat_world_rubble_restore(SwatWorld*,int slot,const SwatRubbleState*,SwatFracture* prepared);
SwatDoorMotion* swat_world_door_prepare(void);
unsigned int swat_world_door_hinges(const SwatObject*);
// Caller prepares sparse storage before an atomic network transaction.
void swat_world_door_restore(SwatWorld*,int object,const SwatDoorState*);
void swat_world_place(SwatObject* object, float yaw);
void swat_world_tilt(SwatObject* object,float pitch);
bool swat_world_fragment(SwatObject* object,const float corners[4][2]);
bool swat_world_fragment_valid(const SwatObject* object,const float corners[4][2]);
b3HullData* swat_world_fragment_hull(const SwatObject* object,const float corners[4][2]);
bool swat_world_breachable(const SwatObject* object);
int swat_world_breach(SwatWorld* world,int object,b3Pos position);
// Mounted charge: local tool damage plus attenuated damage to nearby material.
int swat_world_blast(SwatWorld* world,int object,b3Pos position);
// Rebuilds the retained leaf's shapes from the shared latch-damage geometry.
bool swat_world_break_latch(SwatWorld* world,int object);
// Exact retained convex pieces; prepare before mutating physics/network state.
SwatFracture* swat_world_cut_prepare(const SwatObject*,const SwatCut*,int count);
SwatFracture* swat_world_sections_prepare(const SwatObject*,const SwatCut*,int count,const SwatFracturePolygon*,int sections);
void swat_world_cut_apply(SwatWorld*,int object,SwatFracture* prepared);
void swat_world_cut_close(SwatFracture*);
// Returns 0 unchanged, 1 partially cut, 2 removed, -1 rejected.
int swat_world_cut(SwatWorld*,int object,SwatCut cut);
int swat_world_room(const SwatWorld* world, b3Pos position);
b3SurfaceMaterial swat_physics_material(SwatMaterial material);
bool swat_world_visible(const SwatWorld* world,b3Pos from,b3Pos to);
bool swat_world_object_ray(const SwatObject*,b3Pos origin,b3Vec3 translation);
// Shared renderer/simulation source, including switches and lost supports.
SwatRoomLight swat_world_room_light(const SwatWorld*,int room);
// Bounded actor-detection approximation, not photometric lux or renderer pixels.
float swat_world_visual_range(const SwatWorld*,b3Pos target,float daylight_range);

#ifdef __cplusplus
}
#endif
#endif
