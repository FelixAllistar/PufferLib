#include "world.h"
#include <assert.h>
#include <string.h>

static float swat_bounce(float a,uint64_t ma,float b,uint64_t mb) {
    (void)ma; (void)mb;
    return sqrtf(a*b); // a soft receiving surface damps a hard projectile
}
b3SurfaceMaterial swat_physics_material(SwatMaterial material) {
    const SwatMaterialDef* m=swat_material(material);
    b3SurfaceMaterial result={0};
    result.friction=m->friction; result.restitution=m->restitution;
    result.rollingResistance=m->rolling_resistance; result.userMaterialId=(uint64_t)material;
    return result;
}
void swat_world_init(SwatWorld* w) {
    memset(w,0,sizeof(*w));
    b3WorldDef def = b3DefaultWorldDef();
    def.restitutionCallback=swat_bounce;
    w->id = b3CreateWorld(&def);
}

void swat_world_close(SwatWorld* w) {
    if (B3_IS_NON_NULL(w->id)) b3DestroyWorld(w->id);
    memset(w,0,sizeof(*w));
}

int swat_world_box(SwatWorld* w, b3Pos center, b3Vec3 half, SwatMaterial material, float hp) {
    assert(w->count < SWAT_MAX_OBJECTS);
    int index = w->count++;
    SwatObject* o = &w->objects[index];
    memset(o,0,sizeof(*o));
    o->tag = (SwatTag){SWAT_HIT_WORLD,index};
    o->center = center; o->half = half; o->material = material;
    o->health = o->max_health = hp;
    o->active = true;
    o->breach_owner=-1;
    b3BodyDef bd = b3DefaultBodyDef();
    bd.position = center;
    o->body = b3CreateBody(w->id,&bd);
    b3Body_SetUserData(o->body,&o->tag);
    b3ShapeDef sd = b3DefaultShapeDef();
    sd.baseMaterial=swat_physics_material(material); sd.density=swat_material(material)->density;
    b3BoxHull hull = b3MakeBoxHull(half.x,half.y,half.z);
    o->shape = b3CreateHullShape(o->body,&sd,&hull.base);
    return index;
}

void swat_world_build_range(SwatWorld* w, uint32_t* seed, bool randomize) {
    swat_world_box(w,(b3Pos){10,-0.5f,0},swat_v(14,0.5f,7),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){10,1.8f,-7},swat_v(14,1.8f,0.2f),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){10,1.8f,7},swat_v(14,1.8f,0.2f),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){-4,1.8f,0},swat_v(0.2f,1.8f,7),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){24,1.8f,0},swat_v(0.2f,1.8f,7),SWAT_CONCRETE,0);

    // Partition with a hinged door in the central opening.
    swat_world_box(w,(b3Pos){7,1.8f,-4.0f},swat_v(0.18f,1.8f,2.8f),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){7,1.8f,4.0f},swat_v(0.18f,1.8f,2.8f),SWAT_CONCRETE,0);
    swat_world_box(w,(b3Pos){7,3.1f,0},swat_v(0.18f,0.5f,1.2f),SWAT_CONCRETE,0);
    int door = swat_world_box(w,(b3Pos){7,1.25f,0},swat_v(0.07f,1.25f,1.15f),SWAT_WOOD,105);
    w->objects[door].door = true;
    w->objects[door].hinge = (b3Pos){7,1.25f,-1.15f};

    // A tiled freestanding wall. Each cell owns a real collider and can break.
    for (int y=0;y<4;y++) for (int z=0;z<6;z++)
        swat_world_box(w,(b3Pos){13,0.35f+y*0.7f,-1.75f+z*0.7f},
            swat_v(0.08f,0.35f,0.35f),SWAT_DRYWALL,45);
    float shift = randomize ? (swat_rand01(seed)-0.5f)*1.0f : 0.0f;
    swat_world_box(w,(b3Pos){3.7f,0.6f,-2.0f+shift},swat_v(0.6f,0.6f,1),SWAT_WOOD,85);
    swat_world_box(w,(b3Pos){10,0.6f,3.0f+shift},swat_v(0.6f,0.6f,1),SWAT_WOOD,85);
    swat_world_box(w,(b3Pos){18,1.2f,-3.5f},swat_v(0.035f,1.2f,1.0f),SWAT_GLASS,8);
    // Low clearance station for stance/lean testing along the near wall.
    swat_world_box(w,(b3Pos){3.0f,2.0f,4.8f},swat_v(1.8f,0.8f,1.0f),SWAT_CONCRETE,0);
}

