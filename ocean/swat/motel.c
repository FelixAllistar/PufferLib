#include "motel.h"
#include <string.h>
#include "mission.h"
#include <assert.h>
#include <stdlib.h>
#include "motel_data.h"
#include "motel_utility_data.h"
#include "motel_fence_data.h"
#include "motel_surroundings_data.h"
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
const SwatMotelAsset* swat_motel_asset(int index) { return index<0 || index>=SWAT_MOTEL_ASSETS?NULL:index<SWAT_MOTEL_BASE_ASSETS?&motel_assets[index]:index<42?&utility_assets[index-SWAT_MOTEL_BASE_ASSETS]:&fence_assets[index-42]; }
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
bool swat_motel_bind_collision(SwatWorld* w) {
    // Canonical prefix remains stable; appended wall fragments use their explicit
    // wire geometry. Also retain the original map without utility props.
    if(w->count<SWAT_MOTEL_INSTANCES+1 && w->count!=SWAT_MOTEL_BASE_INSTANCES+1 && w->count!=SWAT_MOTEL_UTILITY_INSTANCES+1) return false;
    int instances=w->count==SWAT_MOTEL_BASE_INSTANCES+1?SWAT_MOTEL_BASE_INSTANCES:w->count==SWAT_MOTEL_UTILITY_INSTANCES+1?SWAT_MOTEL_UTILITY_INSTANCES:SWAT_MOTEL_INSTANCES;
    if(w->motel) return true;
    if(!fence_validate(w) || !surroundings_validate(w))return false;
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
    fence_bind(w);
    surroundings_bind(w);
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
