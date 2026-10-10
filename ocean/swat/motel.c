#include "motel.h"
#include <string.h>
#include "mission.h"
#include <assert.h>
#include <stdlib.h>
#include "motel_data.h"
#include "motel_utility_data.h"
#include "motel_fence_data.h"
#include "motel_surroundings_data.h"
#include "motel_ground_data.h"
#include "motel_mounted_data.h"
#include "motel_props_data.h"
#include "nightstand_data.h"
typedef struct SwatMotelFoliage {SwatMotelInstance placement;int supports[2];} SwatMotelFoliage;
#include "motel_foliage_data.h"
typedef struct SwatMotelContactSource {int asset;const int32_t* indices;int triangle_count;} SwatMotelContactSource;
#include "motel_contacts_data.h"
typedef struct SwatMotelPaneSource {int asset;b3Vec3 center,half;int triangles[44];} SwatMotelPaneSource;
#include "motel_panes_data.h"
static const int pane_parents[]={6,17,41,65,89};
// Small bevels must survive Box3D's minimum triangle area. Build this shell in
// centimetre units and apply the inverse shape scale; world dimensions stay exact.
static float mesh_units(int asset){return asset>=44?100:1;}
static void contact_meshes_create(SwatWorld* w) {
    int count=(int)(sizeof(motel_contact_sources)/sizeof(motel_contact_sources[0]));
    w->motel_contact_meshes=calloc((size_t)count,sizeof(*w->motel_contact_meshes));assert(w->motel_contact_meshes);
    w->motel_contact_mesh_count=count;
    bool* selected[SWAT_MOTEL_ASSETS]={0};
    for(int i=0;i<count;i++) {
        const SwatMotelContactSource* part=&motel_contact_sources[i];const SwatMotelAsset* source=swat_motel_asset(part->asset);
        assert(part->triangle_count<=240);b3Vec3 vertices[720];int32_t indices[720];
        float units=mesh_units(part->asset);
        if(part->asset>=46) {
            // Curved parts can exceed the nearby-triangle contact buffer. Keep
            // their exact triangles and original adjacency across subset seams.
            const b3MeshData* original=w->motel_meshes[part->asset];
            const b3Vec3* positions=b3GetMeshVertices(original);const b3MeshTriangle* triangles=b3GetMeshTriangles(original);
            if(!selected[part->asset]){selected[part->asset]=calloc((size_t)original->triangleCount,sizeof(bool));assert(selected[part->asset]);}
            int32_t selection[240];
            for(int t=0;t<part->triangle_count;t++) {
                int found=-1;
                for(int s=0;s<original->triangleCount;s++) {
                    if(selected[part->asset][s])continue;
                    int v[]={triangles[s].index1,triangles[s].index2,triangles[s].index3};bool same=true;
                    for(int k=0;k<3;k++)same&=b3LengthSquared(b3Sub(positions[v[k]],swat_mul(source->vertices[part->indices[3*t+k]],units)))<units*units*1e-12f;
                    if(same){found=s;break;}
                }
                assert(found>=0);selection[t]=found;selected[part->asset][found]=true;
            }
            w->motel_contact_meshes[i]=b3CreateMeshSubset(original,selection,part->triangle_count);assert(w->motel_contact_meshes[i]);
            continue;
        }
        for(int v=0;v<part->triangle_count*3;v++){vertices[v]=swat_mul(source->vertices[part->indices[v]],units);indices[v]=v;}
        b3MeshDef def={.vertices=vertices,.indices=indices,.vertexCount=part->triangle_count*3,.triangleCount=part->triangle_count,
            .weldVertices=true,.weldTolerance=units*1e-6f,.identifyEdges=true};
        w->motel_contact_meshes[i]=b3CreateMesh(&def,NULL,0);assert(w->motel_contact_meshes[i]);
    }
    for(int asset=0;asset<SWAT_MOTEL_ASSETS;asset++)if(selected[asset]) {
        for(int t=0;t<w->motel_meshes[asset]->triangleCount;t++)assert(selected[asset][t]);
        free(selected[asset]);
    }
}
static bool contact_meshes_bind(SwatWorld* w,SwatObject* o,const SwatMotelInstance* p,const b3ShapeDef* def) {
    bool found=false;
    b3Vec3 scale=swat_mul(p->scale,1/mesh_units(p->asset));
    for(int i=0;i<(int)(sizeof(motel_contact_sources)/sizeof(motel_contact_sources[0]));i++)if(motel_contact_sources[i].asset==p->asset) {
        b3ShapeId shape=b3CreateMeshShape(o->body,def,w->motel_contact_meshes[i],scale);assert(B3_IS_NON_NULL(shape));
        if(!found)o->shape=shape;
        found=true;
    }
    if(found)o->query_mesh=(b3Mesh){w->motel_meshes[p->asset],scale};
    return found;
}
const SwatMotelAsset* swat_motel_ground_asset(int part) {return part>=0 && part<SWAT_GROUND_PARTS?&ground_parts[part]:NULL;}
int swat_motel_ground_part(const SwatWorld* w,const SwatObject* o) {
    int part=o->tag.index-SWAT_MOTEL_GROUND_FIRST;
    return w->motel && w->count>=SWAT_MOTEL_MOUNTED_FIRST && part>=0 && part<SWAT_GROUND_PARTS?part:-1;
}
static bool ground_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_GROUND_FIRST)return true; // Older canonical prefix.
    if(w->count<SWAT_MOTEL_MOUNTED_FIRST)return false;
    for(int k=0;k<SWAT_GROUND_PARTS;k++) {
        const SwatObject* o=&w->objects[SWAT_MOTEL_GROUND_FIRST+k];const SwatMotelAsset* a=&ground_parts[k];
        b3Pos center={a->center.x,a->center.y,a->center.z};
        if(b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(a->half,o->half))>1e-4f || o->yaw!=0 || o->pitch!=0 ||
           o->door || o->fractured || o->wall_group || o->part!=SWAT_PART_SOLID || o->max_health!=0 || o->material!=ground_materials[k])return false;
    }
    return true;
}
static void ground_bind(SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_GROUND_FIRST)return;
    for(int k=0;k<SWAT_GROUND_PARTS;k++) {
        const SwatMotelAsset* a=&ground_parts[k];
        b3MeshDef mesh={.vertices=a->vertices,.indices=a->indices,.vertexCount=a->vertex_count,.triangleCount=a->triangle_count,
            .weldVertices=true,.weldTolerance=1e-6f,.identifyEdges=true};
        w->ground_meshes[k]=b3CreateMesh(&mesh,NULL,0);assert(w->ground_meshes[k]);
        SwatObject* o=&w->objects[SWAT_MOTEL_GROUND_FIRST+k];b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        o->shape=b3CreateMeshShape(o->body,&def,w->ground_meshes[k],swat_v(1,1,1));assert(B3_IS_NON_NULL(o->shape));
    }
}
const SwatMotelAsset* swat_motel_surroundings_asset(int part) {return part>=0 && part<SWAT_SURROUNDINGS_PARTS?&surroundings_parts[part]:NULL;}
int swat_motel_surroundings_part(const SwatWorld* w,const SwatObject* o) {
    int id=o->tag.index-SWAT_MOTEL_SURROUNDINGS_FIRST;
    return w->motel && w->count>=SWAT_MOTEL_SURROUNDINGS_FIRST+SWAT_MOTEL_SURROUNDINGS_COUNT && id>=0 && id<SWAT_MOTEL_SURROUNDINGS_COUNT ? id%SWAT_SURROUNDINGS_PARTS:-1;
}
bool swat_motel_surroundings_instance(int owner,SwatMotelInstance* out) {
    int id=owner-SWAT_MOTEL_SURROUNDINGS_FIRST;if(id<0 || id>=SWAT_MOTEL_SURROUNDINGS_COUNT)return false;
    *out=surroundings_placements[(id/SWAT_SURROUNDINGS_PARTS)*2+(id%SWAT_SURROUNDINGS_PARTS!=0)];return true;
}
int swat_motel_bank_triangle_part(int triangle) {return triangle>=136 && triangle<1176?surroundings_rock_triangles[triangle-136]:0;}
static void surroundings_recipe(int id,b3Pos* center,b3Vec3* half,float* yaw) {
    SwatMotelInstance p;bool found=swat_motel_surroundings_instance(id,&p);assert(found);(void)found;
    const SwatMotelAsset* a=&surroundings_parts[(id-SWAT_MOTEL_SURROUNDINGS_FIRST)%SWAT_SURROUNDINGS_PARTS];
    *center=b3OffsetPos(p.origin,swat_v(cosf(p.yaw)*a->center.x+sinf(p.yaw)*a->center.z,a->center.y,-sinf(p.yaw)*a->center.x+cosf(p.yaw)*a->center.z));
    *half=a->half;*yaw=p.yaw;
}
static bool surroundings_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_SURROUNDINGS_FIRST)return true; // Canonical legacy layout prefix.
    if(w->count<SWAT_MOTEL_SURROUNDINGS_FIRST+SWAT_MOTEL_SURROUNDINGS_COUNT)return false;
    for(int k=0;k<SWAT_MOTEL_SURROUNDINGS_COUNT;k++) {
        int id=SWAT_MOTEL_SURROUNDINGS_FIRST+k,part=k%SWAT_SURROUNDINGS_PARTS;
        const SwatObject* o=&w->objects[id];b3Pos center;b3Vec3 half;float yaw;surroundings_recipe(id,&center,&half,&yaw);
        if(b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(half,o->half))>1e-4f || fabsf(swat_angle(yaw-o->yaw))>1e-5f ||
            o->pitch!=0 || o->door || o->fractured || o->wall_group || o->part!=SWAT_PART_SOLID || o->max_health!=0 || o->material!=(part<2?SWAT_SOIL:SWAT_STONE))return false;
    }
    return true;
}
static void surroundings_bind(SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_SURROUNDINGS_FIRST)return;
    for(int part=0;part<SWAT_SURROUNDINGS_PARTS;part++) {
        const SwatMotelAsset* a=&surroundings_parts[part];
        b3MeshDef mesh={.vertices=a->vertices,.indices=a->indices,.vertexCount=a->vertex_count,.triangleCount=a->triangle_count,
            .weldVertices=true,.weldTolerance=1e-6f,.identifyEdges=true};
        w->surroundings_meshes[part]=b3CreateMesh(&mesh,NULL,0);assert(w->surroundings_meshes[part]);
    }
    for(int k=0;k<SWAT_MOTEL_SURROUNDINGS_COUNT;k++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_SURROUNDINGS_FIRST+k];b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        o->shape=b3CreateMeshShape(o->body,&def,w->surroundings_meshes[k%SWAT_SURROUNDINGS_PARTS],swat_v(1,1,1));assert(B3_IS_NON_NULL(o->shape));
    }
}
// Floor-level dressing stays outside the established door and capsule routes.
static const SwatMotelInstance utility_instances[]={
{40,{-4.55f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{-7.70f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false},
{40,{-0.55f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{-3.70f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false},
{40,{3.45f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{0.30f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false},
{40,{7.45f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{4.30f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false},
{42,{12.5f,-.08f,6},{1,1,1},SWAT_PI*.5f,SWAT_STEEL,false,false},
{42,{12.5f,-.08f,4},{1,1,1},SWAT_PI*.5f,SWAT_STEEL,false,false},
{42,{12.5f,-.08f,2},{1,1,1},SWAT_PI*.5f,SWAT_STEEL,false,false},
{43,{12.5f,-.08f,0},{1,1,1},SWAT_PI*.5f,SWAT_STEEL,false,false}
};
const SwatMotelAsset* swat_motel_asset(int index) { return index<0 || index>=SWAT_MOTEL_ASSETS?NULL:index<SWAT_MOTEL_BASE_ASSETS?&motel_assets[index]:index<42?&utility_assets[index-SWAT_MOTEL_BASE_ASSETS]:index<44?&fence_assets[index-42]:index==44?&mounted_assets[0]:index==49?&nightstand_asset:&prop_assets[index-45]; }
const SwatMotelInstance* swat_motel_instance(int index) { return index<0 || index>=SWAT_MOTEL_INSTANCES?NULL:index<SWAT_MOTEL_BASE_INSTANCES?&motel_instances[index]:&utility_instances[index-SWAT_MOTEL_BASE_INSTANCES]; }
static void recipe(int i,b3Pos* center,b3Vec3* half,float* yaw) {
    const SwatMotelInstance* p=swat_motel_instance(i); const SwatMotelAsset* a=swat_motel_asset(p->asset);
    if(p->door) {
        *half=swat_v(.025f,1.095f,.54f); *yaw=p->yaw+SWAT_PI*.5f;
        *center=b3OffsetPos(p->origin,swat_v(.54f*cosf(p->yaw),1.095f,-.54f*sinf(p->yaw))); return;
    }
    b3Vec3 local=swat_v(a->center.x*p->scale.x,a->center.y*p->scale.y,a->center.z*p->scale.z);
    float c=cosf(p->yaw),s=sinf(p->yaw);
    *center=b3OffsetPos(p->origin,swat_v(c*local.x+s*local.z,local.y,-s*local.x+c*local.z));
    *half=swat_v(fmaxf(.001f,a->half.x*p->scale.x),fmaxf(.001f,a->half.y*p->scale.y),fmaxf(.001f,a->half.z*p->scale.z)); *yaw=p->yaw;
}
int swat_motel_fence_part_count(void){return (int)(sizeof(fence_parts)/sizeof(fence_parts[0]));}
int swat_motel_fence_triangle_part(int triangle){return triangle>=0 && triangle<4188?fence_triangle_parts[triangle]:-1;}
bool swat_motel_fence_proxy(const SwatWorld* w,const SwatObject* o) {
    int first=o->wall_group-1;
    return w->motel && o->tag.index>=155 && o->tag.index<=157 && first>SWAT_MOTEL_INSTANCES &&
        first+swat_motel_fence_part_count()<=w->count && w->objects[first].part==SWAT_PART_FENCE_POST;
}
int swat_motel_fence_parent(const SwatWorld* w,const SwatObject* o) {
    if(!w->motel || o->part<SWAT_PART_FENCE_WIRE || o->part>SWAT_PART_FENCE_POST)return -1;
    for(int i=155;i<=157 && i<w->count;i++)if(w->objects[i].wall_group==o->wall_group && swat_motel_fence_proxy(w,&w->objects[i]))return i;
    return -1;
}
static b3Pos fence_center(int owner,int part) {
    const SwatMotelInstance* p=swat_motel_instance(owner-1);b3Vec3 c=fence_parts[part].center;
    return b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*c.x+sinf(p->yaw)*c.z,c.y,-sinf(p->yaw)*c.x+cosf(p->yaw)*c.z));
}
static float fence_health(int part){return fence_parts[part].part==SWAT_PART_FENCE_POST?0:swat_material(SWAT_STEEL)->fracture_health;}
static bool fence_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_INSTANCES+1)return true; // Explicit legacy prefix has no piece records.
    for(int owner=155;owner<=157 && owner<w->count;owner++) {
        int first=w->objects[owner].wall_group-1;if(first<0)continue;
        if(first<=SWAT_MOTEL_INSTANCES || first+swat_motel_fence_part_count()>w->count)return false;
        for(int k=0;k<swat_motel_fence_part_count();k++) {
            const SwatObject* o=&w->objects[first+k];
            if(b3Distance(o->center,fence_center(owner,k))>1e-4f || b3Length(b3Sub(o->half,fence_parts[k].half))>1e-4f ||
               fabsf(swat_angle(o->yaw-SWAT_PI*.5f))>1e-5f || o->pitch!=0 || o->material!=SWAT_STEEL || o->door ||
               o->part!=fence_parts[k].part || o->max_health!=fence_health(k) || o->wall_group!=first+1)return false;
        }
    }
    return true;
}
static void fence_bind(SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_INSTANCES+1)return;
    const SwatMotelAsset* source=&fence_assets[0];
    for(int owner=155;owner<=157 && owner<w->count;owner++) {
        int first=w->objects[owner].wall_group-1;if(first<0)continue;
        for(int k=0;k<swat_motel_fence_part_count();k++) {
            if(!w->fence_meshes[k]) {
                b3Vec3 vertices[720];int32_t indices[720];int n=0;
                for(int t=0;t<source->triangle_count;t++)if(fence_triangle_parts[t]==k)for(int j=0;j<3;j++) {
                    vertices[n]=b3Sub(b3Add(source->vertices[source->indices[t*3+j]],source->center),fence_parts[k].center);
                    indices[n]=n;n++;
                }
                assert(n==fence_parts[k].triangles*3 && n<=720);
                b3MeshDef def={.vertices=vertices,.indices=indices,.vertexCount=n,.triangleCount=n/3,.weldVertices=true,.weldTolerance=1e-6f,.identifyEdges=true};
                w->fence_meshes[k]=b3CreateMesh(&def,NULL,0);assert(w->fence_meshes[k]);
            }
            SwatObject* o=&w->objects[first+k];b3DestroyShape(o->shape,false);
            b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
            o->shape=b3CreateMeshShape(o->body,&def,w->fence_meshes[k],swat_v(1,1,1));assert(B3_IS_NON_NULL(o->shape));
        }
        SwatObject* proxy=&w->objects[owner];
        if(B3_IS_NON_NULL(proxy->body))b3DestroyBody(proxy->body);
        proxy->body=b3_nullBodyId;proxy->shape=b3_nullShapeId;proxy->active=false;
    }
}
static const SwatMotelInstance mounted_instances[]={
    {44,{-11.50f,1.30f,-6.091f},{1,1,1},SWAT_PI,SWAT_STEEL,false,false},
    {44,{7.90f,1.30f,.091f},{1,1,1},0,SWAT_STEEL,false,false}
};
static const int mounted_parents[]={7,85};
static const b3Vec3 mounted_anchors[]={{0,0,0},{-.022f,.26f,0},{.022f,.26f,0},{-.022f,-.16f,0},{.022f,-.16f,0}};
static float mounted_health(void){return swat_material(SWAT_STEEL)->fracture_health*.0012f/swat_material(SWAT_STEEL)->reference_thickness;}
static void mounted_recipe(int index,b3Pos* center,b3Vec3* half) {
    const SwatMotelInstance* p=&mounted_instances[index];const SwatMotelAsset* a=swat_motel_asset(p->asset);
    *center=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*a->center.x+sinf(p->yaw)*a->center.z,a->center.y,-sinf(p->yaw)*a->center.x+cosf(p->yaw)*a->center.z));*half=a->half;
}
// Resolve every authored fastening against the canonical wall polygons, including
// already destroyed pieces: a loaded damaged map must not pick a new support.
static bool wall_supports(const SwatWorld* w,const SwatMotelInstance* p,int parent,const b3Vec3* anchors,int anchor_count,int supports[SWAT_MAX_SUPPORTS]) {
    int group=w->objects[parent].wall_group,n=0;
    for(int i=0;i<SWAT_MAX_SUPPORTS;i++)supports[i]=-1;
    if(!group)return false;
    for(int anchor=0;anchor<anchor_count;anchor++) {
        b3Vec3 a=anchors[anchor];b3Pos point=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*a.x+sinf(p->yaw)*a.z,a.y,-sinf(p->yaw)*a.x+cosf(p->yaw)*a.z));int found=-1;
        for(int i=group-1;i<SWAT_MOTEL_MOUNTED_FIRST && i<w->count && w->objects[i].wall_group==group;i++) {
            const SwatObject* o=&w->objects[i];if(o->part!=SWAT_PART_SKIN)continue;
            b3Vec3 d=b3SubPos(point,o->center);float c=cosf(o->yaw),s=sinf(o->yaw),x=c*d.x-s*d.z,y=d.y,z=s*d.x+c*d.z;
            if(fabsf(fabsf(x)-o->half.x)>.002f || fabsf(y)>o->half.y || fabsf(z)>o->half.z)continue;
            bool inside=true;
            if(o->fractured)for(int j=0;j<4;j++){int k=(j+1)%4;float ay=o->corners[j][0],az=o->corners[j][1];if((o->corners[k][0]-ay)*(z-az)-(o->corners[k][1]-az)*(y-ay)<-1e-5f)inside=false;}
            if(inside){found=i;break;}
        }
        if(found<0)return false;
        bool duplicate=false;for(int i=0;i<n;i++)duplicate|=supports[i]==found;
        if(!duplicate)supports[n++]=found;
    }
    return true;
}
static bool mounted_supports(const SwatWorld* w,int index,int supports[SWAT_MAX_SUPPORTS]) {
    return wall_supports(w,&mounted_instances[index],mounted_parents[index],mounted_anchors,SWAT_MAX_SUPPORTS,supports);
}
bool swat_motel_mounted(const SwatWorld* w,int owner,SwatMotelInstance* out) {
    int index=owner-SWAT_MOTEL_MOUNTED_FIRST;
    if(!w->motel || w->count<SWAT_MOTEL_PROPS_FIRST || index<0 || index>=SWAT_MOTEL_MOUNTED_INSTANCES)return false;
    *out=mounted_instances[index];return true;
}
static bool mounted_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_MOUNTED_FIRST)return true;
    if(w->count<SWAT_MOTEL_PROPS_FIRST)return false;
    for(int i=0;i<SWAT_MOTEL_MOUNTED_INSTANCES;i++) {
        b3Pos center;b3Vec3 half;int supports[SWAT_MAX_SUPPORTS];mounted_recipe(i,&center,&half);
        const SwatObject* o=&w->objects[SWAT_MOTEL_MOUNTED_FIRST+i];
        if(!mounted_supports(w,i,supports) || memcmp(supports,o->supports,sizeof(supports)) ||
           b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(half,o->half))>1e-4f || fabsf(swat_angle(o->yaw-mounted_instances[i].yaw))>1e-5f ||
           o->pitch!=0 || o->door || o->fractured || o->wall_group || o->part!=SWAT_PART_FIXTURE || o->material!=SWAT_STEEL || o->max_health!=mounted_health() || o->structural_thickness!=.0012f)return false;
    }
    return true;
}
static void mounted_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_PROPS_FIRST)return;
    for(int i=0;i<SWAT_MOTEL_MOUNTED_INSTANCES;i++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_MOUNTED_FIRST+i];b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        bool bound=contact_meshes_bind(w,o,&mounted_instances[i],&def);assert(bound);(void)bound;
    }
}
static const SwatMotelInstance prop_instances[]={
    {45,{-8.75f,.0055f,-9.35f},{1,1,1},0,SWAT_STEEL,false,false},
    {46,{4.20f,1.12f,.224f},{1,1,1},0,SWAT_STEEL,false,false},
    {47,{-11.91f,1.65f,-2.60f},{1,1,1},SWAT_PI*.5f,SWAT_WOOD,false,false},
    {48,{-10.70f,.0055f,-4.70f},{1,1,1},0,SWAT_WOOD,false,false}
};
static const float prop_thickness[]={.008f,.012f,.032f,.042f};
static float prop_health(int index){const SwatMaterialDef* material=swat_material(prop_instances[index].material);return material->fracture_health*prop_thickness[index]/material->reference_thickness;}
static void prop_recipe(int index,b3Pos* center,b3Vec3* half) {
    const SwatMotelInstance* p=&prop_instances[index];const SwatMotelAsset* a=swat_motel_asset(p->asset);
    *center=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*a->center.x+sinf(p->yaw)*a->center.z,a->center.y,-sinf(p->yaw)*a->center.x+cosf(p->yaw)*a->center.z));*half=a->half;
}
static bool floor_supports(const SwatWorld* w,const SwatMotelInstance* p,int floor_owner,const b3Vec3* anchors,int supports[SWAT_MAX_SUPPORTS]) {
    for(int i=0;i<SWAT_MAX_SUPPORTS;i++)supports[i]=-1;
    // Resolve all measured contacts against original floor triangles, rather
    // than the bounds (the perimeter trim is 1 mm above this floor surface).
    // This also works before collider creation and after support loss.
    const SwatObject* floor=&w->objects[floor_owner];
    const SwatMotelInstance* base=swat_motel_instance(floor_owner-1);const SwatMotelAsset* source=swat_motel_asset(base->asset);
    for(int i=0;i<4;i++) {
        b3Vec3 a=anchors[i];b3Pos point=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*a.x+sinf(p->yaw)*a.z,a.y,-sinf(p->yaw)*a.x+cosf(p->yaw)*a.z));
        if(fabsf(point.x-floor->center.x)>floor->half.x || fabsf(point.z-floor->center.z)>floor->half.z)return false;
        bool supported=false;
        for(int t=0;t<source->triangle_count && !supported;t++) {
            b3Vec3 v[3];
            for(int k=0;k<3;k++) {
                b3Vec3 a=b3Add(source->vertices[source->indices[3*t+k]],source->center);
                a=swat_v(a.x*base->scale.x,a.y*base->scale.y,a.z*base->scale.z);
                v[k]=swat_v(base->origin.x+cosf(base->yaw)*a.x+sinf(base->yaw)*a.z,base->origin.y+a.y,base->origin.z-sinf(base->yaw)*a.x+cosf(base->yaw)*a.z);
            }
            float ax=v[1].x-v[0].x,az=v[1].z-v[0].z,bx=v[2].x-v[0].x,bz=v[2].z-v[0].z,det=ax*bz-az*bx;
            if(fabsf(det)<1e-8f)continue;
            float dx=point.x-v[0].x,dz=point.z-v[0].z,u=(dx*bz-dz*bx)/det,vv=(ax*dz-az*dx)/det;
            supported=u>=-1e-5f && vv>=-1e-5f && u+vv<=1.00001f && fabsf(point.y-(v[0].y+u*(v[1].y-v[0].y)+vv*(v[2].y-v[0].y)))<.0001f;
        }
        if(!supported)return false;
    }
    supports[0]=floor_owner;return true;
}
static bool prop_supports(const SwatWorld* w,int index,int supports[SWAT_MAX_SUPPORTS]) {
    const SwatMotelInstance* p=&prop_instances[index];
    if(index==1)return wall_supports(w,p,85,prop_extinguisher_anchors,2,supports);
    if(index==2)return wall_supports(w,p,104,prop_noticeboard_anchors,2,supports);
    return floor_supports(w,p,index==3?2:127,index==3?prop_chair_anchors:prop_wheel_anchors,supports);
}
static const SwatMotelInstance nightstand_instance={49,{7.62f,.004f,-3.15f},{1,1,1},0,SWAT_WOOD,false,false};
static float nightstand_health(void){const SwatMaterialDef* m=swat_material(SWAT_WOOD);return m->fracture_health*.024f/m->reference_thickness;}
static void nightstand_recipe(b3Pos* center,b3Vec3* half){*center=b3OffsetPos(nightstand_instance.origin,nightstand_asset.center);*half=nightstand_asset.half;}
static bool nightstand_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_NIGHTSTAND_FIRST)return true;
    if(w->count!=SWAT_MOTEL_OBJECTS)return false;
    const SwatObject* o=&w->objects[SWAT_MOTEL_NIGHTSTAND_FIRST];b3Pos center;b3Vec3 half;int supports[SWAT_MAX_SUPPORTS];nightstand_recipe(&center,&half);
    return floor_supports(w,&nightstand_instance,81,nightstand_feet,supports) && !memcmp(o->supports,supports,sizeof(supports)) &&
        b3Distance(center,o->center)<1e-4f && b3Length(b3Sub(half,o->half))<1e-4f && o->yaw==0 && o->pitch==0 &&
        !o->door && !o->fractured && !o->wall_group && o->part==SWAT_PART_FIXTURE && o->material==SWAT_WOOD &&
        o->max_health==nightstand_health() && o->structural_thickness==.024f;
}
static void nightstand_bind(SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_NIGHTSTAND_FIRST)return;
    SwatObject* o=&w->objects[SWAT_MOTEL_NIGHTSTAND_FIRST];if(!o->active)return;
    b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
    bool bound=contact_meshes_bind(w,o,&nightstand_instance,&def);assert(bound);(void)bound;
}
bool swat_motel_prop(const SwatWorld* w,int owner,SwatMotelInstance* out) {
    if(w->motel && owner==SWAT_MOTEL_NIGHTSTAND_FIRST && owner<w->count){*out=nightstand_instance;return true;}
    int index=owner-SWAT_MOTEL_PROPS_FIRST;
    if(!w->motel || owner>=w->count || index<0 || index>=SWAT_MOTEL_PROP_INSTANCES)return false;
    *out=prop_instances[index];return true;
}
static bool props_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_PROPS_FIRST)return true; // Existing version-17 maps keep their prefix.
    if(w->count>SWAT_MOTEL_OBJECTS)return false;
    for(int i=0;i<SWAT_MOTEL_PROP_INSTANCES && SWAT_MOTEL_PROPS_FIRST+i<w->count;i++) {
        b3Pos center;b3Vec3 half;int supports[SWAT_MAX_SUPPORTS];prop_recipe(i,&center,&half);
        const SwatObject* o=&w->objects[SWAT_MOTEL_PROPS_FIRST+i];
        if(!prop_supports(w,i,supports) || memcmp(supports,o->supports,sizeof(supports)) ||
           b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(half,o->half))>1e-4f || fabsf(swat_angle(o->yaw-prop_instances[i].yaw))>1e-5f ||
           o->pitch!=0 || o->door || o->fractured || o->wall_group || o->part!=SWAT_PART_FIXTURE || o->material!=prop_instances[i].material || o->max_health!=prop_health(i) || o->structural_thickness!=prop_thickness[i])return false;
    }
    return true;
}
static void props_bind(SwatWorld* w) {
    for(int i=0;i<SWAT_MOTEL_PROP_INSTANCES && SWAT_MOTEL_PROPS_FIRST+i<w->count;i++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_PROPS_FIRST+i];b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        bool bound=contact_meshes_bind(w,o,&prop_instances[i],&def);assert(bound);(void)bound;
    }
}
int swat_motel_window_index(int parent) {
    for(int i=0;i<5;i++)if(pane_parents[i]==parent)return i;
    return -1;
}
int swat_motel_pane_parent(const SwatWorld* w,const SwatObject* o) {
    int i=o->tag.index-SWAT_MOTEL_PANES_FIRST;
    return w->motel && w->count>=SWAT_MOTEL_NIGHTSTAND_FIRST && i>=0 && i<10?pane_parents[i/2]:-1;
}
int swat_motel_pane_mask(const SwatWorld* w,int parent) {
    int i=swat_motel_window_index(parent);
    if(i<0 || w->count<SWAT_MOTEL_NIGHTSTAND_FIRST)return 3;
    return w->objects[SWAT_MOTEL_PANES_FIRST+2*i].active | (w->objects[SWAT_MOTEL_PANES_FIRST+2*i+1].active<<1);
}
static int source_pane(int asset,int triangle) {
    for(int i=0;i<4;i++)if(pane_sources[i].asset==asset)
        for(int t=0;t<44;t++)if(pane_sources[i].triangles[t]==triangle)return i;
    return -1;
}
int swat_motel_pane_triangle(int asset,b3Vec3 a,b3Vec3 b,b3Vec3 c) {
    const SwatMotelAsset* source=swat_motel_asset(asset);b3Vec3 v[]={a,b,c};
    for(int i=0;i<4;i++)if(pane_sources[i].asset==asset)for(int t=0;t<44;t++) {
        int tri=pane_sources[i].triangles[t];
        for(int shift=0;shift<3;shift++) {
            bool same=true;
            for(int k=0;k<3;k++)same&=b3LengthSquared(b3Sub(v[(k+shift)%3],b3Add(source->vertices[source->indices[3*tri+k]],source->center)))<4e-12f;
            if(same)return i%2;
        }
    }
    return -1;
}
static const SwatMotelPaneSource* pane_source(int i) {return &pane_sources[(i<2?2:0)+i%2];}
static float pane_health(int i) {
    const SwatMaterialDef* m=swat_material(SWAT_OPAQUE_GLASS);
    return m->fracture_health*2*pane_source(i)->half.z/m->reference_thickness;
}
static void pane_recipe(int i,b3Pos* center,b3Vec3* half,float* yaw) {
    const SwatMotelPaneSource* s=pane_source(i);const SwatMotelInstance* p=swat_motel_instance(pane_parents[i/2]-1);
    assert(p->asset==s->asset && p->scale.x==1 && p->scale.y==1 && p->scale.z==1);
    *center=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*s->center.x+sinf(p->yaw)*s->center.z,s->center.y,-sinf(p->yaw)*s->center.x+cosf(p->yaw)*s->center.z));
    *half=swat_v(s->half.z,s->half.y,s->half.x);*yaw=p->yaw+SWAT_PI*.5f;
}
static bool panes_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_PANES_FIRST)return true;
    if(w->count<SWAT_MOTEL_NIGHTSTAND_FIRST || w->count>SWAT_MOTEL_OBJECTS)return false;
    for(int i=0;i<10;i++) {
        const SwatObject* o=&w->objects[SWAT_MOTEL_PANES_FIRST+i];b3Pos center;b3Vec3 half;float yaw;pane_recipe(i,&center,&half,&yaw);
        int supports[SWAT_MAX_SUPPORTS];for(int j=0;j<SWAT_MAX_SUPPORTS;j++)supports[j]=j?-1:pane_parents[i/2];
        if(b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(half,o->half))>1e-4f || fabsf(swat_angle(o->yaw-yaw))>1e-5f ||
           o->pitch!=0 || o->door || o->fractured || o->wall_group!=SWAT_MOTEL_PANES_FIRST+2*(i/2)+1 ||
           o->part!=SWAT_PART_FIXTURE || o->material!=SWAT_OPAQUE_GLASS || o->max_health!=pane_health(i) ||
           o->structural_thickness!=2*pane_source(i)->half.z || memcmp(supports,o->supports,sizeof(supports)))return false;
    }
    return true;
}
static int owned_mesh(SwatWorld* w,b3MeshData* mesh) {
    assert(mesh);int id=w->motel_contact_mesh_count++;
    w->motel_contact_meshes=realloc(w->motel_contact_meshes,(size_t)w->motel_contact_mesh_count*sizeof(*w->motel_contact_meshes));
    assert(w->motel_contact_meshes);w->motel_contact_meshes[id]=mesh;return id;
}
static b3MeshData* window_mesh(int source_pane_id,int asset) {
    const SwatMotelAsset* a=swat_motel_asset(asset);b3Vec3* v=malloc((size_t)a->triangle_count*3*sizeof(*v));
    int32_t* indices=malloc((size_t)a->triangle_count*3*sizeof(*indices));assert(v && indices);int count=0;
    for(int t=0;t<a->triangle_count;t++) {
        int part=source_pane(asset,t);
        if(source_pane_id<0?part>=0:part!=source_pane_id)continue;
        for(int k=0;k<3;k++) {
            b3Vec3 p=a->vertices[a->indices[3*t+k]];
            if(source_pane_id>=0){p=b3Sub(b3Add(p,a->center),pane_sources[source_pane_id].center);p=swat_v(-p.z,p.y,p.x);}
            v[3*count+k]=swat_mul(p,100);indices[3*count+k]=3*count+k;
        }
        count++;
    }
    assert(count==(source_pane_id<0?a->triangle_count-88:44));
    b3MeshDef def={.vertices=v,.indices=indices,.vertexCount=count*3,.triangleCount=count,.weldVertices=true,.weldTolerance=.0001f,.identifyEdges=true};
    b3MeshData* mesh=b3CreateMesh(&def,NULL,0);free(v);free(indices);return mesh;
}
static void panes_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_NIGHTSTAND_FIRST)return;
    for(int kind=0;kind<2;kind++) {
        int asset=kind?10:12;int full=owned_mesh(w,window_mesh(-1,asset));
        b3MeshData* mesh=w->motel_contact_meshes[full];int first=w->motel_contact_mesh_count;
        for(int start=0;start<mesh->triangleCount;start+=240) {
            int selection[240],count=mesh->triangleCount-start;if(count>240)count=240;
            for(int k=0;k<count;k++)selection[k]=start+k;
            owned_mesh(w,b3CreateMeshSubset(mesh,selection,count));
        }
        int end=w->motel_contact_mesh_count;
        for(int bay=kind?1:0;bay<(kind?5:1);bay++) {
            SwatObject* o=&w->objects[pane_parents[bay]];if(!o->active)continue;
            int count=b3Body_GetShapeCount(o->body);b3ShapeId* shapes=malloc((size_t)count*sizeof(*shapes));assert(shapes);
            assert(b3Body_GetShapes(o->body,shapes,count)==count);
            for(int j=0;j<count;j++)b3DestroyShape(shapes[j],false);
            free(shapes);
            b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
            for(int j=first;j<end;j++) {
                b3ShapeId shape=b3CreateMeshShape(o->body,&def,w->motel_contact_meshes[j],swat_v(.01f,.01f,.01f));assert(B3_IS_NON_NULL(shape));
                if(j==first)o->shape=shape;
            }
            o->query_mesh=(b3Mesh){mesh,{.01f,.01f,.01f}};
        }
    }
    for(int source=0;source<4;source++) {
        int id=owned_mesh(w,window_mesh(source,pane_sources[source].asset));
        for(int i=0;i<10;i++)if(pane_source(i)==&pane_sources[source]) {
            SwatObject* o=&w->objects[SWAT_MOTEL_PANES_FIRST+i];if(!o->active)continue;
            b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
            o->shape=b3CreateMeshShape(o->body,&def,w->motel_contact_meshes[id],swat_v(.01f,.01f,.01f));assert(B3_IS_NON_NULL(o->shape));
        }
    }
}
bool swat_motel_bind_collision(SwatWorld* w) {
    // Canonical prefix remains stable; appended wall fragments use their explicit
    // wire geometry. Also retain the original map without utility props.
    if(w->count<SWAT_MOTEL_INSTANCES+1 && w->count!=SWAT_MOTEL_BASE_INSTANCES+1 && w->count!=SWAT_MOTEL_UTILITY_INSTANCES+1) return false;
    int instances=w->count==SWAT_MOTEL_BASE_INSTANCES+1?SWAT_MOTEL_BASE_INSTANCES:w->count==SWAT_MOTEL_UTILITY_INSTANCES+1?SWAT_MOTEL_UTILITY_INSTANCES:SWAT_MOTEL_INSTANCES;
    if(w->motel) return true;
    if(!fence_validate(w) || !surroundings_validate(w) || !ground_validate(w) || !mounted_validate(w) || !props_validate(w) || !panes_validate(w) || !nightstand_validate(w))return false;
    for(int i=0;i<instances;i++) {
        b3Pos center; b3Vec3 half; float yaw; recipe(i,&center,&half,&yaw); const SwatObject* o=&w->objects[i+1];
        const SwatMotelInstance* p=swat_motel_instance(i);
        if(p->door) {
            b3Pos hinge={p->origin.x,p->origin.y+1.095f,p->origin.z};
            if(b3Distance(hinge,o->hinge)>1e-4f || fabsf(swat_angle(yaw-o->closed_yaw))>1e-5f) return false;
            // Checkpoint/maps can be captured while a door is already open.
            // Validate its canonical hinge and current transform, not rest yaw.
            center=b3OffsetPos(hinge,swat_v(sinf(o->yaw)*half.z,0,cosf(o->yaw)*half.z)); yaw=o->yaw;
        }
        if(b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(half,o->half))>1e-4f || fabsf(swat_angle(yaw-o->yaw))>1e-5f || o->pitch!=0 ||
            o->door!=p->door || o->material!=p->material) return false;
    }
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) {
        const SwatMotelAsset* a=swat_motel_asset(i);
        float units=mesh_units(i);b3Vec3* scaled=NULL;
        if(units!=1){scaled=malloc((size_t)a->vertex_count*sizeof(*scaled));assert(scaled);for(int v=0;v<a->vertex_count;v++)scaled[v]=swat_mul(a->vertices[v],units);}
        b3MeshDef def={.vertices=scaled?scaled:a->vertices,.indices=a->indices,.vertexCount=a->vertex_count,.triangleCount=a->triangle_count,
            .weldVertices=true,.weldTolerance=units*1e-6f,.identifyEdges=true};
        w->motel_meshes[i]=b3CreateMesh(&def,NULL,0); assert(w->motel_meshes[i]);
        free(scaled);
    }
    contact_meshes_create(w);
    for(int i=0;i<instances;i++) {
        const SwatMotelInstance* p=swat_motel_instance(i); if(p->door) continue;
        SwatObject* o=&w->objects[i+1]; b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef(); def.baseMaterial=swat_physics_material(o->material);
        if(!contact_meshes_bind(w,o,p,&def))o->shape=b3CreateMeshShape(o->body,&def,w->motel_meshes[p->asset],p->scale);
        assert(B3_IS_NON_NULL(o->shape));
    }
    // New maps replace these whole walls with authored layers. Legacy maps
    // without those layers keep their original solid wall, even if truncated.
    for(int i=1;i<=instances;i++) {
        SwatObject* o=&w->objects[i]; int first=o->wall_group-1;
        if(first<=SWAT_MOTEL_INSTANCES || first>=w->count || w->objects[first].wall_group!=o->wall_group)continue;
        b3DestroyBody(o->body);o->body=b3_nullBodyId;o->shape=b3_nullShapeId;o->active=false;
    }
    fence_bind(w);
    surroundings_bind(w);
    ground_bind(w);
    mounted_bind(w);
    props_bind(w);
    nightstand_bind(w);
    panes_bind(w);
    w->motel=true; return true;
}

