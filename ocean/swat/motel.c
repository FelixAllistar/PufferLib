#include "motel.h"
#include "mission.h"
#include <assert.h>
#include <stdlib.h>
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
    for(int i=1;i<=instances;i++) {
        SwatObject* o=&w->objects[i]; int first=o->wall_group-1;
        if(first<=SWAT_MOTEL_INSTANCES || first>=w->count || w->objects[first].wall_group!=o->wall_group)continue;
        b3DestroyBody(o->body);o->body=b3_nullBodyId;o->shape=b3_nullShapeId;o->active=false;
    }
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
int swat_motel_wall_edges(const SwatWorld* w,const SwatObject* o,SwatMotelEdge* edges,int capacity) {
    if(!o->active || o->material!=SWAT_BRICK || swat_motel_wall_parent(w,o)<0)return 0;
    int count=0;float c=cosf(o->yaw),s=sinf(o->yaw);
    for(int i=o->wall_group-1;i<w->count && w->objects[i].wall_group==o->wall_group;i++) {
        const SwatObject* n=&w->objects[i];if(n->active || n->part!=SWAT_PART_SKIN)continue;
        b3Vec3 d=b3SubPos(n->center,o->center);float y=d.y,z=s*d.x+c*d.z;
        float ly=0,lz=0,length=0,roll=0;
        if(fabsf(fabsf(y)-o->half.y-n->half.y)<.0001f) {
            float lo=fmaxf(-o->half.z,z-n->half.z),hi=fminf(o->half.z,z+n->half.z);
            length=hi-lo;lz=(hi+lo)*.5f;ly=copysignf(o->half.y,y);roll=y>0?SWAT_PI:0;
        } else if(fabsf(fabsf(z)-o->half.z-n->half.z)<.0001f) {
            float lo=fmaxf(-o->half.y,y-n->half.y),hi=fminf(o->half.y,y+n->half.y);
            length=hi-lo;ly=(hi+lo)*.5f;lz=copysignf(o->half.z,z);roll=z>0?-SWAT_PI*.5f:SWAT_PI*.5f;
        }
        if(length<.0001f)continue;
        if(count>=capacity)return count;
        edges[count++]=(SwatMotelEdge){o->tag.index,i,b3OffsetPos(o->center,swat_v(s*lz,ly,c*lz)),o->yaw,roll,length,2*o->half.x};
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
            float lx=(x[ix]+width*(c+.5f)/columns)*p->scale.x,ly=(y[iy]+height*(r+.5f)/rows)*p->scale.y;
            b3Pos center=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*lx,ly,-sinf(p->yaw)*lx));
            b3Vec3 half=swat_v(depth*p->scale.z,height*p->scale.y/(2*rows),width*p->scale.x/(2*columns));
            int id=swat_world_box(w,center,half,material,swat_material(material)->fracture_health);
            SwatObject* o=&w->objects[id];o->part=SWAT_PART_SKIN;o->wall_group=first+1;
            swat_world_place(o,swat_angle(p->yaw+SWAT_PI*.5f));
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
    for(int i=0;i<5;i++) w->rooms[i]=(SwatRoom){{-10+4*i,1.4f,-3},{1.88f,1.4f,2.88f},SWAT_PLASTER,SWAT_CARPET};
    w->rooms[5]=(SwatRoom){{-10,1.4f,-8},{1.88f,1.4f,1.88f},SWAT_PLASTER,SWAT_TILE};w->room_count=6;
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
