#include "storefront.h"
#include <assert.h>
#include <string.h>
#include "storefront_data.h"
const SwatMotelAsset* swat_storefront_asset(int i) { return i>=0 && i<SWAT_STOREFRONT_ASSETS ? &storefront_assets[i] : NULL; }
const SwatMotelInstance* swat_storefront_instance(int i) { return i>=0 && i<SWAT_STOREFRONT_INSTANCES ? &storefront_instances[i] : NULL; }
static void recipe(int i,b3Pos* center,b3Vec3* half,float* yaw) {
    const SwatMotelInstance* p=&storefront_instances[i]; const SwatMotelAsset* a=&storefront_assets[p->asset];
    if(p->door) {
        float height=strstr(a->file,"glass_door") ? 2.55f : 2.39f;
        *half=swat_v(.025f,height*.5f,.6f); *yaw=p->yaw+SWAT_PI*.5f;
        *center=b3OffsetPos(p->origin,swat_v(.6f*cosf(p->yaw),half->y,-.6f*sinf(p->yaw))); return;
    }
    b3Vec3 local=swat_v(a->center.x*p->scale.x,a->center.y*p->scale.y,a->center.z*p->scale.z);
    float c=cosf(p->yaw),s=sinf(p->yaw);
    *center=b3OffsetPos(p->origin,swat_v(c*local.x+s*local.z,local.y,-s*local.x+c*local.z));
    *half=swat_v(fmaxf(.001f,a->half.x*p->scale.x),fmaxf(.001f,a->half.y*p->scale.y),fmaxf(.001f,a->half.z*p->scale.z)); *yaw=p->yaw;
}
bool swat_storefront_bind_collision(SwatWorld* w) {
    if(w->count!=SWAT_STOREFRONT_INSTANCES+1 || w->motel) return false;
    if(w->storefront) return true;
    for(int i=0;i<SWAT_STOREFRONT_INSTANCES;i++) {
        b3Pos center; b3Vec3 half; float yaw; recipe(i,&center,&half,&yaw);
        const SwatObject* o=&w->objects[i+1]; const SwatMotelInstance* p=&storefront_instances[i];
        if(p->door) {
            b3Pos hinge=b3OffsetPos(p->origin,swat_v(0,half.y,0));
            if(b3Distance(hinge,o->hinge)>1e-4f || fabsf(swat_angle(yaw-o->closed_yaw))>1e-5f) return false;
            center=b3OffsetPos(hinge,swat_v(sinf(o->yaw)*half.z,0,cosf(o->yaw)*half.z)); yaw=o->yaw;
        }
        if(b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(half,o->half))>1e-4f || fabsf(swat_angle(yaw-o->yaw))>1e-5f || o->pitch!=0 || o->door!=p->door || o->material!=p->material) return false;
    }
    for(int i=0;i<SWAT_STOREFRONT_ASSETS;i++) {
        const SwatMotelAsset* a=&storefront_assets[i];
        b3MeshDef def={.vertices=a->vertices,.indices=a->indices,.vertexCount=a->vertex_count,.triangleCount=a->triangle_count,.weldVertices=true,.weldTolerance=1e-6f,.identifyEdges=true};
        w->storefront_meshes[i]=b3CreateMesh(&def,NULL,0); assert(w->storefront_meshes[i]);
    }
    for(int i=0;i<SWAT_STOREFRONT_INSTANCES;i++) {
        const SwatMotelInstance* p=&storefront_instances[i]; if(p->door) continue;
        SwatObject* o=&w->objects[i+1]; b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef(); def.baseMaterial=swat_physics_material(o->material);
        o->shape=b3CreateMeshShape(o->body,&def,w->storefront_meshes[p->asset],p->scale); assert(B3_IS_NON_NULL(o->shape));
    }
    w->storefront=true; return true;
}
void swat_storefront_build(SwatWorld* w) {
    // Backup terrain sits below the authored street/sidewalk and interior slabs.
    swat_world_box(w,(b3Pos){0,-.68f,-4},swat_v(24,.5f,24),SWAT_CONCRETE,0);
    for(int i=0;i<SWAT_STOREFRONT_INSTANCES;i++) {
        const SwatMotelInstance* p=&storefront_instances[i]; b3Pos center; b3Vec3 half; float yaw; recipe(i,&center,&half,&yaw);
        int id=swat_world_box(w,center,half,p->material,p->door?120:0); assert(id==i+1);
        SwatObject* o=&w->objects[id]; swat_world_place(o,yaw);
        if(p->door) {
            o->door=true; o->closed_yaw=yaw; o->hinge=b3OffsetPos(p->origin,swat_v(0,half.y,0));
            o->locked=strstr(storefront_assets[p->asset].file,"stockroom")==NULL;
        }
    }
    bool bound=swat_storefront_bind_collision(w); assert(bound); (void)bound;
    w->rooms[0]=(SwatRoom){{-3,1.6f,-3},{2.88f,1.6f,2.88f},SWAT_PLASTER,SWAT_TILE};
    w->rooms[1]=(SwatRoom){{3,1.6f,-3},{2.88f,1.6f,2.88f},SWAT_PLASTER,SWAT_TILE};
    w->rooms[2]=(SwatRoom){{0,1.6f,-8},{5.88f,1.6f,1.88f},SWAT_PLASTER,SWAT_CONCRETE}; w->room_count=3;
}
