// One static enclosure owner, retaining both supplied meshes and open-door pose.
// Source substrate/gauge/hardware are unknown. Existing metal-cover response
// and health section are game approximations, not measurements of the material.
static const float cabinet_section=.0012f;
static float cabinet_health(void){const SwatMaterialDef* m=swat_material(SWAT_STEEL);return m->fracture_health*cabinet_section/m->reference_thickness;}
static bool cabinet_supports(const SwatWorld* w,int supports[SWAT_MAX_SUPPORTS]) {
    return wall_supports(w,&cabinet_instance,129,cabinet_rear_contacts,4,supports);
}
static bool cabinet_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_CABINET_FIRST)return true;
    if(w->count!=SWAT_MOTEL_CABINET_END && w->count!=SWAT_MOTEL_SERVICE_END && w->count!=SWAT_MOTEL_SCREWDRIVER_END && w->count!=SWAT_MOTEL_DUMPSTER_END && w->count!=SWAT_MOTEL_OBJECTS)return false;
    const SwatObject* o=&w->objects[SWAT_MOTEL_CABINET_FIRST];int supports[SWAT_MAX_SUPPORTS];
    return cabinet_supports(w,supports) && !memcmp(o->supports,supports,sizeof(supports)) &&
        b3Distance(o->center,b3OffsetPos(cabinet_instance.origin,cabinet_asset.center))<1e-4f && b3Length(b3Sub(o->half,cabinet_asset.half))<1e-5f &&
        o->yaw==0 && o->pitch==0 && !o->door && !o->fractured && !o->wall_group && o->part==SWAT_PART_FIXTURE &&
        o->material==SWAT_STEEL && o->structural_thickness==cabinet_section && o->max_health==cabinet_health();
}
static void cabinet_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_CABINET_END)return;
    SwatObject* o=&w->objects[SWAT_MOTEL_CABINET_FIRST];if(!o->active)return;
    b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
    bool bound=contact_meshes_bind(w,o,&cabinet_instance,&def);assert(bound);(void)bound;
}
static void cabinet_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_CABINET_FIRST);
    int owner=swat_world_box(w,b3OffsetPos(cabinet_instance.origin,cabinet_asset.center),cabinet_asset.half,SWAT_STEEL,cabinet_health());
    assert(owner==SWAT_MOTEL_CABINET_FIRST);SwatObject* o=&w->objects[owner];o->part=SWAT_PART_FIXTURE;o->structural_thickness=cabinet_section;
    int supports[SWAT_MAX_SUPPORTS];bool supported=cabinet_supports(w,supports);assert(supported);(void)supported;
    int count=0;while(count<SWAT_MAX_SUPPORTS && supports[count]>=0)count++;
    bool attached=swat_world_attach(w,owner,supports,count);assert(attached);(void)attached;
    assert(cabinet_validate(w));cabinet_bind(w);
}
