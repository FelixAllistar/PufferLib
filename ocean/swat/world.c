#include "world.h"
#include "motel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "door_damage.h"
#include "fracture_geometry.h"
#include "damage_profile.h"
_Thread_local SwatDamageProfile swat_damage_profile;
static SwatFracture* swat_sections_geometry(const SwatObject*,const SwatCut*,int,const SwatFracturePolygon*,int);
static bool swat_sections_equal(const SwatObject*,const SwatFracture*);
static bool swat_sections_hulls(const SwatObject*,SwatFracture*);
#include "door_motion.h"
#include "wall_support.h"

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
    if(w->rubble){for(int i=0;i<w->rubble->count;i++)rubble_remove(&w->rubble->pieces[i]);free(w->rubble);}
    if(w->support){for(int i=0;i<w->count;i++)swat_world_cut_close(w->support->before[i]);free(w->support);}
    for(int i=0;i<w->count;i++){swat_world_cut_close(w->objects[i].cuts);free(w->objects[i].motion);}
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
    if(tag && (tag->kind==SWAT_HIT_WORLD || tag->kind==SWAT_HIT_RUBBLE))for(int i=0;i<c->layer_count;i++)
        if(tag->index+(tag->kind==SWAT_HIT_RUBBLE?SWAT_MAX_OBJECTS:0)==c->layers[i])return -1.0f;
    if(tag && tag->kind==SWAT_HIT_PROJECTILE) return -1.0f;
    if(c->world_only && tag && tag->kind!=SWAT_HIT_WORLD && tag->kind!=SWAT_HIT_RUBBLE) return -1.0f;
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
bool swat_world_object_ray(const SwatObject* o,b3Pos origin,b3Vec3 translation) {
    if(!o->active || B3_IS_NULL(o->body))return false;
    b3ShapeId shapes[SWAT_CUT_SECTIONS];int count=b3Body_GetShapes(o->body,shapes,SWAT_CUT_SECTIONS);
    for(int i=0;i<count;i++)if(b3Shape_RayCast(shapes[i],origin,translation).hit)return true;
    return false;
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
    door_motion_stop(o);
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
    support_changed(w,object);return true;
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
bool swat_world_impact_at(SwatWorld* w,int object,b3Pos point,float damage,bool door_round) {
    if(object<0 || object>=w->count || !isfinite(damage) || damage<=0)return false;
    SwatObject* o=&w->objects[object];if(!o->active)return false;
    b3Vec3 local=b3InvRotateVector(swat_object_rotation(o),b3SubPos(point,o->center));
    if(o->door && door_round && local.z>o->half.z-.32f && fabsf(local.y-.06f)<.36f) {
        // Authored hardware interaction in the game, not a whole-leaf damage
        // multiplier or a real-world ballistic breaching procedure.
        if(swat_world_breachable(o) && swat_world_break_latch(w,object))return true;
    }
    bool board=o->material==SWAT_DRYWALL || o->material==SWAT_PLASTER || o->material==SWAT_INSULATION;
    if((o->part==SWAT_PART_SKIN && board) || ((o->part==SWAT_PART_FRAME || o->door) && o->material==SWAT_WOOD)) {
        const SwatMaterialDef* m=swat_material(o->material);float useful=fmaxf(0,damage-m->impact_threshold);
        if(!useful)return false;
        float radius=swat_clamp((board?.018f:.0125f)*sqrtf(useful/fmaxf(m->fracture_health,1)),.005f,.04f);
        SwatObject source=*o;bool changed=false;support_begin(w);
        int first=!o->door && o->wall_group>0?o->wall_group-1:object;
        for(int i=first;i<w->count;i++) {
            SwatObject* peer=&w->objects[i];
            if(source.door && i!=object)break;
            if(!source.door && peer->wall_group!=source.wall_group)break;
            if(!peer->active || peer->door!=source.door || peer->part!=source.part || peer->material!=source.material ||
               fabsf(peer->half.x-source.half.x)>.001f)continue;
            b3Vec3 p=b3InvRotateVector(swat_object_rotation(peer),b3SubPos(point,peer->center));
            if(fabsf(p.x)>peer->half.x+.002f)continue; // Only the contacted sheet/frame layer.
            SwatCut cut=swat_impact_patch(peer,p.y,p.z,radius);
            changed|=swat_world_cut(w,i,cut)>0;
        }
        support_end(w);
        if(changed && o->door)door_motion_impulse(o,point,fminf(4,damage*.045f));
        return changed;
    }
    return swat_world_impact(w,object,damage);
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

bool swat_world_fragment_valid(const SwatObject* o,const float corners[4][2]) {
    if(o->door || o->part!=SWAT_PART_SKIN)return false;
    for(int i=0;i<4;i++) {
        int j=(i+1)%4,k=(i+2)%4;
        float ay=corners[j][0]-corners[i][0],az=corners[j][1]-corners[i][1];
        float by=corners[k][0]-corners[j][0],bz=corners[k][1]-corners[j][1];
        if(!isfinite(corners[i][0]) || !isfinite(corners[i][1]) ||
           fabsf(corners[i][0])>o->half.y+.001f || fabsf(corners[i][1])>o->half.z+.001f || ay*bz-az*by<.001f) return false;
    }
    return true;
}
b3HullData* swat_world_fragment_hull(const SwatObject* o,const float corners[4][2]) {
    if(!swat_world_fragment_valid(o,corners))return NULL;
    b3Vec3 points[8];
    for(int i=0;i<4;i++) {
        points[i]=swat_v(-o->half.x,corners[i][0],corners[i][1]);
        points[i+4]=swat_v(o->half.x,corners[i][0],corners[i][1]);
    }
    return b3CreateHull(points,8,8);
}
bool swat_world_fragment(SwatObject* o,const float corners[4][2]) {
    if(!o->active)return false;
    if(o->cuts)return !memcmp(o->corners,corners,sizeof(o->corners));
    b3HullData* hull=swat_world_fragment_hull(o,corners);if(!hull)return false;
    b3Shape_SetHull(o->shape,hull); b3DestroyHull(hull);
    memcpy(o->corners,corners,sizeof(o->corners)); o->fractured=true; return true;
}

void swat_world_cut_close(SwatFracture* f) {
    if(!f)return;
    if(f->pending_hulls) {
        for(int i=0;i<f->section_count;i++)if(f->pending_hulls[i])b3DestroyHull(f->pending_hulls[i]);
        free(f->pending_hulls);
    }
    free(f->sections);free(f);
}
SwatFracture* swat_world_cut_prepare(const SwatObject* o,const SwatCut* cuts,int count) {
    if(count<0 || count>SWAT_MAX_CUTS || (count && !cuts) || (!count && o->support_role!=SWAT_SUPPORT_MEMBER) ||
       (!o->door && o->part!=SWAT_PART_SKIN && o->part!=SWAT_PART_FRAME))return NULL;
    SwatFracturePolygon pieces[SWAT_CUT_SECTIONS];int n=swat_cut_polygons(o,cuts,count,pieces);
    if(n<0)return NULL;
    return swat_world_sections_prepare(o,cuts,count,pieces,n);
}
SwatFracture* swat_world_sections_prepare(const SwatObject* o,const SwatCut* cuts,int count,const SwatFracturePolygon* pieces,int n) {
    SwatFracture* f=swat_sections_geometry(o,cuts,count,pieces,n);
    if(f && !swat_sections_hulls(o,f)){swat_world_cut_close(f);return NULL;}
    return f;
}
static SwatFracture* swat_sections_geometry(const SwatObject* o,const SwatCut* cuts,int count,const SwatFracturePolygon* pieces,int n) {
    if(count<0 || count>SWAT_MAX_CUTS || n<0 || n>SWAT_CUT_SECTIONS)return NULL;
    SwatFracture* f=calloc(1,sizeof(*f));if(!f)return NULL;
    f->count=count;f->section_count=n;if(count)memcpy(f->cuts,cuts,(size_t)count*sizeof(*cuts));
    if(n) {
        f->sections=malloc((size_t)n*sizeof(*f->sections));
        if(!f->sections){swat_world_cut_close(f);return NULL;}
        memcpy(f->sections,pieces,(size_t)n*sizeof(*pieces));
        SwatCutPlane planes[SWAT_MAX_CUTS][SWAT_CUT_SIDES];
        for(int c=0;c<count;c++)swat_cut_planes(&cuts[c],planes[c]);
        SwatFracturePolygon original;swat_source_polygon(o,&original);
        for(int s=0;s<n;s++) {
            SwatFracturePolygon* p=&f->sections[s];p->exposed_edges=0;
            memset(p->vertices+p->count,0,(size_t)(SWAT_CUT_VERTICES-p->count)*sizeof(*p->vertices));
            for(int e=0;e<p->count;e++) {
                const float* a=p->vertices[e];const float* b=p->vertices[(e+1)%p->count];
                float dy=b[0]-a[0],dz=b[1]-a[1],length=hypotf(dy,dz);
                float y=.5f*(a[0]+b[0])+.0001f*dz/length,z=.5f*(a[1]+b[1])-.0001f*dy/length;
                bool present=swat_polygon_contains(&original,y,z,1e-6f) &&
                    (!o->door || !o->latch_broken || z<=swat_door_edge(o,y));
                for(int c=0;c<count && present;c++) {
                    bool inside=true;
                    for(int e=0;e<swat_cut_plane_count(&cuts[c]) && inside;e++)inside=planes[c][e].y*y+planes[c][e].z*z<=planes[c][e].offset;
                    if(inside)present=false;
                }
                if(!present)p->exposed_edges|=1u<<e;
            }
        }
    }
    return f;
}
// Shape identity depends on the extrusion, not cut history or exposed-edge paint.
static bool swat_sections_equal(const SwatObject* o,const SwatFracture* f) {
    SwatFracturePolygon source;swat_source_polygon(o,&source);
    int n=o->cuts?o->cuts->section_count:1;
    if(n!=f->section_count)return false;
    for(int s=0;s<n;s++) {
        const SwatFracturePolygon* p=o->cuts?&o->cuts->sections[s]:&source;
        if(p->count!=f->sections[s].count || memcmp(p->vertices,f->sections[s].vertices,(size_t)p->count*sizeof(*p->vertices)))return false;
    }
    return true;
}
static bool swat_sections_hulls(const SwatObject* o,SwatFracture* f) {
    if(!f->section_count)return true;
    f->pending_hulls=calloc((size_t)f->section_count,sizeof(*f->pending_hulls));if(!f->pending_hulls)return false;
    for(int s=0;s<f->section_count;s++) {
            const SwatFracturePolygon* p=&f->sections[s];b3Vec3 points[2*SWAT_CUT_VERTICES];
            for(int i=0;i<p->count;i++) {
                points[i]=swat_v(-o->half.x,p->vertices[i][0],p->vertices[i][1]);
                points[i+p->count]=swat_v(o->half.x,p->vertices[i][0],p->vertices[i][1]);
            }
            uint64_t start=swat_profile_begin();
            f->pending_hulls[s]=b3CreateHull(points,2*p->count,2*SWAT_CUT_VERTICES);
            SWAT_PROFILE_ADD(hulls,1);swat_profile_end(&swat_damage_profile.hull_ms,start);
            if(!f->pending_hulls[s])return false;
    }
    return true;
}
void swat_world_cut_apply(SwatWorld* w,int object,SwatFracture* f) {
    uint64_t start=swat_profile_begin();
    SwatObject* o=&w->objects[object];
    if(o->active && !swat_sections_equal(o,f)) {
        b3ShapeId shapes[SWAT_CUT_SECTIONS];int count=b3Body_GetShapes(o->body,shapes,SWAT_CUT_SECTIONS);
        assert(count==b3Body_GetShapeCount(o->body));
        SWAT_PROFILE_ADD(shape_rebuilds,1);SWAT_PROFILE_ADD(shapes_destroyed,count);SWAT_PROFILE_ADD(shapes_created,f->section_count);
        for(int i=0;i<count;i++)b3DestroyShape(shapes[i],false);
        o->shape=b3_nullShapeId;
        b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);def.density=swat_material(o->material)->density;
        for(int i=0;i<f->section_count;i++) {
            b3ShapeId id=b3CreateHullShape(o->body,&def,f->pending_hulls[i]);
            if(!i)o->shape=id;
        }
    }
    swat_world_cut_close(o->cuts);o->cuts=f;
    if(f->pending_hulls) {
        for(int i=0;i<f->section_count;i++)b3DestroyHull(f->pending_hulls[i]);
        free(f->pending_hulls);f->pending_hulls=NULL;
    }
    swat_profile_end(&swat_damage_profile.collision_ms,start);
}
int swat_world_cut(SwatWorld* w,int object,SwatCut cut) {
    if(object<0 || object>=w->count || !swat_cut_valid(&cut))return -1;
    SwatObject* o=&w->objects[object];if(!o->active || !o->max_health)return 0;
    if(fabsf(cut.y)>o->half.y+cut.height*1.03f || fabsf(cut.z)>o->half.z+cut.width*1.03f)return 0;
    int count=o->cuts?o->cuts->count:0;
    if(count<0 || count>SWAT_MAX_CUTS)return -1;
    for(int i=0;i<count;i++)if(!memcmp(&cut,&o->cuts->cuts[i],sizeof(cut)))return 0;
    SwatCut cuts[SWAT_MAX_CUTS];if(count)memcpy(cuts,o->cuts->cuts,(size_t)count*sizeof(*cuts));
    int patch=-1;
    if(cut.kind>=SWAT_CUT_PATCH)for(int i=0;i<count;i++)if(cuts[i].kind==cut.kind){patch=i;break;}
    if(patch>=0) {
        if(swat_cut_grows(&cut,&cuts[patch]))return 0;
        cuts[patch]=swat_patch_union(cuts[patch],cut);
    }
    else {if(count==SWAT_MAX_CUTS)return -1;cuts[count++]=cut;}
    SwatFracture* prepared;
    if(o->cuts && o->cuts->support_filtered) {
        SwatFracturePolygon pieces[SWAT_CUT_SECTIONS];
        int n=swat_cut_polygons_from(o,cuts,count,o->cuts->sections,o->cuts->section_count,pieces);
        prepared=n<0?NULL:swat_world_sections_prepare(o,cuts,count,pieces,n);
        if(prepared)prepared->support_filtered=true;
    } else prepared=swat_world_cut_prepare(o,cuts,count);
    if(!prepared)return -1;
    float old_area=0,new_area=0;
    if(o->cuts)for(int i=0;i<o->cuts->section_count;i++)old_area+=swat_polygon_area(&o->cuts->sections[i]);
    else {SwatFracturePolygon p;swat_source_polygon(o,&p);old_area=swat_polygon_area(&p);}
    for(int i=0;i<prepared->section_count;i++)new_area+=swat_polygon_area(&prepared->sections[i]);
    if(new_area>=old_area-1e-6f){swat_world_cut_close(prepared);return 0;}
    SwatDoorMotion* motion=NULL;
    if(o->door && !o->motion && prepared->section_count) {
        SwatObject candidate=*o;candidate.cuts=prepared;
        if(swat_world_door_hinges(&candidate) && !(motion=swat_world_door_prepare())){swat_world_cut_close(prepared);return -1;}
    }
    support_preserve(w,object);swat_world_cut_apply(w,object,prepared);
    if(!prepared->section_count){swat_world_damage(w,object,o->max_health);return 2;}
    if(motion)door_motion_begin(o,motion);
    door_motion_refresh(w,o,true);
    o->health*=new_area/old_area;w->generation++;support_changed(w,object);return o->active?1:2;
}

bool swat_world_breachable(const SwatObject* o) {
    return o->active && o->max_health>0 && charge_demand(o)<=1 && (o->door || (o->wall_group>0 &&
        (o->part==SWAT_PART_SKIN || o->part==SWAT_PART_FRAME || o->part==SWAT_PART_FENCE_WIRE || o->part==SWAT_PART_FENCE_RAIL ||
         o->part==SWAT_PART_FIXTURE)));
}

bool swat_world_break_latch(SwatWorld* w,int object) {
    if(object<0 || object>=w->count)return false;
    SwatObject* o=&w->objects[object];
    if(!o->active || !o->door || o->latch_broken)return false;
    if(o->cuts) {
        SwatObject source=*o;source.latch_broken=true;
        SwatFracture* prepared=swat_world_cut_prepare(&source,o->cuts->cuts,o->cuts->count);
        if(!prepared || !prepared->section_count){swat_world_cut_close(prepared);return false;}
        swat_world_cut_apply(w,object,prepared);
    } else {
    b3HullData* hulls[SWAT_DOOR_SECTIONS]={0};
    SwatObject part=*o;part.door=false;part.part=SWAT_PART_SKIN;
    for(int i=0;i<SWAT_DOOR_SECTIONS;i++) {
        float corners[4][2];swat_door_section(o,i,corners);
        hulls[i]=swat_world_fragment_hull(&part,corners);
        if(!hulls[i]) {for(int j=0;j<i;j++)b3DestroyHull(hulls[j]);return false;}
    }
    b3ShapeId shapes[SWAT_DOOR_SECTIONS];int count=b3Body_GetShapes(o->body,shapes,SWAT_DOOR_SECTIONS);
    for(int i=0;i<count;i++)b3DestroyShape(shapes[i],false);
    b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);def.density=swat_material(o->material)->density;
    for(int i=0;i<SWAT_DOOR_SECTIONS;i++) {
        b3ShapeId shape=b3CreateHullShape(o->body,&def,hulls[i]);b3DestroyHull(hulls[i]);
        if(!i)o->shape=shape;
    }
    }
    o->latch_broken=true;o->locked=false;o->peek=false;
    door_motion_refresh(w,o,true);
    o->breach_ticks=24;w->generation++;
    return true;
}

