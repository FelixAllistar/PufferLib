// Original static tool on the trolley; no handheld use or rolling simulation.
// Mixed wood/metal assembly uses one finite steel-cover game response. Source
// construction and strengths are unknown; this is not a measured shaft gauge.
static const float screwdriver_section=.003f;
static float screwdriver_health(void){const SwatMaterialDef* m=swat_material(SWAT_STEEL);return m->fracture_health*screwdriver_section/m->reference_thickness;}
static b3Vec3 screwdriver_contact(void){return b3Add(screwdriver_vertices[1139],screwdriver_asset.center);}
static bool screwdriver_supports(const SwatWorld* w,int supports[SWAT_MAX_SUPPORTS]) {
    // Actual minimum-height source vertex against the original top liner.
    b3Vec3 contact=screwdriver_contact();
    return surface_contacts(w,&screwdriver_instance,SWAT_MOTEL_PROPS_FIRST,&prop_instances[0],&contact,1,supports);
}
static bool screwdriver_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_SCREWDRIVER_FIRST)return true;
    if(w->count!=SWAT_MOTEL_OBJECTS)return false;
    const SwatObject* o=&w->objects[SWAT_MOTEL_SCREWDRIVER_FIRST];int supports[SWAT_MAX_SUPPORTS];
    return screwdriver_supports(w,supports) && !memcmp(o->supports,supports,sizeof(supports)) &&
        b3Distance(o->center,b3OffsetPos(screwdriver_instance.origin,screwdriver_asset.center))<1e-4f && b3Length(b3Sub(o->half,screwdriver_asset.half))<1e-5f &&
        o->yaw==0 && o->pitch==0 && !o->door && !o->fractured && !o->wall_group && o->part==SWAT_PART_FIXTURE &&
        o->material==SWAT_STEEL && o->structural_thickness==screwdriver_section && o->max_health==screwdriver_health();
}
static void screwdriver_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_OBJECTS)return;
    SwatObject* o=&w->objects[SWAT_MOTEL_SCREWDRIVER_FIRST];if(!o->active)return;
    b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
    bool bound=contact_meshes_bind(w,o,&screwdriver_instance,&def);assert(bound);(void)bound;
}
static void screwdriver_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_SCREWDRIVER_FIRST);
    int owner=swat_world_box(w,b3OffsetPos(screwdriver_instance.origin,screwdriver_asset.center),screwdriver_asset.half,SWAT_STEEL,screwdriver_health());
    assert(owner==SWAT_MOTEL_SCREWDRIVER_FIRST);SwatObject* o=&w->objects[owner];o->part=SWAT_PART_FIXTURE;o->structural_thickness=screwdriver_section;
    int supports[SWAT_MAX_SUPPORTS];bool supported=screwdriver_supports(w,supports);assert(supported);(void)supported;
    bool attached=swat_world_attach(w,owner,supports,1);assert(attached);(void)attached;
    assert(screwdriver_validate(w));screwdriver_bind(w);
}
