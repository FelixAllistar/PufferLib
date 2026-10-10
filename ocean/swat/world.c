#include "world.h"
#include "motel.h"
#include <assert.h>
#include <stdlib.h>
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
    w->attachment_first=SWAT_MAX_OBJECTS;
    b3WorldDef def = b3DefaultWorldDef();
    def.restitutionCallback=swat_bounce;
    w->id = b3CreateWorld(&def);
}

void swat_world_close(SwatWorld* w) {
    if (B3_IS_NON_NULL(w->id)) b3DestroyWorld(w->id);
    for(int i=0;i<SWAT_MOTEL_MESH_CAPACITY;i++) if(w->motel_meshes[i]) b3DestroyMesh(w->motel_meshes[i]);
    for(int i=0;i<40;i++) if(w->storefront_meshes[i]) b3DestroyMesh(w->storefront_meshes[i]);
    for(int i=0;i<SWAT_FENCE_PART_CAPACITY;i++)if(w->fence_meshes[i])b3DestroyMesh(w->fence_meshes[i]);
    for(int i=0;i<SWAT_SURROUNDINGS_PARTS;i++)if(w->surroundings_meshes[i])b3DestroyMesh(w->surroundings_meshes[i]);
    for(int i=0;i<w->motel_contact_mesh_count;i++)b3DestroyMesh(w->motel_contact_meshes[i]);
    free(w->motel_contact_meshes);
    for(int i=0;i<SWAT_GROUND_PARTS;i++)if(w->ground_meshes[i])b3DestroyMesh(w->ground_meshes[i]);
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
    o->breach_owner=o->wedge_owner=-1;
    for(int i=0;i<SWAT_MAX_SUPPORTS;i++)o->supports[i]=-1;
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

typedef struct SwatRayContext {
    SwatHit result;b3BodyId ignore;bool world_only;const SwatWorld* optical;
    const int* layers;int layer_count;
} SwatRayContext;
static float swat_ray_callback(b3ShapeId shape, b3Pos point, b3Vec3 normal,
    float fraction, uint64_t material, int triangle, int child, void* context) {
    (void)material; (void)triangle; (void)child;
    SwatRayContext* c = (SwatRayContext*)context;
    b3BodyId body = b3Shape_GetBody(shape);
    if (B3_ID_EQUALS(body,c->ignore)) return -1.0f;
    SwatTag* tag = (SwatTag*)b3Body_GetUserData(body);
    if(tag && tag->kind==SWAT_HIT_WORLD)for(int i=0;i<c->layer_count;i++)if(tag->index==c->layers[i])return -1.0f;
    if(tag && tag->kind==SWAT_HIT_PROJECTILE) return -1.0f;
    if(c->world_only && tag && tag->kind!=SWAT_HIT_WORLD) return -1.0f;
    if(c->optical && tag && tag->kind==SWAT_HIT_WORLD && tag->index>=0 &&
       tag->index<c->optical->count && c->optical->objects[tag->index].material==SWAT_GLASS)return -1.0f;
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

static SwatHit cast_ray(const SwatWorld* w,b3Pos origin,b3Vec3 direction,
                       float range,b3BodyId ignore,bool sight,const int* layers,int count) {
    SwatRayContext c = {0}; c.ignore = ignore;
    c.layers=layers;c.layer_count=count;
    if(sight)c.optical=w;
    c.result.fraction = 1; c.result.index = -1;
    b3Vec3 translation = swat_mul(swat_normalize(direction),range);
    c.result.point = b3OffsetPos(origin,translation);
    b3World_CastRay(w->id,origin,translation,b3DefaultQueryFilter(),swat_ray_callback,&c);
    c.result.distance = c.result.fraction*range;
    return c.result;
}
SwatHit swat_world_ray(const SwatWorld* w,b3Pos origin,b3Vec3 direction,float range,b3BodyId ignore) {
    return cast_ray(w,origin,direction,range,ignore,false,NULL,0);
}
SwatHit swat_world_sight_ray(const SwatWorld* w,b3Pos origin,b3Vec3 direction,float range,b3BodyId ignore) {
    return cast_ray(w,origin,direction,range,ignore,true,NULL,0);
}
SwatHit swat_world_layer_ray(const SwatWorld* w,b3Pos origin,b3Vec3 direction,float range,b3BodyId ignore,const int* layers,int count) {
    return cast_ray(w,origin,direction,range,ignore,false,layers,count);
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

bool swat_world_attach(SwatWorld* w,int object,const int* supports,int count) {
    if(object<0 || object>=w->count || count<1 || count>SWAT_MAX_SUPPORTS || !supports || !w->objects[object].active)return false;
    for(int i=0;i<count;i++) {
        if(supports[i]<0 || supports[i]>=object || !w->objects[supports[i]].active)return false;
        for(int j=0;j<i;j++)if(supports[j]==supports[i])return false;
    }
    SwatObject* o=&w->objects[object];
    for(int i=0;i<SWAT_MAX_SUPPORTS;i++)o->supports[i]=i<count?supports[i]:-1;
    if(object<w->attachment_first)w->attachment_first=object;
    return true;
}
static void remove_object(SwatWorld* w,SwatObject* o) {
    b3DestroyBody(o->body);
    o->body = b3_nullBodyId; o->shape = b3_nullShapeId; o->active = false;
    o->locked=false; o->breach_owner=o->wedge_owner=-1;
    o->trapped=o->peek=false; o->trap_known=0;
    o->breach_ticks=48;
    w->generation++;
}
bool swat_world_damage(SwatWorld* w, int object, float damage) {
    if (object < 0 || object >= w->count || damage <= 0) return false;
    SwatObject* o = &w->objects[object];
    if (!o->active || o->max_health <= 0) return false;
    o->health = fmaxf(0,o->health-damage);
    if (o->health > 0) return false;
    remove_object(w,o);
    // Topological owner order permits bounded, nonrecursive support propagation.
    // A failed fastening sheds its child even when the child is indestructible.
    int first=object+1>w->attachment_first?object+1:w->attachment_first;
    for(int i=first;i<w->count;i++) {
        SwatObject* child=&w->objects[i];if(!child->active || child->supports[0]<0)continue;
        for(int k=0;k<SWAT_MAX_SUPPORTS;k++)if(child->supports[k]>=0 && !w->objects[child->supports[k]].active) {
            child->health=0;remove_object(w,child);break;
        }
    }
    return true;
}

bool swat_world_impact(SwatWorld* w,int object,float damage) {
    if(object<0 || object>=w->count)return false;
    // Small repeated impacts do not accumulate into a doorway through masonry.
    // Thin boards, timber and glass still use their existing damage budgets.
    const SwatObject* o=&w->objects[object];const SwatMaterialDef* material=swat_material(o->material);
    float threshold=material->impact_threshold;
    if(o->part==SWAT_PART_FIXTURE)threshold*=o->structural_thickness/material->reference_thickness;
    return swat_world_damage(w,object,fmaxf(0,damage-threshold));
}

static float charge_demand(const SwatObject* o) {
    const SwatMaterialDef* m=swat_material(o->material);
    if(o->part==SWAT_PART_FIXTURE)return m->charge_resistance*o->structural_thickness/m->reference_thickness;
    // Fence infill fails at its thin wire/rail fastenings, not across the air
    // contained by a chunk's bounds. Posts retain their independent support.
    if(o->part==SWAT_PART_FENCE_WIRE || o->part==SWAT_PART_FENCE_RAIL)
        return m->charge_resistance*(o->part==SWAT_PART_FENCE_WIRE?.0025f:.003f)/m->reference_thickness;
    return m->charge_resistance*(2*o->half.x/m->reference_thickness);
}

bool swat_world_fragment(SwatObject* o,const float corners[4][2]) {
    if(!o->active || o->door || o->part!=SWAT_PART_SKIN) return false;
    b3Vec3 points[8];
    for(int i=0;i<4;i++) {
        int j=(i+1)%4,k=(i+2)%4;
        float ay=corners[j][0]-corners[i][0],az=corners[j][1]-corners[i][1];
        float by=corners[k][0]-corners[j][0],bz=corners[k][1]-corners[j][1];
        if(!isfinite(corners[i][0]) || !isfinite(corners[i][1]) ||
           fabsf(corners[i][0])>o->half.y+.001f || fabsf(corners[i][1])>o->half.z+.001f || ay*bz-az*by<.001f) return false;
        points[i]=swat_v(-o->half.x,corners[i][0],corners[i][1]);
        points[i+4]=swat_v(o->half.x,corners[i][0],corners[i][1]);
    }
    b3HullData* hull=b3CreateHull(points,8,8); if(!hull) return false;
    b3Shape_SetHull(o->shape,hull); b3DestroyHull(hull);
    memcpy(o->corners,corners,sizeof(o->corners)); o->fractured=true; return true;
}

bool swat_world_breachable(const SwatObject* o) {
    return o->active && o->max_health>0 && charge_demand(o)<=1 && (o->door || (o->wall_group>0 &&
        (o->part==SWAT_PART_SKIN || o->part==SWAT_PART_FRAME || o->part==SWAT_PART_FENCE_WIRE || o->part==SWAT_PART_FENCE_RAIL ||
         o->part==SWAT_PART_FIXTURE)));
}

int swat_world_breach(SwatWorld* w,int object,b3Pos position) {
    if(object<0 || object>=w->count || !swat_world_breachable(&w->objects[object])) return 0;
    SwatObject source=w->objects[object];
    if(source.door) return swat_world_damage(w,object,source.max_health) ? 1 : 0;
    bool fence=source.part==SWAT_PART_FENCE_WIRE || source.part==SWAT_PART_FENCE_RAIL;
    b3Vec3 tangent=swat_v(sinf(source.yaw),0,cosf(source.yaw));
    if(fence)tangent=swat_v(cosf(source.yaw),0,-sinf(source.yaw));
    // Localized game-space aperture. It crosses both wall faces and the
    // intervening stud segments, leaving the rest of the assembly intact.
    float floor_y=(float)source.center.y;
    for(int i=0;i<w->count;i++) if(w->objects[i].wall_group==source.wall_group)
        floor_y=fminf(floor_y,(float)w->objects[i].center.y-w->objects[i].half.y);
    b3Pos center=position; center.y=fmax(floor_y+1.05,position.y-.35);
    float aperture=.72f*swat_clamp(1-.25f*charge_demand(&source),.65f,1);
    int removed=0;
    for(int i=0;i<w->count;i++) {
        SwatObject* o=&w->objects[i];
        if(!swat_world_breachable(o) || o->door || o->wall_group!=source.wall_group) continue;
        b3Vec3 delta=b3SubPos(o->center,center);
        float z=fabsf(b3Dot(delta,tangent)),y=fabsf(delta.y);
        if(z<aperture+(fence?o->half.x:o->half.z)*.6f && y<1.10f+o->half.y*.45f)
            removed+=swat_world_damage(w,i,o->max_health);
    }
    bool framed=false;
    for(int i=0;i<w->count;i++)if(w->objects[i].wall_group==source.wall_group && w->objects[i].part==SWAT_PART_FRAME)framed=true;
    // Board islands without a surviving timber attachment shed locally.
    // This is a bounded wall-assembly support rule, not whole-building collapse.
    for(int i=0;i<w->count;i++) {
        SwatObject* skin=&w->objects[i];
        if(!framed || !skin->active || skin->part!=SWAT_PART_SKIN || skin->wall_group!=source.wall_group) continue;
        bool supported=false;
        for(int j=0;j<w->count && !supported;j++) {
            const SwatObject* frame=&w->objects[j];
            if(!frame->active || frame->wall_group!=source.wall_group ||
               (frame->part!=SWAT_PART_FRAME && frame->part!=SWAT_PART_SUPPORT)) continue;
            b3Vec3 d=b3SubPos(frame->center,skin->center);
            supported=fabsf(b3Dot(d,tangent))<frame->half.z+skin->half.z+.025f &&
                fabsf(d.y)<frame->half.y+skin->half.y+.025f;
        }
        if(!supported) removed+=swat_world_damage(w,i,skin->max_health);
    }
    return removed;
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

SwatRoomLight swat_world_room_light(const SwatWorld* w,int room) {
    SwatRoomLight light={0};if(room<0 || room>=w->room_count)return light;
    const SwatRoom* r=&w->rooms[room];
    light.origin=b3OffsetPos(r->center,swat_v(0,r->half.y-.18f,0));light.power=1;
    if(w->motel && room>=1 && room<=4) {
        light.direction=swat_v(0,-.9396926f,.3420201f);
        light.power=swat_motel_lamp(w,room,&light.origin)?1:0;
    } else for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];b3Vec3 d=b3SubPos(o->center,r->center);
        if(o->active && o->part==SWAT_PART_LIGHT && fabsf(d.x)<r->half.x &&
           fabsf(d.y)<r->half.y && fabsf(d.z)<r->half.z) {
            light.origin=b3OffsetPos(o->center,swat_v(0,-o->half.y-.01f,0));break;
        }
    }
    if(w->room_light_off_mask&(1u<<room))light.power=0;
    return light;
}
bool swat_world_sight_clear(const SwatWorld* w,b3Pos from,b3Pos to) {
    SwatRayContext c={0};c.result.fraction=1;c.world_only=true;c.optical=w;
    b3World_CastRay(w->id,from,b3SubPos(to,from),b3DefaultQueryFilter(),swat_ray_callback,&c);
    return !c.result.hit;
}
float swat_world_visual_range(const SwatWorld* w,b3Pos target,float daylight_range) {
    int room=swat_world_room(w,target);if(room<0)return daylight_range;
    // A roof opening restores daylight without a camera, shadow map or hidden
    // occupant state. Glass transmits this approximation; opaque cover blocks it.
    if(swat_world_sight_clear(w,target,b3OffsetPos(target,swat_v(0,64,0))))return daylight_range;
    SwatRoomLight light=swat_world_room_light(w,room);float exposure=0;
    if(light.power>0) {
        b3Vec3 delta=b3SubPos(target,light.origin);float d2=b3Dot(delta,delta),beam=1;
        if(b3Dot(light.direction,light.direction)>.5f) {
            float t=swat_clamp((b3Dot(swat_normalize(delta),light.direction)-.05f)/.30f,0,1);
            beam=t*t*(3-2*t); // Same cone and attenuation as the room shader.
        }
        if(beam>0 && swat_world_sight_clear(w,light.origin,target))exposure=light.power*beam*1.8f/(1+.18f*d2);
    }
    // Residual ambient keeps close targets detectable. Darkness lowers range
    // continuously, to half the daylight range; it never disables hearing.
    return daylight_range*sqrtf(.25f+.75f*swat_clamp(exposure,0,1));
}

typedef struct MeshExit { b3Vec3 entry,direction; float distance; } MeshExit;
static bool mesh_exit_triangle(b3Vec3 a,b3Vec3 b,b3Vec3 c,int triangle,void* context) {
    (void)triangle;MeshExit* q=context;
    b3Vec3 e1=b3Sub(b,a),e2=b3Sub(c,a),p=b3Cross(q->direction,e2);
    float det=b3Dot(e1,p);
    // Only an outward face closes the material entered by the original ray.
    if(det>=-1e-10f)return true;
    b3Vec3 t=b3Sub(q->entry,a);float u=b3Dot(t,p)/det;
    if(u<-.00001f || u>1.00001f)return true;
    b3Vec3 r=b3Cross(t,e1);float v=b3Dot(q->direction,r)/det;
    if(v<-.00001f || u+v>1.00001f)return true;
    float distance=b3Dot(e2,r)/det;
    if(distance>.00001f && distance<q->distance)q->distance=distance;
    return true;
}
float swat_world_exit_distance(const SwatObject* o,b3Pos entry,b3Vec3 d) {
    b3Quat q=b3MulQuat(b3MakeQuatFromAxisAngle(swat_v(0,1,0),o->yaw),b3MakeQuatFromAxisAngle(swat_v(0,0,1),o->pitch));
    b3Vec3 rel=b3InvRotateVector(q,b3SubPos(entry,o->center));
    b3Vec3 direction=b3InvRotateVector(q,d);
    float p[3]={rel.x,rel.y,rel.z},v[3]={direction.x,direction.y,direction.z};
    float half[3] = {o->half.x,o->half.y,o->half.z};
    float exit = 1e6f;
    if(o->fractured) {
        if(fabsf(direction.x)>1e-7f) exit=((direction.x>0 ? o->half.x : -o->half.x)-rel.x)/direction.x;
        for(int i=0;i<4;i++) {
            int j=(i+1)%4;
            float ey=o->corners[j][0]-o->corners[i][0],ez=o->corners[j][1]-o->corners[i][1];
            float inside=ey*(rel.z-o->corners[i][1])-ez*(rel.y-o->corners[i][0]);
            float rate=ey*direction.z-ez*direction.y;
            if(rate< -1e-7f) exit=fminf(exit,-inside/rate);
        }
        return exit>=-.001f && exit<1e6f ? fmaxf(0,exit) : 0;
    }
    for (int i=0;i<3;i++) if (fabsf(v[i]) > 1e-7f) {
        float distance = ((v[i] > 0 ? half[i] : -half[i])-p[i])/v[i];
        if (distance >= -0.001f) exit = fminf(exit,fmaxf(0,distance));
    }
    if(exit==1e6f)return 0;
    if(B3_IS_NON_NULL(o->shape) && b3Shape_GetType(o->shape)==b3_meshShape) {
        // A furniture/fence bounding box contains air. Find the first actual
        // outward triangle through the mesh BVH, then let the next world cast
        // encounter subsequent pieces (or a person behind an opening).
        b3Mesh mesh=o->query_mesh.data ? o->query_mesh : b3Shape_GetMesh(o->shape);
        b3Vec3 end=b3Add(rel,swat_mul(direction,exit+.001f)),pad=swat_v(.001f,.001f,.001f);
        b3AABB bounds={b3Sub(b3Min(rel,end),pad),b3Add(b3Max(rel,end),pad)};
        MeshExit query={rel,direction,exit+.002f};
        b3QueryMesh(&mesh,bounds,mesh_exit_triangle,&query);
        return query.distance<=exit+.001f ? query.distance : 0; // Open/non-manifold surfaces conservatively stop the shot.
    }
    return exit;
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
        float target = o->door_open ? (o->peek ? 12*SWAT_RAD : SWAT_PI*0.5f) : 0;
        if(o->wedge_owner>=0) target=0;
        float delta = swat_clamp(target-o->door_angle,-2.0f*SWAT_DT,2.0f*SWAT_DT);
        if (fabsf(delta) < 1e-7f) continue;
        if (swat_door_obstructed(w,o,o->door_angle+delta)) continue;
        o->door_angle += delta; o->yaw = o->closed_yaw+o->door_angle;
        o->center = b3OffsetPos(o->hinge,swat_v(sinf(o->yaw)*o->half.z,0,cosf(o->yaw)*o->half.z));
        b3Quat rotation = {{0,sinf(o->yaw*0.5f),0},cosf(o->yaw*0.5f)};
        b3Body_SetTransform(o->body,o->center,rotation);
    }
}

void swat_world_tilt(SwatObject* o,float pitch) {
    o->pitch=pitch;
    b3Quat q=b3MulQuat(b3MakeQuatFromAxisAngle(swat_v(0,1,0),o->yaw),b3MakeQuatFromAxisAngle(swat_v(0,0,1),pitch));
    b3Body_SetTransform(o->body,o->center,q);
}