int swat_world_breach(SwatWorld* w,int object,b3Pos position) {
    if(object<0 || object>=w->count || !swat_world_breachable(&w->objects[object])) return 0;
    SwatObject source=w->objects[object];
    if(source.door) {
        b3Vec3 mount=b3InvRotateVector(swat_object_rotation(&source),b3SubPos(position,source.center));
        SwatCut local={.y=mount.y,.z=mount.z,.height=.18f,.width=.14f,
            .seed=(unsigned int)object,.kind=SWAT_CUT_BREACH};
        int changed=swat_world_cut(w,object,local);
        // Losing leaf material is independent of releasing the hardware, and
        // neither operation commands the surviving hinged leaf to swing open.
        if(changed>0 && swat_cut_contains(&local,.06f,source.half.z-.01f))swat_world_break_latch(w,object);
        return changed>0?1:0;
    }
    support_begin(w);
    bool fence=source.part==SWAT_PART_FENCE_WIRE || source.part==SWAT_PART_FENCE_RAIL;
    b3Vec3 tangent=swat_v(sinf(source.yaw),0,cosf(source.yaw));
    if(fence)tangent=swat_v(cosf(source.yaw),0,-sinf(source.yaw));
    // Localized game-space aperture. It crosses both wall faces and the
    // intervening stud segments, leaving the rest of the assembly intact.
    b3Pos center=position;
    float aperture=.72f*swat_clamp(1-.25f*charge_demand(&source),.65f,1);
    int removed=0;
    for(int i=0;i<w->count;i++) {
        SwatObject* o=&w->objects[i];
        if(!swat_world_breachable(o) || o->door || o->wall_group!=source.wall_group) continue;
        b3Vec3 delta=b3SubPos(o->center,center);
        float z=fabsf(b3Dot(delta,tangent)),y=fabsf(delta.y);
        if(!fence && (o->part==SWAT_PART_SKIN || o->part==SWAT_PART_FRAME)) {
            bool coating=o->material==SWAT_DRYWALL || o->material==SWAT_PLASTER || o->material==SWAT_INSULATION;
            SwatCut cut={.y=-(float)delta.y,.z=-b3Dot(delta,tangent),.height=1.12f+(coating?.02f:0),.width=.92f+(coating?.03f:0),
                .seed=(unsigned int)source.wall_group,.kind=SWAT_CUT_BREACH};
            int changed=swat_world_cut(w,i,cut);if(changed>0)removed++;
        }
        else if((fence && z<aperture+o->half.x*.6f && y<1.10f+o->half.y*.45f) ||
                (!fence && z<aperture+o->half.z && y<1.1f+o->half.y))
            removed+=swat_world_damage(w,i,o->max_health);
    }
    // Collision divisions are not manufactured panels. Retained sheet material
    // stays connected across them; an AABB/stud overlap cannot decide whether
    // to delete a whole division outside the physical opening.
    support_end(w);return removed;
}

