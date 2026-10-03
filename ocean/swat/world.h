#ifndef SWAT_WORLD_H
#define SWAT_WORLD_H
#include "swat_math.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_MAX_OBJECTS 192
typedef enum SwatHitKind { SWAT_HIT_NONE, SWAT_HIT_WORLD, SWAT_HIT_ACTOR } SwatHitKind;
typedef enum SwatMaterial { SWAT_CONCRETE, SWAT_DRYWALL, SWAT_WOOD, SWAT_GLASS, SWAT_STEEL } SwatMaterial;
typedef struct SwatTag { SwatHitKind kind; int index; } SwatTag;
typedef struct SwatObject {
    SwatTag tag;
    b3BodyId body;
    b3ShapeId shape;
    b3Pos center;
    b3Vec3 half;
    float yaw, health, max_health, door_angle;
    SwatMaterial material;
    bool active, door, door_open;
    b3Pos hinge;
} SwatObject;

typedef struct SwatWorld {
    b3WorldId id;
    SwatObject objects[SWAT_MAX_OBJECTS];
    int count, generation;
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

#ifdef __cplusplus
}
#endif
#endif