static bool sectioned_asset(int asset) {return asset==0 || asset==1 || asset==2 || asset==21 || asset==22;}
int swat_motel_wall_parent(const SwatWorld* w,const SwatObject* piece) {
    if(!w->motel || piece->tag.index<=SWAT_MOTEL_INSTANCES || !piece->wall_group)return -1;
    for(int i=1;i<=SWAT_MOTEL_INSTANCES && i<w->count;i++) {
        const SwatObject* parent=&w->objects[i];
        if(i>=106 && i<=108)continue; // Existing timber party walls use their own skins.
        if(!parent->active && parent->wall_group==piece->wall_group && sectioned_asset(swat_motel_instance(i-1)->asset))return i;
    }
    return -1;
}
static void wall_corners(const SwatObject* o,float v[4][2]) {
    if(o->fractured){memcpy(v,o->corners,sizeof(o->corners));return;}
    const int sign[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
    for(int i=0;i<4;i++){v[i][0]=sign[i][0]*o->half.y;v[i][1]=sign[i][1]*o->half.z;}
}
int swat_motel_wall_edges(const SwatWorld* w,const SwatObject* o,SwatMotelEdge* edges,int capacity) {
    if(!o->active || o->material!=SWAT_BRICK || swat_motel_wall_parent(w,o)<0)return 0;
    int count=0;float c=cosf(o->yaw),s=sinf(o->yaw),v[4][2];wall_corners(o,v);
    for(int i=o->wall_group-1;i<w->count && w->objects[i].wall_group==o->wall_group;i++) {
        const SwatObject* n=&w->objects[i];if(n->active || n->part!=SWAT_PART_SKIN)continue;
        b3Vec3 d=b3SubPos(n->center,o->center);float ny=d.y,nz=s*d.x+c*d.z,u[4][2];wall_corners(n,u);
        for(int e=0;e<4;e++) {
            int k=(e+1)%4;float dy=v[k][0]-v[e][0],dz=v[k][1]-v[e][1],length=hypotf(dy,dz);
            if(length<.0001f)continue;
            float ty=dy/length,tz=dz/length;
            for(int f=0;f<4;f++) {
                int g=(f+1)%4;float ay=ny+u[f][0]-v[e][0],az=nz+u[f][1]-v[e][1];
                float by=ny+u[g][0]-v[e][0],bz=nz+u[g][1]-v[e][1];
                if(fabsf(ty*az-tz*ay)>.0001f || fabsf(ty*bz-tz*by)>.0001f)continue;
                float a=ty*ay+tz*az,b=ty*by+tz*bz;
                if(b>=a)continue; // Neighbor's shared boundary runs the other way.
                float lo=fmaxf(0,b),hi=fminf(length,a);if(hi-lo<.0001f)continue;
                float mid=(lo+hi)*.5f,ly=v[e][0]+ty*mid,lz=v[e][1]+tz*mid;
                if(count>=capacity)return count;
                // Strip +Y points into this survivor; +Z follows the reverse edge.
                edges[count++]=(SwatMotelEdge){o->tag.index,i,b3OffsetPos(o->center,swat_v(s*lz,ly,c*lz)),o->yaw,atan2f(ty,-tz),hi-lo,2*o->half.x};
            }
        }
    }
    return count;
}
static int float_order(const void* a,const void* b) {float x=*(const float*)a,y=*(const float*)b;return (x>y)-(x<y);}
static void wall_cut(float* cuts,int* count,float v) {
    // Source bevels are 3 mm. Snap their paired edges to the structural datum.
    v=roundf(v*100)*.01f;
    for(int i=0;i<*count;i++)if(fabsf(cuts[i]-v)<.001f)return;
    assert(*count<64);cuts[(*count)++]=v;
}
static bool wall_face(const SwatMotelAsset* a,int tri,float depth,b3Vec3 v[3]) {
    for(int k=0;k<3;k++) {v[k]=b3Add(a->vertices[a->indices[3*tri+k]],a->center);if(fabsf(v[k].z-depth)>.0001f)return false;}
    return fabsf((v[1].x-v[0].x)*(v[2].y-v[0].y)-(v[1].y-v[0].y)*(v[2].x-v[0].x))>1e-5f;
}
static bool wall_occupied(const SwatMotelAsset* a,float depth,float x,float y) {
    for(int t=0;t<a->triangle_count;t++) {
        b3Vec3 v[3];if(!wall_face(a,t,depth,v))continue;
        bool positive=false,negative=false;
        for(int k=0;k<3;k++) {
            b3Vec3 p=v[k],q=v[(k+1)%3];float cross=(q.x-p.x)*(y-p.y)-(q.y-p.y)*(x-p.x);
            float tolerance=.006f*(fabsf(q.x-p.x)+fabsf(q.y-p.y));
            positive|=cross>tolerance;negative|=cross< -tolerance;
        }
        if(!(positive&&negative))return true;
    }
    return false;
}
static float masonry_noise(int owner,int patch_x,int patch_y,int x,int y,int axis) {
    uint32_t h=(uint32_t)owner*2246822519u^(uint32_t)patch_x*3266489917u^(uint32_t)patch_y*668265263u^
        (uint32_t)x*374761393u^(uint32_t)y*0x9e3779b9u^(uint32_t)axis*0x85ebca6bu;
    h=(h^(h>>13))*1274126177u;h^=h>>16;return (h%10001)/5000.0f-1;
}
static void sectioned_wall(SwatWorld* w,int owner) {
    const SwatMotelInstance* p=swat_motel_instance(owner-1);const SwatMotelAsset* a=swat_motel_asset(p->asset);
    float depth=p->asset==22?.06f:.09f,x[64],y[64];int nx=0,ny=0;
    for(int t=0;t<a->triangle_count;t++) {b3Vec3 v[3];if(!wall_face(a,t,depth,v))continue;
        for(int k=0;k<3;k++){wall_cut(x,&nx,v[k].x);wall_cut(y,&ny,v[k].y);}}
    assert(nx>=2 && ny>=2);qsort(x,nx,sizeof(float),float_order);qsort(y,ny,sizeof(float),float_order);
    int first=w->count;SwatMaterial material=p->asset==22?SWAT_DRYWALL:SWAT_BRICK;
    for(int ix=0;ix<nx-1;ix++)for(int iy=0;iy<ny-1;iy++) {
        float width=x[ix+1]-x[ix],height=y[iy+1]-y[iy];
        if(!wall_occupied(a,depth,(x[ix]+x[ix+1])*.5f,(y[iy]+y[iy+1])*.5f))continue;
        int columns=(int)ceilf(width/.7f),rows=(int)ceilf(height/.8f);
        for(int c=0;c<columns;c++)for(int r=0;r<rows;r++) {
            float dx=width/columns,dy=height/rows,v[4][2];
            float min_x=1e9f,max_x=-1e9f,min_y=1e9f,max_y=-1e9f;
            const int offset[4][2]={{0,0},{0,1},{1,1},{1,0}};
            for(int k=0;k<4;k++) {
                int gx=c+offset[k][0],gy=r+offset[k][1];
                // Shared vertices tile without gaps. Architectural opening/patch
                // boundaries stay fixed; only internal fracture lines wander.
                float px=x[ix]+gx*dx+(gx>0 && gx<columns?.20f*dx*masonry_noise(owner,ix,iy,gx,gy,0):0);
                float py=y[iy]+gy*dy+(gy>0 && gy<rows?.16f*dy*masonry_noise(owner,ix,iy,gx,gy,1):0);
                v[k][0]=py*p->scale.y;v[k][1]=px*p->scale.x;
                min_x=fminf(min_x,v[k][1]);max_x=fmaxf(max_x,v[k][1]);min_y=fminf(min_y,v[k][0]);max_y=fmaxf(max_y,v[k][0]);
            }
            float lx=(min_x+max_x)*.5f,ly=(min_y+max_y)*.5f;
            b3Pos center=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*lx,ly,-sinf(p->yaw)*lx));
            b3Vec3 half=swat_v(depth*p->scale.z,(max_y-min_y)*.5f,(max_x-min_x)*.5f);
            int id=swat_world_box(w,center,half,material,swat_material(material)->fracture_health);
            SwatObject* o=&w->objects[id];o->part=SWAT_PART_SKIN;o->wall_group=first+1;
            swat_world_place(o,swat_angle(p->yaw+SWAT_PI*.5f));
            for(int k=0;k<4;k++){v[k][0]-=ly;v[k][1]-=lx;}
            bool valid=swat_world_fragment(o,v);assert(valid);(void)valid;
        }
    }
    assert(w->count>first);SwatObject* old=&w->objects[owner];old->wall_group=first+1;
    b3DestroyBody(old->body);old->body=b3_nullBodyId;old->shape=b3_nullShapeId;old->active=false;
}
void swat_motel_build(SwatWorld* w) {
    swat_world_box(w,(b3Pos){0,-.58f,0},swat_v(24,.5f,24),SWAT_CONCRETE,0);
    for(int i=0;i<SWAT_MOTEL_INSTANCES;i++) {
        const SwatMotelInstance* p=swat_motel_instance(i); b3Pos center; b3Vec3 half; float yaw; recipe(i,&center,&half,&yaw);
        int id=swat_world_box(w,center,half,p->material,p->door?120:0); assert(id==i+1);
        SwatObject* o=&w->objects[id]; swat_world_place(o,yaw);
        if(p->door) { o->door=true; o->closed_yaw=yaw; o->hinge=(b3Pos){p->origin.x,p->origin.y+1.095f,p->origin.z}; }
    }
    bool bound=swat_motel_bind_collision(w); assert(bound); (void)bound;
    // Guest-room party walls: same two board faces, studs and charge aperture
    // used by scenario buildings. Preserve all existing object/door IDs.
    for(int i=106;i<=108;i++) {
        SwatObject* o=&w->objects[i];int first=w->count;
        swat_build_framed_wall(w,(b3Pos){o->center.x,0,-3},0,6,2.8f,0,0,0,0,false);
        o->wall_group=first+1;
        b3DestroyBody(o->body);o->body=b3_nullBodyId;o->shape=b3_nullShapeId;o->active=false;
    }
    for(int i=1;i<=SWAT_MOTEL_INSTANCES;i++)if(w->objects[i].active && sectioned_asset(swat_motel_instance(i-1)->asset))sectioned_wall(w,i);
    for(int owner=155;owner<=157;owner++) {
        int first=w->count;w->objects[owner].wall_group=first+1;
        for(int k=0;k<swat_motel_fence_part_count();k++) {
            int id=swat_world_box(w,fence_center(owner,k),fence_parts[k].half,SWAT_STEEL,fence_health(k));
            SwatObject* o=&w->objects[id];o->part=fence_parts[k].part;o->wall_group=first+1;swat_world_place(o,SWAT_PI*.5f);
        }
    }
    assert(fence_validate(w));fence_bind(w);
    assert(w->count==SWAT_MOTEL_SURROUNDINGS_FIRST);
    for(int k=0;k<SWAT_MOTEL_SURROUNDINGS_COUNT;k++) {
        b3Pos center;b3Vec3 half;float yaw;surroundings_recipe(w->count,&center,&half,&yaw);
        int id=swat_world_box(w,center,half,k%SWAT_SURROUNDINGS_PARTS<2?SWAT_SOIL:SWAT_STONE,0);swat_world_place(&w->objects[id],yaw);
    }
    assert(surroundings_validate(w));surroundings_bind(w);
    assert(w->count==SWAT_MOTEL_GROUND_FIRST);
    for(int k=0;k<SWAT_GROUND_PARTS;k++) {
        const SwatMotelAsset* a=&ground_parts[k];
        swat_world_box(w,(b3Pos){a->center.x,a->center.y,a->center.z},a->half,ground_materials[k],0);
    }
    assert(ground_validate(w));ground_bind(w);
    assert(w->count==SWAT_MOTEL_MOUNTED_FIRST);
    for(int i=0;i<SWAT_MOTEL_MOUNTED_INSTANCES;i++) {
        b3Pos center;b3Vec3 half;mounted_recipe(i,&center,&half);
        int id=swat_world_box(w,center,half,SWAT_STEEL,mounted_health());SwatObject* o=&w->objects[id];
        o->part=SWAT_PART_FIXTURE;o->structural_thickness=.0012f;swat_world_place(o,mounted_instances[i].yaw);
        int supports[SWAT_MAX_SUPPORTS],count=0;bool supported=mounted_supports(w,i,supports);assert(supported);
        while(count<SWAT_MAX_SUPPORTS && supports[count]>=0)count++;
        supported=swat_world_attach(w,id,supports,count);assert(supported);(void)supported;
    }
    assert(mounted_validate(w));mounted_bind(w);
    assert(w->count==SWAT_MOTEL_PROPS_FIRST);
    for(int i=0;i<SWAT_MOTEL_PROP_INSTANCES;i++) {
        b3Pos center;b3Vec3 half;prop_recipe(i,&center,&half);
        int id=swat_world_box(w,center,half,prop_instances[i].material,prop_health(i));SwatObject* o=&w->objects[id];
        o->part=SWAT_PART_FIXTURE;o->structural_thickness=prop_thickness[i];swat_world_place(o,prop_instances[i].yaw);
        int supports[SWAT_MAX_SUPPORTS],count=0;bool supported=prop_supports(w,i,supports);assert(supported);(void)supported;
        while(count<SWAT_MAX_SUPPORTS && supports[count]>=0)count++;
        bool attached=swat_world_attach(w,id,supports,count);assert(attached);(void)attached;
    }
    assert(props_validate(w));props_bind(w);
    assert(w->count==SWAT_MOTEL_PANES_FIRST);
    for(int i=0;i<10;i++) {
        b3Pos center;b3Vec3 half;float yaw;pane_recipe(i,&center,&half,&yaw);
        int id=swat_world_box(w,center,half,SWAT_OPAQUE_GLASS,pane_health(i));SwatObject* o=&w->objects[id];
        o->part=SWAT_PART_FIXTURE;o->wall_group=SWAT_MOTEL_PANES_FIRST+2*(i/2)+1;
        o->structural_thickness=2*pane_source(i)->half.z;swat_world_place(o,yaw);
        int parent=pane_parents[i/2];bool attached=swat_world_attach(w,id,&parent,1);assert(attached);(void)attached;
    }
    assert(panes_validate(w));panes_bind(w);
    b3Pos center;b3Vec3 half;nightstand_recipe(&center,&half);
    int stand=swat_world_box(w,center,half,SWAT_WOOD,nightstand_health());assert(stand==SWAT_MOTEL_NIGHTSTAND_FIRST);
    w->objects[stand].part=SWAT_PART_FIXTURE;w->objects[stand].structural_thickness=.024f;
    int support=81;bool attached=swat_world_attach(w,stand,&support,1);assert(attached);(void)attached;
    assert(nightstand_validate(w));nightstand_bind(w);
    for(int i=0;i<5;i++) w->rooms[i]=(SwatRoom){{-10+4*i,1.4f,-3},{1.88f,1.4f,2.88f},SWAT_PLASTER,SWAT_CARPET};
    w->rooms[5]=(SwatRoom){{-10,1.4f,-8},{1.88f,1.4f,1.88f},SWAT_PLASTER,SWAT_TILE};w->room_count=6;
}

