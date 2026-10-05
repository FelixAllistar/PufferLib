#ifndef SWAT_WORLD_H
#define SWAT_WORLD_H
#include "swat_math.h"
#include "materials.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_MAX_OBJECTS 1536
#define SWAT_MAX_ROOMS 8
typedef enum SwatHitKind { SWAT_HIT_NONE, SWAT_HIT_WORLD, SWAT_HIT_ACTOR, SWAT_HIT_PROJECTILE,SWAT_HIT_DEVICE } SwatHitKind;
typedef enum SwatPart { SWAT_PART_SOLID, SWAT_PART_SKIN, SWAT_PART_FRAME, SWAT_PART_SUPPORT } SwatPart;
typedef struct SwatRoom {
    b3Pos center;
    b3Vec3 half;
    SwatMaterial surface, floor;
} SwatRoom;
typedef struct SwatTag { SwatHitKind kind; int index; } SwatTag;
typedef struct SwatObject {
    SwatTag tag;
    b3BodyId body;
    b3ShapeId shape;
    b3Pos center;
    b3Vec3 half;
    float yaw, health, max_health, door_angle, closed_yaw,pitch;
    SwatMaterial material;
    SwatPart part;
    bool active, door, door_open,locked;
    int breach_owner,breach_ticks;
    int wedge_owner;
    bool peek,trapped;
    unsigned int trap_known;
    b3Pos hinge;
} SwatObject;

typedef struct SwatWorld {
    b3WorldId id;
    bool motel;
    b3MeshData* motel_meshes[40]; // Owned static collision; released after world destruction.
    SwatObject objects[SWAT_MAX_OBJECTS];
    int count, generation;
    SwatRoom rooms[SWAT_MAX_ROOMS];
    int room_count;
} SwatWorld;

typedef struct SwatHit {
    bool hit;
    SwatHitKind kind;
    int index;
    float fraction, distance;
    b3Pos point;
    b3Vec3 normal;
} SwatHit;

void swat_world_init(SwatWorld* world);
void swat_world_close(SwatWorld* world);
int swat_world_box(SwatWorld* world, b3Pos center, b3Vec3 half,
                   SwatMaterial material, float health);
void swat_world_build_range(SwatWorld* world, uint32_t* seed, bool randomize);
SwatHit swat_world_ray(const SwatWorld* world, b3Pos origin, b3Vec3 direction,
                       float range, b3BodyId ignore);
SwatHit swat_world_sphere_cast(const SwatWorld* world, b3Pos origin,
                              b3Vec3 translation, float radius, b3BodyId ignore);
bool swat_world_damage(SwatWorld* world, int object, float damage);
float swat_world_exit_distance(const SwatObject* object, b3Pos entry, b3Vec3 direction);
float swat_material_resistance(SwatMaterial material);
void swat_world_step_doors(SwatWorld* world);
void swat_world_place(SwatObject* object, float yaw);
void swat_world_tilt(SwatObject* object,float pitch);
int swat_world_room(const SwatWorld* world, b3Pos position);
b3SurfaceMaterial swat_physics_material(SwatMaterial material);
bool swat_world_visible(const SwatWorld* world,b3Pos from,b3Pos to);

#ifdef __cplusplus
}
#endif
#endif
