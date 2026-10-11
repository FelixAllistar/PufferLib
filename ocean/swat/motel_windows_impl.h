// Internal R1 assembly implementation, compiled only in motel.c.
#include "motel_windows_data.h"
const SwatWindowModel* swat_motel_window_model(int kind){return kind>=0 && kind<2?&window_r1_models[kind]:NULL;}
bool swat_motel_windows_revised(const SwatWorld* w){return w->motel && w->count>=SWAT_MOTEL_WINDOWS_END;}
int swat_motel_window_first(int parent){
    int bay=swat_motel_window_index(parent);if(bay<0)return -1;
    return SWAT_MOTEL_WINDOWS_FIRST+(bay?window_r1_models[1].part_count+(bay-1)*window_r1_models[0].part_count:0);
}
int swat_motel_window_part_parent(const SwatWorld* w,int owner){
    if(!swat_motel_windows_revised(w) || owner<SWAT_MOTEL_WINDOWS_FIRST || owner>=SWAT_MOTEL_WINDOWS_END)return -1;
    int offset=owner-SWAT_MOTEL_WINDOWS_FIRST,bay=offset<window_r1_models[1].part_count?0:1+(offset-window_r1_models[1].part_count)/window_r1_models[0].part_count;
    return pane_parents[bay];
}
int swat_motel_window_find(const SwatWorld* w,int parent,const char* name,SwatMaterial material){
    if(!swat_motel_windows_revised(w))return -1;
    int bay=swat_motel_window_index(parent);if(bay<0)return -1;
    const SwatWindowModel* m=swat_motel_window_model(bay?0:1);for(int i=0;i<m->part_count;i++)if(m->parts[i].material==material && !strcmp(name,m->parts[i].name))return swat_motel_window_first(parent)+i;
    return -1;
}
uint64_t swat_motel_window_mask(const SwatWorld* w,int parent){
    if(!swat_motel_windows_revised(w))return 0;
    int bay=swat_motel_window_index(parent);if(bay<0)return 0;
    const SwatWindowModel* m=swat_motel_window_model(bay?0:1);int first=swat_motel_window_first(parent);uint64_t mask=0;
    for(int i=0;i<m->part_count;i++)if(w->objects[first+i].active)mask|=UINT64_C(1)<<i;
    return mask;
}
static float window_part_health(const SwatWindowPart* p){const SwatMaterialDef* m=swat_material(p->material);return m->fracture_health*p->thickness/m->reference_thickness;}
static void window_part_recipe(int parent,int index,b3Pos* center,b3Vec3* half,float* yaw,int supports[SWAT_MAX_SUPPORTS]){
    const SwatMotelInstance* placement=swat_motel_instance(parent-1);const SwatWindowPart* p=&window_r1_models[parent==6?1:0].parts[index];
    *center=b3OffsetPos(placement->origin,swat_v(cosf(placement->yaw)*p->center.x+sinf(placement->yaw)*p->center.z,p->center.y,-sinf(placement->yaw)*p->center.x+cosf(placement->yaw)*p->center.z));
    *half=swat_v(p->half.z,p->half.y,p->half.x);*yaw=placement->yaw+SWAT_PI*.5f;
    for(int k=0;k<SWAT_MAX_SUPPORTS;k++)supports[k]=p->supports[k]>=0?swat_motel_window_first(parent)+p->supports[k]:-1;
    if(supports[0]<0)supports[0]=parent;
}
static bool windows_validate(const SwatWorld* w){
    if(w->count<=SWAT_MOTEL_WINDOWS_FIRST)return true;
    if(w->count!=SWAT_MOTEL_WINDOWS_END && w->count!=SWAT_MOTEL_OBJECTS)return false;
    for(int bay=0;bay<5;bay++){
        int parent=pane_parents[bay],first=swat_motel_window_first(parent);const SwatWindowModel* m=&window_r1_models[bay?0:1];
        for(int i=0;i<m->part_count;i++){
            b3Pos center;b3Vec3 half;float yaw;int supports[SWAT_MAX_SUPPORTS];window_part_recipe(parent,i,&center,&half,&yaw,supports);
            const SwatObject* o=&w->objects[first+i];const SwatWindowPart* p=&m->parts[i];
            if(b3Distance(o->center,center)>1e-4f || b3Length(b3Sub(o->half,half))>1e-5f || fabsf(swat_angle(o->yaw-yaw))>1e-5f ||
               o->door || o->fractured || o->pitch!=0 || o->part!=SWAT_PART_FIXTURE || o->wall_group!=first+1 ||
               o->material!=p->material || o->max_health!=window_part_health(p) || o->structural_thickness!=p->thickness || memcmp(supports,o->supports,sizeof(supports)))return false;
        }
    }return true;
}
static void windows_bind(SwatWorld* w){
    if(w->count<SWAT_MOTEL_WINDOWS_END)return;
    // Legacy aggregates remain active logical opening anchors. They own no
    // collision/render triangles in R1. Every physical piece has its own material.
    for(int bay=0;bay<5;bay++){
        SwatObject* o=&w->objects[pane_parents[bay]];int n=b3Body_GetShapeCount(o->body);b3ShapeId* shapes=malloc((size_t)n*sizeof(*shapes));assert(shapes || !n);
        assert(b3Body_GetShapes(o->body,shapes,n)==n);for(int i=0;i<n;i++)b3DestroyShape(shapes[i],false);free(shapes);o->shape=b3_nullShapeId;o->query_mesh=(b3Mesh){0};
    }
    for(int i=0;i<10;i++){SwatObject* o=&w->objects[SWAT_MOTEL_PANES_FIRST+i];if(B3_IS_NON_NULL(o->body))b3DestroyBody(o->body);o->body=b3_nullBodyId;o->shape=b3_nullShapeId;o->query_mesh=(b3Mesh){0};o->active=false;o->health=0;}
    for(int kind=0;kind<2;kind++){
        const SwatWindowModel* m=&window_r1_models[kind];
        for(int part=0;part<m->part_count;part++){
            const SwatWindowPart* p=&m->parts[part];int triangles=0;for(int t=0;t<m->triangle_count;t++)triangles+=m->triangle_parts[t]==part;
            b3Vec3* v=malloc((size_t)triangles*3*sizeof(*v));int32_t* indices=malloc((size_t)triangles*3*sizeof(*indices));assert(v && indices);int n=0;
            for(int t=0;t<m->triangle_count;t++)if(m->triangle_parts[t]==part)for(int k=0;k<3;k++){
                b3Vec3 d=b3Sub(m->vertices[3*t+k],p->center);v[n]=swat_mul(swat_v(-d.z,d.y,d.x),100);indices[n]=n;n++;
            }
            b3MeshDef def={.vertices=v,.indices=indices,.vertexCount=n,.triangleCount=triangles,.weldVertices=true,.weldTolerance=.0001f,.identifyEdges=true};
            int full=owned_mesh(w,b3CreateMesh(&def,NULL,0));free(v);free(indices);b3MeshData* mesh=w->motel_contact_meshes[full];assert(mesh->triangleCount==triangles);
            int first=w->motel_contact_mesh_count;for(int start=0;start<triangles;start+=240){int selection[240],count=triangles-start;if(count>240)count=240;for(int k=0;k<count;k++)selection[k]=start+k;owned_mesh(w,b3CreateMeshSubset(mesh,selection,count));}
            int end=w->motel_contact_mesh_count;
            for(int bay=kind?0:1;bay<(kind?1:5);bay++){
                SwatObject* o=&w->objects[swat_motel_window_first(pane_parents[bay])+part];if(!o->active)continue;
                b3DestroyShape(o->shape,false);b3ShapeDef shape=b3DefaultShapeDef();shape.baseMaterial=swat_physics_material(o->material);
                for(int j=first;j<end;j++){b3ShapeId id=b3CreateMeshShape(o->body,&shape,w->motel_contact_meshes[j],swat_v(.01f,.01f,.01f));assert(B3_IS_NON_NULL(id));if(j==first)o->shape=id;}
                o->query_mesh=(b3Mesh){mesh,{.01f,.01f,.01f}};
            }
        }
    }
}
static void windows_build(SwatWorld* w){
    assert(w->count==SWAT_MOTEL_WINDOWS_FIRST);
    for(int bay=0;bay<5;bay++){
        int parent=pane_parents[bay];const SwatWindowModel* m=&window_r1_models[bay?0:1];
        for(int part=0;part<m->part_count;part++){
            b3Pos center;b3Vec3 half;float yaw;int supports[SWAT_MAX_SUPPORTS];window_part_recipe(parent,part,&center,&half,&yaw,supports);
            const SwatWindowPart* p=&m->parts[part];int id=swat_world_box(w,center,half,p->material,window_part_health(p));SwatObject* o=&w->objects[id];
            o->part=SWAT_PART_FIXTURE;o->structural_thickness=p->thickness;o->wall_group=swat_motel_window_first(parent)+1;swat_world_place(o,yaw);
            int count=0;while(count<SWAT_MAX_SUPPORTS && supports[count]>=0)count++;bool attached=swat_world_attach(w,id,supports,count);assert(attached);(void)attached;
        }
    }assert(windows_validate(w));windows_bind(w);
}