int swat_motel_foliage(const SwatWorld* w,int index,SwatMotelInstance* out) {
    if(!w->motel || index<0 || index>=SWAT_MOTEL_FOLIAGE_INSTANCES)return -1;
    const SwatMotelFoliage* f=&motel_foliage[index];
    for(int i=0;i<2;i++)if(f->supports[i]>=0 &&
        (f->supports[i]>=w->count || !w->objects[f->supports[i]].active))return -1;
    *out=f->placement;return f->supports[0];
}
int swat_motel_dressing_parent(int index) {
    if(index<0 || index>=SWAT_MOTEL_DRESSING_INSTANCES)return -1;
    const int owners[]={14,14,24,24,20,16,105,20,24};
    int room=index/SWAT_MOTEL_DRESSING_ASSETS,kind=index%SWAT_MOTEL_DRESSING_ASSETS;
    return owners[kind]+room*(kind==6?1:24);
}
int swat_motel_dressing(const SwatWorld* w,int index,SwatMotelInstance* p) {
    if(!w->motel || index<0 || index>=SWAT_MOTEL_DRESSING_INSTANCES)return -1;
    int room=index/SWAT_MOTEL_DRESSING_ASSETS,kind=index%SWAT_MOTEL_DRESSING_ASSETS;
    float x=-6+4*room;int owner=swat_motel_dressing_parent(index);
    if(owner>=w->count)return -1;
    *p=(SwatMotelInstance){.asset=kind,.scale={1,1,1}};
    switch(kind) {
    case 0: p->origin=(b3Pos){x+.52f,.72f,-5.898f};break;
    case 1: p->origin=(b3Pos){x+1.10f,1.65f,-5.898f};break;
    case 2: case 3:
        p->origin=(b3Pos){x-1.49f,.7603f+(kind==2?.009f:0),-2.25f};p->yaw=SWAT_PI*.5f;break;
    case 4: p->origin=(b3Pos){x+.60f,1.65f,-3.938f};break;
    case 5: {
        const SwatObject* door=&w->objects[owner];
        p->yaw=door->yaw-SWAT_PI*.5f;
        // Replace the original viewer at its authored height, on the raised
        // door panel. The renderer omits that tiny original viewer primitive.
        p->origin=b3OffsetPos(door->hinge,swat_v(.54f*cosf(p->yaw)+.0345f*sinf(p->yaw),1.6f-door->half.y,-.54f*sinf(p->yaw)+.0345f*cosf(p->yaw)));
        break;
    }
    case 7: p->origin=(b3Pos){x+1.35f,1.25f,-3.938f};break;
    case 8: // Bottom contact anchor on the free end of the desk, clear of the tray/TV.
        p->origin=(b3Pos){x-1.67f,room==0?.7583f:.7606f,-1.88f};p->yaw=SWAT_PI*.5f;break;
    case 6:
        p->origin=(b3Pos){x-1.885f,.99f,-.95f};p->yaw=SWAT_PI*.5f;break;
    }
    const SwatObject* support=&w->objects[owner];
    if(support->active)return owner;
    if(!support->wall_group)return -1;
    for(int i=support->wall_group-1;i<w->count && w->objects[i].wall_group==support->wall_group;i++) {
        const SwatObject* s=&w->objects[i];if(s->part!=SWAT_PART_SKIN)continue;
        b3Vec3 d=b3SubPos(p->origin,s->center);
        float c=cosf(s->yaw),sn=sinf(s->yaw),x=c*d.x-sn*d.z,y=d.y,z=sn*d.x+c*d.z;
        if(fabsf(y)>s->half.y || fabsf(z)>s->half.z || fabsf(x)>s->half.x+.08f)continue;
        bool inside=true;
        if(s->fractured)for(int j=0;j<4;j++) {
            int k=(j+1)%4;float ay=s->corners[j][0],az=s->corners[j][1];
            if((s->corners[k][0]-ay)*(z-az)-(s->corners[k][1]-az)*(y-ay)<-1e-5f)inside=false;
        }
        if(!inside)continue;
        float shift=copysignf(s->half.x+.001f,x)-x;
        p->origin=b3OffsetPos(p->origin,swat_v(c*shift,0,-sn*shift));
        return s->active?i:-1;
    }
    return -1;
}

bool swat_motel_lamp(const SwatWorld* w,int room,b3Pos* origin) {
    if(!w->motel || room<1 || room>4)return false;
    SwatMotelInstance p;int owner=swat_motel_dressing(w,(room-1)*SWAT_MOTEL_DRESSING_ASSETS+7,&p);
    if(owner<0)return false;
    // ANCHOR_bulb_light from the accepted identity-root GLB, in metres.
    *origin=b3OffsetPos(p.origin,swat_v(.12970687f*sinf(p.yaw),.08959322f,.12970687f*cosf(p.yaw)));
    return true;
}

bool swat_motel_lamp_switch(const SwatWorld* w,int room,b3Pos* position) {
    if(!w->motel || room<1 || room>4)return false;
    SwatMotelInstance p;
    if(swat_motel_dressing(w,(room-1)*SWAT_MOTEL_DRESSING_ASSETS+7,&p)<0)return false;
    // Authored button center, transformed with the same supported mount as the lamp.
    *position=b3OffsetPos(p.origin,swat_v(.0165f*sinf(p.yaw),-.034f,.0165f*cosf(p.yaw)));
    return true;
}
