// Static assembly attachments and game material sections. Source geometry is
// unchanged; these health sections do not certify automotive/building strength.
static const SwatMotelInstance roadside_placements[]={
    {0,{-6,-.08f,6.65f},{1,1,1},SWAT_PI,SWAT_STEEL,false,false},
    {1,{-8,-.08f,34.75f},{1,1,1},0,SWAT_STEEL,false,false}
};
int swat_motel_roadside_count(int model){return model==0?SWAT_SEDAN_PARTS:model==1?SWAT_SHELTER_PARTS:0;}
int swat_motel_roadside_first(int model){return model==0?SWAT_MOTEL_SEDAN_FIRST:model==1?SWAT_MOTEL_SHELTER_FIRST:-1;}
const SwatRoadsidePart* swat_motel_roadside_part(int model,int part){return part<0 || part>=swat_motel_roadside_count(model)?NULL:model?&shelter_parts[part]:&sedan_parts[part];}
int swat_motel_roadside_triangle(int model,int triangle){return triangle<0 || triangle>=(model==0?13124:model==1?7868:0)?-1:model?shelter_triangle_parts[triangle]:sedan_triangle_parts[triangle];}
bool swat_motel_roadside(const SwatWorld* w,int owner,SwatMotelInstance* out) {
    if(!w->motel || w->count!=SWAT_MOTEL_OBJECTS || owner<SWAT_MOTEL_SEDAN_FIRST || owner>=SWAT_MOTEL_OBJECTS)return false;
    *out=roadside_placements[owner>=SWAT_MOTEL_SHELTER_FIRST];return true;
}
uint64_t swat_motel_roadside_mask(const SwatWorld* w,int model) {
    int first=swat_motel_roadside_first(model),count=swat_motel_roadside_count(model);uint64_t mask=0;
    if(!w->motel || w->count!=SWAT_MOTEL_OBJECTS)return 0;
    for(int i=0;i<count;i++)if(w->objects[first+i].active)mask|=UINT64_C(1)<<i;
    return mask;
}
static b3Pos roadside_point(int model,b3Vec3 point) {
    const SwatMotelInstance* p=&roadside_placements[model];
    return b3OffsetPos(p->origin,b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),p->yaw),point));
}
static float roadside_health(const SwatRoadsidePart* p){const SwatMaterialDef* m=swat_material(p->material);return m->fracture_health*p->section/m->reference_thickness;}
static bool roadside_supports(const SwatWorld* w,int model,int part,int supports[SWAT_MAX_SUPPORTS]) {
    const SwatRoadsidePart* p=swat_motel_roadside_part(model,part);
    for(int i=0;i<SWAT_MAX_SUPPORTS;i++)supports[i]=p->parents[i]<0?-1:swat_motel_roadside_first(model)+p->parents[i];
    if(p->parents[0]>=0)return true;
    int floor=model?SWAT_MOTEL_GROUND_FIRST+13:0;const SwatObject* ground=&w->objects[floor];
    if(ground->door || ground->fractured || ground->yaw!=0 || ground->pitch!=0 || ground->max_health!=0)return false;
    // Sedan root: four actual authored tire contacts locate the parked assembly.
    // Shelter roots: each original baseplate's four bottom corners. Static
    // damage does not add a suspension, a passenger interior or an occupancy rig.
    if(!p->anchor_count)return false;
    for(int i=0;i<p->anchor_count;i++) {
        b3Pos point=roadside_point(model,p->anchors[i]);
        if(fabs(point.x-ground->center.x)>ground->half.x || fabs(point.z-ground->center.z)>ground->half.z ||
           fabs(point.y-ground->center.y-ground->half.y)>1e-4f)return false;
    }
    supports[0]=floor;return true;
}
static bool roadside_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_DUMPSTER_END)return true;
    if(w->count!=SWAT_MOTEL_OBJECTS)return false;
    for(int model=0;model<2;model++)for(int part=0;part<swat_motel_roadside_count(model);part++) {
        const SwatRoadsidePart* p=swat_motel_roadside_part(model,part);const SwatObject* o=&w->objects[swat_motel_roadside_first(model)+part];int supports[SWAT_MAX_SUPPORTS];
        if(!roadside_supports(w,model,part,supports) || memcmp(supports,o->supports,sizeof(supports)) ||
           b3Distance(o->center,roadside_point(model,p->center))>1e-4f || b3Length(b3Sub(o->half,p->half))>1e-5f ||
           o->yaw!=roadside_placements[model].yaw || o->pitch!=0 || o->door || o->fractured || o->wall_group || o->part!=SWAT_PART_FIXTURE ||
           o->material!=p->material || o->structural_thickness!=p->section || o->max_health!=roadside_health(p))return false;
    }
    return true;
}
static void roadside_bind(SwatWorld* w) {
    if(w->count!=SWAT_MOTEL_OBJECTS)return;
    for(int model=0;model<2;model++)for(int part=0;part<swat_motel_roadside_count(model);part++) {
        const SwatRoadsidePart* p=swat_motel_roadside_part(model,part);SwatObject* o=&w->objects[swat_motel_roadside_first(model)+part];
        swat_world_build_progress(w);
        b3Vec3* vertices=malloc((size_t)p->vertex_count*sizeof(*vertices));assert(vertices);
        for(int i=0;i<p->vertex_count;i++)vertices[i]=swat_mul(p->vertices[i],100);
        b3MeshDef mesh={.vertices=vertices,.indices=p->indices,.vertexCount=p->vertex_count,.triangleCount=p->triangle_count,.weldVertices=true,.weldTolerance=.0001f,.identifyEdges=true};
        b3MeshData* full=b3CreateMesh(&mesh,NULL,0);assert(full);owned_mesh(w,full);free(vertices);
        // Bound each physical contact shape to the same established 240-triangle
        // budget while preserving cooked adjacency and full sparse ray queries.
        b3DestroyShape(o->shape,false);o->query_mesh=(b3Mesh){full,{.01f,.01f,.01f}};
        b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        for(int first=0;first<full->triangleCount;first+=240) {
            int count=full->triangleCount-first;if(count>240)count=240;int32_t selection[240];for(int i=0;i<count;i++)selection[i]=first+i;
            b3MeshData* subset=b3CreateMeshSubset(full,selection,count);assert(subset);owned_mesh(w,subset);
            b3ShapeId shape=b3CreateMeshShape(o->body,&def,subset,swat_v(.01f,.01f,.01f));assert(B3_IS_NON_NULL(shape));if(!first)o->shape=shape;
        }
    }
}
static void roadside_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_DUMPSTER_END);
    for(int model=0;model<2;model++)for(int part=0;part<swat_motel_roadside_count(model);part++) {
        const SwatRoadsidePart* p=swat_motel_roadside_part(model,part);
        int id=swat_world_box(w,roadside_point(model,p->center),p->half,p->material,roadside_health(p));assert(id==swat_motel_roadside_first(model)+part);
        SwatObject* o=&w->objects[id];o->part=SWAT_PART_FIXTURE;o->structural_thickness=p->section;swat_world_place(o,roadside_placements[model].yaw);
        int supports[SWAT_MAX_SUPPORTS];bool valid=roadside_supports(w,model,part,supports);assert(valid);(void)valid;
        int count=0;while(count<SWAT_MAX_SUPPORTS && supports[count]>=0)count++;
        valid=swat_world_attach(w,id,supports,count);assert(valid);
    }
    assert(roadside_validate(w));roadside_bind(w);
}
