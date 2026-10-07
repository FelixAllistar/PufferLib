#include "motel.h"
#include "mission.h"
#include <assert.h>
#include "motel_data.h"
#include "motel_utility_data.h"
// Floor-level dressing stays outside the established door and capsule routes.
static const SwatMotelInstance utility_instances[]={
{40,{-4.55f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{-7.70f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false},
{40,{-0.55f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{-3.70f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false},
{40,{3.45f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{0.30f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false},
{40,{7.45f,.008f,-.65f},{1,1,1},0,SWAT_WOOD,false,false},
{41,{4.30f,.008f,-3.30f},{1,1,1},0,SWAT_WOOD,false,false}
};
const SwatMotelAsset* swat_motel_asset(int index) { return index<0 || index>=SWAT_MOTEL_ASSETS?NULL:index<SWAT_MOTEL_BASE_ASSETS?&motel_assets[index]:&utility_assets[index-SWAT_MOTEL_BASE_ASSETS]; }
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
bool swat_motel_bind_collision(SwatWorld* w) {
    // Canonical prefix remains stable; appended wall fragments use their explicit
    // wire geometry. Also retain the original map without utility props.
    if(w->count<SWAT_MOTEL_INSTANCES+1 && w->count!=SWAT_MOTEL_BASE_INSTANCES+1) return false;
    int instances=w->count==SWAT_MOTEL_BASE_INSTANCES+1?SWAT_MOTEL_BASE_INSTANCES:SWAT_MOTEL_INSTANCES;
    if(w->motel) return true;
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
        b3MeshDef def={.vertices=a->vertices,.indices=a->indices,.vertexCount=a->vertex_count,.triangleCount=a->triangle_count,
            .weldVertices=true,.weldTolerance=1e-6f,.identifyEdges=true};
        w->motel_meshes[i]=b3CreateMesh(&def,NULL,0); assert(w->motel_meshes[i]);
    }
    for(int i=0;i<instances;i++) {
        const SwatMotelInstance* p=swat_motel_instance(i); if(p->door) continue;
        SwatObject* o=&w->objects[i+1]; b3DestroyShape(o->shape,false);
        b3ShapeDef def=b3DefaultShapeDef(); def.baseMaterial=swat_physics_material(o->material);
        o->shape=b3CreateMeshShape(o->body,&def,w->motel_meshes[p->asset],p->scale); assert(B3_IS_NON_NULL(o->shape));
    }
    // New maps replace these whole walls with authored layers. Legacy maps
    // without those layers keep their original solid wall, even if truncated.
    for(int i=106;i<=108;i++) {
        SwatObject* o=&w->objects[i]; int first=o->wall_group-1;
        if(first<=SWAT_MOTEL_INSTANCES || first>=w->count || w->objects[first].wall_group!=o->wall_group)continue;
        b3DestroyBody(o->body);o->body=b3_nullBodyId;o->shape=b3_nullShapeId;o->active=false;
    }
    w->motel=true; return true;
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
    for(int i=0;i<5;i++) w->rooms[i]=(SwatRoom){{-10+4*i,1.4f,-3},{1.88f,1.4f,2.88f},SWAT_PLASTER,SWAT_CARPET};
    w->rooms[5]=(SwatRoom){{-10,1.4f,-8},{1.88f,1.4f,1.88f},SWAT_PLASTER,SWAT_TILE};w->room_count=6;
}

int swat_motel_dressing_parent(int index) {
    if(index<0 || index>=SWAT_MOTEL_DRESSING_INSTANCES)return -1;
    const int owners[]={14,14,24,24,20,16,105};
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
    case 6:
        p->origin=(b3Pos){x-1.885f,.99f,-.95f};p->yaw=SWAT_PI*.5f;break;
    }
    const SwatObject* support=&w->objects[owner];
    if(support->active)return owner;
    if(kind!=6 || !support->wall_group)return -1;
    // The bumper stays on the room-facing board, not an invisible retired wall.
    // Select by the original convex footprint even after a piece is destroyed,
    // so it cannot jump to a neighbouring support after a breach.
    for(int i=SWAT_MOTEL_INSTANCES+1;i<w->count;i++) {
        const SwatObject* s=&w->objects[i];
        if(s->wall_group!=support->wall_group || s->part!=SWAT_PART_SKIN || s->center.x<support->center.x)continue;
        float y=p->origin.y-s->center.y,z=p->origin.z-s->center.z;
        if(fabsf(y)>s->half.y || fabsf(z)>s->half.z)continue;
        bool inside=true;
        if(s->fractured)for(int j=0;j<4;j++) {
            int k=(j+1)%4;float ay=s->corners[j][0],az=s->corners[j][1];
            if((s->corners[k][0]-ay)*(z-az)-(s->corners[k][1]-az)*(y-ay)<-1e-5f)inside=false;
        }
        if(!inside)continue;
        p->origin.x=s->center.x+s->half.x+.001f;
        return s->active?i:-1;
    }
    return -1;
}
