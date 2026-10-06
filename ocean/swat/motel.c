#include "motel.h"
#include <assert.h>
#include "motel_data.h"
const SwatMotelAsset* swat_motel_asset(int index) { return index>=0 && index<SWAT_MOTEL_ASSETS?&motel_assets[index]:NULL; }
const SwatMotelInstance* swat_motel_instance(int index) { return index>=0 && index<SWAT_MOTEL_INSTANCES?&motel_instances[index]:NULL; }
static void recipe(int i,b3Pos* center,b3Vec3* half,float* yaw) {
    const SwatMotelInstance* p=&motel_instances[i]; const SwatMotelAsset* a=&motel_assets[p->asset];
    if(p->door) {
        *half=swat_v(.025f,1.095f,.54f); *yaw=p->yaw+SWAT_PI*.5f;
        *center=b3OffsetPos(p->origin,swat_v(.54f*cosf(p->yaw),1.095f,-.54f*sinf(p->yaw))); return;
    }
    b3Vec3 local=swat_v(a->center.x*p->scale.x,a->center.y*p->scale.y,a->center.z*p->scale.z);
    float c=cosf(p->yaw),s=sinf(p->yaw);
    *center=b3OffsetPos(p->origin,swat_v(c*local.x+s*local.z,local.y,-s*local.x+c*local.z));
    *half=swat_v(fmaxf(.001f,a->half.x*p->scale.x),fmaxf(.001f,a->half.y*p->scale.y),fmaxf(.001f,a->half.z*p->scale.z)); *yaw=p->yaw;
}
bool swat_motel_bind_collision(SwatWorld* w) {
    if(w->count!=SWAT_MOTEL_INSTANCES+1) return false;
    if(w->motel) return true;
    for(int i=0;i<SWAT_MOTEL_INSTANCES;i++) {
        b3Pos center; b3Vec3 half; float yaw; recipe(i,&center,&half,&yaw); const SwatObject* o=&w->objects[i+1];
        const SwatMotelInstance* p=&motel_instances[i];
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
        const SwatMotelAsset* a=&motel_assets[i];
        b3MeshDef def={.vertices=a->vertices,.indices=a->indices,.vertexCount=a->vertex_count,.triangleCount=a->triangle_count,
            .weldVertices=true,.weldTolerance=1e-6f,.identifyEdges=true};
        w->motel_meshes[i]=b3CreateMesh(&def,NULL,0); assert(w->motel_meshes[i]);
    }
    for(int i=0;i<SWAT_MOTEL_INSTANCES;i++) {
        const SwatMotelInstance* p=&motel_instances[i]; if(p->door) continue;
        SwatObject* o=&w->objects[i+1]; b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef(); def.baseMaterial=swat_physics_material(o->material);
        o->shape=b3CreateMeshShape(o->body,&def,w->motel_meshes[p->asset],p->scale); assert(B3_IS_NON_NULL(o->shape));
    }
    w->motel=true; return true;
}
void swat_motel_build(SwatWorld* w) {
    swat_world_box(w,(b3Pos){0,-.58f,0},swat_v(24,.5f,24),SWAT_CONCRETE,0);
    for(int i=0;i<SWAT_MOTEL_INSTANCES;i++) {
        const SwatMotelInstance* p=&motel_instances[i]; b3Pos center; b3Vec3 half; float yaw; recipe(i,&center,&half,&yaw);
        int id=swat_world_box(w,center,half,p->material,p->door?120:0); assert(id==i+1);
        SwatObject* o=&w->objects[id]; swat_world_place(o,yaw);
        if(p->door) { o->door=true; o->closed_yaw=yaw; o->hinge=(b3Pos){p->origin.x,p->origin.y+1.095f,p->origin.z}; }
    }
    bool bound=swat_motel_bind_collision(w); assert(bound); (void)bound;
    for(int i=0;i<5;i++) w->rooms[i]=(SwatRoom){{-10+4*i,1.4f,-3},{1.88f,1.4f,2.88f},SWAT_PLASTER,SWAT_CARPET};
    w->rooms[5]=(SwatRoom){{-10,1.4f,-8},{1.88f,1.4f,1.88f},SWAT_PLASTER,SWAT_TILE};w->room_count=6;
}
