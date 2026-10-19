// Original closed exterior in the rear utility court. Owner order follows the
// support graph; asset order preserves the delivered GLB node traversal.
static const SwatMotelInstance transformer_instances[]={
    {SWAT_MOTEL_TRANSFORMER_ASSET_FIRST+3,{10.8f,-.08f,-7.6f},{1,1,1},0,SWAT_CONCRETE,false,false},
    {SWAT_MOTEL_TRANSFORMER_ASSET_FIRST,{10.8f,-.08f,-7.6f},{1,1,1},0,SWAT_STEEL,false,false},
    {SWAT_MOTEL_TRANSFORMER_ASSET_FIRST+1,{10.8f,-.08f,-7.6f},{1,1,1},0,SWAT_STEEL,false,false},
    {SWAT_MOTEL_TRANSFORMER_ASSET_FIRST+2,{10.8f,-.08f,-7.6f},{1,1,1},0,SWAT_STEEL,false,false}
};
// Sections tune existing game damage. The cabinet is a joined exterior, so its
// aggregate section is an approximation, not a physical strength specification.
static const float transformer_sections[]={.18f,.04f,.022f,.022f};
bool swat_motel_transformer(const SwatWorld* w,int owner,SwatMotelInstance* out) {
    int index=owner-SWAT_MOTEL_TRANSFORMER_FIRST;
    if(!w->motel || w->count!=SWAT_MOTEL_OBJECTS || index<0 || index>=SWAT_MOTEL_TRANSFORMER_PARTS)return false;
    *out=transformer_instances[index];return true;
}
static const SwatMotelAsset* transformer_asset(int index) {
    return swat_motel_asset(transformer_instances[index].asset);
}
static b3Pos transformer_point(b3Vec3 point) {
    return b3OffsetPos(transformer_instances[0].origin,point);
}
static float transformer_health(int index) {
    const SwatMaterialDef* m=swat_material(transformer_instances[index].material);
    return m->fracture_health*transformer_sections[index]/m->reference_thickness;
}
static bool transformer_supports(const SwatWorld* w,int index,int supports[SWAT_MAX_SUPPORTS]) {
    for(int k=0;k<SWAT_MAX_SUPPORTS;k++)supports[k]=-1;
    if(index){supports[0]=SWAT_MOTEL_TRANSFORMER_FIRST+(index>1);return true;}
    const SwatObject* floor=&w->objects[0];
    if(floor->door || floor->fractured || floor->pitch!=0 || floor->yaw!=0 ||
       floor->part!=SWAT_PART_SOLID || floor->material!=SWAT_CONCRETE || floor->max_health!=0)return false;
    // All original bottom vertices, including the beveled footprint, must sit
    // on the actual foundation. No invisible pedestal or floating box proxy.
    const SwatMotelAsset* a=transformer_asset(0);int contacts=0;
    for(int v=0;v<a->vertex_count;v++) {
        b3Vec3 source=b3Add(a->vertices[v],a->center);
        if(fabsf(source.y)>1e-6f)continue;
        b3Pos point=transformer_point(source);contacts++;
        if(fabs(point.x-floor->center.x)>floor->half.x || fabs(point.z-floor->center.z)>floor->half.z ||
           fabs(point.y-floor->center.y-floor->half.y)>1e-4)return false;
    }
    if(!contacts)return false;
    supports[0]=0;return true;
}
static bool transformer_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_TRANSFORMER_FIRST)return true; // Previous canonical map.
    if(w->count!=SWAT_MOTEL_OBJECTS)return false;
    for(int i=0;i<SWAT_MOTEL_TRANSFORMER_PARTS;i++) {
        const SwatObject* o=&w->objects[SWAT_MOTEL_TRANSFORMER_FIRST+i];const SwatMotelAsset* a=transformer_asset(i);
        int supports[SWAT_MAX_SUPPORTS];
        if(!transformer_supports(w,i,supports) || memcmp(supports,o->supports,sizeof(supports)) ||
           b3Distance(o->center,transformer_point(a->center))>1e-4f || b3Length(b3Sub(o->half,a->half))>1e-5f ||
           o->yaw!=0 || o->pitch!=0 || o->door || o->fractured || o->wall_group || o->part!=SWAT_PART_FIXTURE ||
           o->material!=transformer_instances[i].material || o->structural_thickness!=transformer_sections[i] || o->max_health!=transformer_health(i))return false;
    }
    return true;
}
static void transformer_bind(SwatWorld* w) {
    if(w->count!=SWAT_MOTEL_OBJECTS)return;
    for(int i=0;i<SWAT_MOTEL_TRANSFORMER_PARTS;i++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_TRANSFORMER_FIRST+i];if(!o->active)continue;
        const SwatMotelInstance* p=&transformer_instances[i];
        b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        // Dense owners use existing 240-triangle adjacency-preserving subsets;
        // the 108-triangle plinth fits in one complete source mesh.
        if(!contact_meshes_bind(w,o,p,&def)) {
            b3Vec3 scale=swat_mul(p->scale,1/mesh_units(p->asset));
            o->shape=b3CreateMeshShape(o->body,&def,w->motel_meshes[p->asset],scale);
            o->query_mesh=(b3Mesh){w->motel_meshes[p->asset],scale};
        }
        assert(B3_IS_NON_NULL(o->shape));
    }
}
static void transformer_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_TRANSFORMER_FIRST);
    for(int i=0;i<SWAT_MOTEL_TRANSFORMER_PARTS;i++) {
        const SwatMotelAsset* a=transformer_asset(i);
        int id=swat_world_box(w,transformer_point(a->center),a->half,transformer_instances[i].material,transformer_health(i));
        assert(id==SWAT_MOTEL_TRANSFORMER_FIRST+i);SwatObject* o=&w->objects[id];
        o->part=SWAT_PART_FIXTURE;o->structural_thickness=transformer_sections[i];
        int supports[SWAT_MAX_SUPPORTS];bool valid=transformer_supports(w,i,supports);assert(valid);(void)valid;
        valid=swat_world_attach(w,id,supports,1);assert(valid);(void)valid;
    }
    assert(transformer_validate(w));transformer_bind(w);
}