typedef struct SwatRayContext { SwatHit result; b3BodyId ignore; bool world_only; } SwatRayContext;
static float swat_ray_callback(b3ShapeId shape, b3Pos point, b3Vec3 normal,
    float fraction, uint64_t material, int triangle, int child, void* context) {
    (void)material; (void)triangle; (void)child;
    SwatRayContext* c = (SwatRayContext*)context;
    b3BodyId body = b3Shape_GetBody(shape);
    if (B3_ID_EQUALS(body,c->ignore)) return -1.0f;
    SwatTag* tag = (SwatTag*)b3Body_GetUserData(body);
    if(tag && tag->kind==SWAT_HIT_PROJECTILE) return -1.0f;
    if(c->world_only && tag && tag->kind!=SWAT_HIT_WORLD) return -1.0f;
    if (fraction <= c->result.fraction) {
        c->result.hit = true;
        c->result.fraction = fraction;
        c->result.point = point;
        c->result.normal = normal;
        c->result.kind = tag ? tag->kind : SWAT_HIT_WORLD;
        c->result.index = tag ? tag->index : -1;
    }
    return c->result.fraction;
}

SwatHit swat_world_ray(const SwatWorld* w, b3Pos origin, b3Vec3 direction,
                       float range, b3BodyId ignore) {
    SwatRayContext c = {0}; c.ignore = ignore;
    c.result.fraction = 1; c.result.index = -1;
    b3Vec3 translation = swat_mul(swat_normalize(direction),range);
    c.result.point = b3OffsetPos(origin,translation);
    b3World_CastRay(w->id,origin,translation,b3DefaultQueryFilter(),swat_ray_callback,&c);
    c.result.distance = c.result.fraction*range;
    return c.result;
}

SwatHit swat_world_sphere_cast(const SwatWorld* w, b3Pos origin,
                              b3Vec3 translation, float radius, b3BodyId ignore) {
    SwatRayContext c = {0}; c.ignore = ignore;
    c.result.fraction = 1; c.result.index = -1;
    b3Vec3 point = {0};
    b3ShapeProxy proxy = {&point,1,radius};
    b3World_CastShape(w->id,origin,&proxy,translation,b3DefaultQueryFilter(),swat_ray_callback,&c);
    c.result.distance = c.result.fraction*b3Length(translation);
    return c.result;
}

bool swat_world_visible(const SwatWorld* w,b3Pos from,b3Pos to) {
    SwatRayContext c={0}; c.result.fraction=1; c.world_only=true;
    b3World_CastRay(w->id,from,b3SubPos(to,from),b3DefaultQueryFilter(),swat_ray_callback,&c);
    return !c.result.hit;
}

bool swat_world_damage(SwatWorld* w, int object, float damage) {
    if (object < 0 || object >= w->count || damage <= 0) return false;
    SwatObject* o = &w->objects[object];
    if (!o->active || o->max_health <= 0) return false;
    o->health = fmaxf(0,o->health-damage);
    if (o->health > 0) return false;
    b3DestroyBody(o->body);
    o->body = b3_nullBodyId; o->shape = b3_nullShapeId; o->active = false;
    o->locked=false; o->breach_owner=-1;
    w->generation++;
    return true;
}

float swat_material_resistance(SwatMaterial material) {
    return swat_material(material)->resistance;
}