static float blast_transmission(const SwatWorld* w,b3Pos from,b3Pos to,int target) {
    int crossed[8],count=0;float transmission=1;b3Vec3 delta=b3SubPos(to,from);
    if(b3LengthSquared(delta)<1e-8f)return 1;
    // Evaluate exposure before changing neighbors. Thin layers absorb some
    // of the authored game impulse; substantial cover stops it completely.
    while(count<8) {
        SwatRayContext query={0};query.result.fraction=1;query.result.index=-1;
        query.world_only=true;query.layers=crossed;query.layer_count=count;
        b3World_CastRay(w->id,from,delta,b3DefaultQueryFilter(),swat_ray_callback,&query);
        if(!query.result.hit || swat_world_hit_layer(query.result)==target || query.result.fraction>.999f)return transmission;
        if(query.result.index<0)return 0;
        const SwatObject* cover=swat_world_hit_object(w,query.result);if(!cover)return 0;float through=0;
        switch(cover->material) {
        case SWAT_GLASS:case SWAT_OPAQUE_GLASS:through=.85f;break;
        case SWAT_DRYWALL:through=.55f;break;
        case SWAT_PLASTER:through=.45f;break;
        case SWAT_INSULATION:through=.9f;break;
        case SWAT_WOOD:through=cover->half.x<.03f?.25f:0;break;
        default:break;
        }
        transmission*=through;if(transmission<.08f)return 0;
        crossed[count++]=swat_world_hit_layer(query.result);
    }
    return 0;
}
static b3Pos blast_group_anchor(const SwatWorld* w,const SwatObject* target,b3Pos origin) {
    b3Quat q=swat_object_rotation(target);
    b3Vec3 lo=swat_v(1e30f,1e30f,1e30f),hi=swat_v(-1e30f,-1e30f,-1e30f);
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];
        if(!o->active || o->wall_group!=target->wall_group ||
           (o->part!=SWAT_PART_SKIN && o->part!=SWAT_PART_FRAME))continue;
        b3Quat r=swat_object_rotation(o);
        for(int v=0;v<8;v++) {
            b3Pos p=b3OffsetPos(o->center,b3RotateVector(r,swat_v((v&1?1:-1)*o->half.x,(v&2?1:-1)*o->half.y,(v&4?1:-1)*o->half.z)));
            b3Vec3 local=b3InvRotateVector(q,b3SubPos(p,target->center));
            lo=swat_v(fminf(lo.x,local.x),fminf(lo.y,local.y),fminf(lo.z,local.z));
            hi=swat_v(fmaxf(hi.x,local.x),fmaxf(hi.y,local.y),fmaxf(hi.z,local.z));
        }
    }
    b3Vec3 p=b3InvRotateVector(q,b3SubPos(origin,target->center));
    p=swat_v(swat_clamp(p.x,lo.x,hi.x),swat_clamp(p.y,lo.y,hi.y),swat_clamp(p.z,lo.z,hi.z));
    return b3OffsetPos(target->center,b3RotateVector(q,p));
}
int swat_world_blast(SwatWorld* w,int object,b3Pos position) {
    if(object<0 || object>=w->count || !swat_world_breachable(&w->objects[object]))return 0;
    support_begin(w);
    SwatObject source=w->objects[object];b3Quat rotation=swat_object_rotation(&source);
    b3Vec3 mount=b3InvRotateVector(rotation,b3SubPos(position,source.center));
    int affected=swat_world_breach(w,object,position);
    if(affected && source.door)door_motion_impulse(&w->objects[object],position,60);
    b3Pos origin=b3OffsetPos(position,b3RotateVector(rotation,swat_v(mount.x>=0?.035f:-.035f,0,0)));
    float strength[SWAT_MAX_OBJECTS]={0};b3Pos contacts[SWAT_MAX_OBJECTS];
    bool anchored[SWAT_MAX_OBJECTS]={0};b3Pos anchors[SWAT_MAX_OBJECTS];
    float range=source.door?1.2f:2.25f;
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];
        if(i==object || !o->active || !o->max_health ||
           (!source.door && source.wall_group>0 && o->wall_group==source.wall_group))continue;
        bool soft=o->material==SWAT_DRYWALL || o->material==SWAT_PLASTER || o->material==SWAT_INSULATION;
        bool wood=o->material==SWAT_WOOD,glass=o->material==SWAT_GLASS || o->material==SWAT_OPAQUE_GLASS;
        if(!soft && !wood && !glass)continue;
        b3Quat q=swat_object_rotation(o);b3Vec3 local=b3InvRotateVector(q,b3SubPos(origin,o->center));
        local=swat_v(swat_clamp(local.x,-o->half.x,o->half.x),swat_clamp(local.y,-o->half.y,o->half.y),swat_clamp(local.z,-o->half.z,o->half.z));
        contacts[i]=b3OffsetPos(o->center,b3RotateVector(q,local));
        float distance=b3Distance(origin,contacts[i]);if(distance>=range)continue;
        strength[i]=(1-distance/range)*blast_transmission(w,origin,contacts[i],i);
        // One contact per neighboring assembly avoids a repeated little hole
        // at every hidden cell boundary. Exposure still belongs to each piece.
        int group=o->wall_group-1;
        if(strength[i]>.08f && !o->door && (o->part==SWAT_PART_SKIN || o->part==SWAT_PART_FRAME) && group>=0 && group<w->count) {
            if(!anchored[group]){anchors[group]=blast_group_anchor(w,o,origin);anchored[group]=true;}
            contacts[i]=anchors[group];
        }
    }
    for(int i=0;i<w->count;i++)if(strength[i]>.08f) {
        SwatObject* o=&w->objects[i];if(!o->active)continue;
        bool wood=o->material==SWAT_WOOD;
        if(o->door || o->part==SWAT_PART_SKIN || o->part==SWAT_PART_FRAME) {
            if(o->material==SWAT_GLASS || o->material==SWAT_OPAQUE_GLASS) {
                affected+=swat_world_damage(w,i,120*strength[i]);continue;
            }
            b3Vec3 local=b3InvRotateVector(swat_object_rotation(o),b3SubPos(contacts[i],o->center));
            float scale=wood?.45f:1;
            SwatCut cut={.y=local.y,.z=local.z,.height=scale*(.12f+.38f*strength[i]),
                .width=scale*(.08f+.26f*strength[i]),.seed=(unsigned int)(object+1),.kind=SWAT_CUT_BREACH};
            affected+=swat_world_cut(w,i,cut)>0;
        } else affected+=swat_world_damage(w,i,(wood?55:120)*strength[i]);
    }
    support_end(w);
    if(w->rubble)for(int i=0;i<w->rubble->count;i++) {
        SwatRubble* r=&w->rubble->pieces[i];if(!r->object.active)continue;
        b3Pos point=b3Body_GetWorldCenter(r->object.body);b3Vec3 delta=b3SubPos(point,position);float distance=b3Length(delta);
        if(distance>=range)continue;
        float impulse=fminf(20,b3Body_GetMass(r->object.body)*1.5f)*(1-distance/range)*blast_transmission(w,origin,point,SWAT_MAX_OBJECTS+i);
        b3Body_ApplyLinearImpulse(r->object.body,swat_mul(swat_normalize(delta),impulse),point,true);
        r->state.velocity=b3Body_GetLinearVelocity(r->object.body);r->state.angular_velocity=b3Body_GetAngularVelocity(r->object.body);r->state.awake=b3Body_IsAwake(r->object.body);
    }
    return affected;
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
    b3Quat q=swat_object_rotation(o);
    b3Vec3 rel=b3InvRotateVector(q,b3SubPos(entry,o->center));
    b3Vec3 direction=b3InvRotateVector(q,d);
    if(o->cuts)return swat_fracture_exit(o->cuts,o->half.x,rel,direction);
    if(o->door && o->latch_broken) {
        // Union of retained bands. Adjacent internal planes do not consume a
        // new material layer; only the actual exterior closes this interval.
        float intervals[SWAT_DOOR_SECTIONS][2];int count=0;
        for(int s=0;s<SWAT_DOOR_SECTIONS;s++) {
            float vtx[4][2];swat_door_section(o,s,vtx);
            float lo=-1e6f,hi=1e6f;
            for(int p=0;p<6;p++) {
                float value,rate;
                if(p<2) {float sign=p?-1:1;value=o->half.x-sign*rel.x;rate=-sign*direction.x;}
                else {int i=p-2,j=(i+1)%4;float y=vtx[j][0]-vtx[i][0],z=vtx[j][1]-vtx[i][1];
                    value=y*(rel.z-vtx[i][1])-z*(rel.y-vtx[i][0]);rate=y*direction.z-z*direction.y;}
                if(fabsf(rate)<1e-7f) {if(value<-.00001f){hi=-1e6f;break;}}
                else if(rate>0)lo=fmaxf(lo,-value/rate);else hi=fminf(hi,-value/rate);
            }
            if(lo<=hi && hi>=-.00001f){intervals[count][0]=lo;intervals[count++][1]=hi;}
        }
        float end=0;bool found=false;
        for(int pass=0;pass<SWAT_DOOR_SECTIONS;pass++)for(int i=0;i<count;i++)
            if(intervals[i][0]<=end+.00001f && intervals[i][1]>=0) {
                end=fmaxf(end,intervals[i][1]);found=true;
            }
        return found && end<1e6f ? end : 0;
    }
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
        if(o->motion) {
            door_motion_refresh(w,o,false);
            if(o->motion->support!=SWAT_DOOR_FIXED) {
                o->motion->mount=b3InvRotateVector(swat_object_rotation(o),b3SubPos(o->breach_position,o->center));
                door_motion_drive(o);continue;
            }
        }
        float target = o->door_open ? (o->peek ? 12*SWAT_RAD : SWAT_PI*0.5f) : 0;
        if(o->wedge_owner>=0) target=0;
        float delta = swat_clamp(target-o->door_angle,-2.0f*SWAT_DT,2.0f*SWAT_DT);
        if (fabsf(delta) < 1e-7f) continue;
        if (swat_door_obstructed(w,o,o->door_angle+delta)) continue;
        b3Vec3 mount=b3InvRotateVector(swat_object_rotation(o),b3SubPos(o->breach_position,o->center));
        o->door_angle += delta; o->yaw = o->closed_yaw+o->door_angle;
        o->center = b3OffsetPos(o->hinge,swat_v(sinf(o->yaw)*o->half.z,0,cosf(o->yaw)*o->half.z));
        b3Quat rotation = {{0,sinf(o->yaw*0.5f),0},cosf(o->yaw*0.5f)};
        b3Body_SetTransform(o->body,o->center,rotation);
        if(o->motion){o->motion->state.position=o->center;o->motion->state.rotation=rotation;}
        if(o->breach_owner>=0)o->breach_position=b3OffsetPos(o->center,b3RotateVector(swat_object_rotation(o),mount));
    }
}

void swat_world_tilt(SwatObject* o,float pitch) {
    o->pitch=pitch;
    b3Quat q=b3MulQuat(b3MakeQuatFromAxisAngle(swat_v(0,1,0),o->yaw),b3MakeQuatFromAxisAngle(swat_v(0,0,1),pitch));
    b3Body_SetTransform(o->body,o->center,q);
}