void swat_world_place(SwatObject* o, float yaw) {
    o->yaw=yaw;
    b3Quat rotation={{0,sinf(yaw*.5f),0},cosf(yaw*.5f)};
    b3Body_SetTransform(o->body,o->center,rotation);
}

int swat_world_room(const SwatWorld* w,b3Pos p) {
    for(int i=0;i<w->room_count;i++) {
        const SwatRoom* r=&w->rooms[i]; b3Vec3 d=b3SubPos(p,r->center);
        if(fabsf(d.x)<r->half.x && fabsf(d.y)<r->half.y && fabsf(d.z)<r->half.z) return i;
    }
    return -1;
}

float swat_world_exit_distance(const SwatObject* o, b3Pos entry, b3Vec3 d) {
    b3Vec3 rel = b3SubPos(entry,o->center);
    float c = cosf(o->yaw), s = sinf(o->yaw);
    float p[3] = {c*rel.x-s*rel.z,rel.y,s*rel.x+c*rel.z};
    float v[3] = {c*d.x-s*d.z,d.y,s*d.x+c*d.z};
    float half[3] = {o->half.x,o->half.y,o->half.z};
    float exit = 1e6f;
    for (int i=0;i<3;i++) if (fabsf(v[i]) > 1e-7f) {
        float distance = ((v[i] > 0 ? half[i] : -half[i])-p[i])/v[i];
        if (distance >= -0.001f) exit = fminf(exit,fmaxf(0,distance));
    }
    return exit == 1e6f ? 0 : exit;
}

static bool swat_door_overlap(b3ShapeId shape, void* context) {
    SwatTag* tag = (SwatTag*)b3Body_GetUserData(b3Shape_GetBody(shape));
    if (!tag || tag->kind != SWAT_HIT_ACTOR) return true;
    *(bool*)context = true;
    return false;
}

static bool swat_door_obstructed(const SwatWorld* w, const SwatObject* o, float next) {
    // Convex wrap of both poses, expanded to cover the short rotational arc.
    // Only actors block this authored door; the frame touches the hinge.
    b3Vec3 points[16];
    for (int pose=0;pose<2;pose++) {
        float angle = o->closed_yaw+(pose ? next : o->door_angle);
        float c = cosf(angle), s = sinf(angle);
        for (int j=0;j<8;j++) {
            float x = j&1 ? o->half.x : -o->half.x;
            float y = j&2 ? o->half.y : -o->half.y;
            float z = j&4 ? 2*o->half.z : 0;
            points[pose*8+j] = swat_v(c*x+s*z,y,-s*x+c*z);
        }
    }
    float arc_margin = 2.0f*b3Length(o->half)*(1.0f-cosf((next-o->door_angle)*0.5f));
    b3ShapeProxy proxy = {points,16,arc_margin+0.005f};
    bool blocked = false;
    b3World_OverlapShape(w->id,o->hinge,&proxy,b3DefaultQueryFilter(),swat_door_overlap,&blocked);
    return blocked;
}

void swat_world_step_doors(SwatWorld* w) {
    for (int i=0;i<w->count;i++) {
        SwatObject* o = &w->objects[i];
        if(o->breach_ticks>0) o->breach_ticks--;
        if (!o->active || !o->door) continue;
        float target = o->door_open ? SWAT_PI*0.5f : 0;
        float delta = swat_clamp(target-o->door_angle,-2.0f*SWAT_DT,2.0f*SWAT_DT);
        if (fabsf(delta) < 1e-7f) continue;
        if (swat_door_obstructed(w,o,o->door_angle+delta)) continue;
        o->door_angle += delta; o->yaw = o->closed_yaw+o->door_angle;
        o->center = b3OffsetPos(o->hinge,swat_v(sinf(o->yaw)*o->half.z,0,cosf(o->yaw)*o->half.z));
        b3Quat rotation = {{0,sinf(o->yaw*0.5f),0},cosf(o->yaw*0.5f)};
        b3Body_SetTransform(o->body,o->center,rotation);
    }
}
