// Static soft maintenance prop. The whole source remains one damage owner;
// contents, weight and torn-polymer dynamics are not supplied by the source.
// Existing soft-cover game response; this is not a measured polymer gauge.
static const float bag_skin=.00015f;
static float bag_health(void){const SwatMaterialDef* m=swat_material(SWAT_CLOTH);return m->fracture_health*bag_skin/m->reference_thickness;}
static bool bag_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_BAG_FIRST)return true;
    if(w->count!=SWAT_MOTEL_BAG_END && w->count!=SWAT_MOTEL_OBJECTS)return false;
    const SwatObject* o=&w->objects[SWAT_MOTEL_BAG_FIRST];int supports[SWAT_MAX_SUPPORTS];
    return floor_contacts(w,&bag_instance,127,bag_floor_contact,1,supports) && !memcmp(o->supports,supports,sizeof(supports)) &&
        b3Distance(o->center,b3OffsetPos(bag_instance.origin,bag_asset.center))<1e-4f && b3Length(b3Sub(o->half,bag_asset.half))<1e-5f &&
        o->yaw==0 && o->pitch==0 && !o->door && !o->fractured && !o->wall_group && o->part==SWAT_PART_FIXTURE &&
        o->material==SWAT_CLOTH && o->structural_thickness==bag_skin && o->max_health==bag_health();
}
static void bag_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_BAG_END)return;
    SwatObject* o=&w->objects[SWAT_MOTEL_BAG_FIRST];if(!o->active)return;
    b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
    bool bound=contact_meshes_bind(w,o,&bag_instance,&def);assert(bound);(void)bound;
}
static void bag_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_BAG_FIRST);
    int owner=swat_world_box(w,b3OffsetPos(bag_instance.origin,bag_asset.center),bag_asset.half,SWAT_CLOTH,bag_health());
    assert(owner==SWAT_MOTEL_BAG_FIRST);SwatObject* o=&w->objects[owner];o->part=SWAT_PART_FIXTURE;o->structural_thickness=bag_skin;
    int supports[SWAT_MAX_SUPPORTS];bool supported=floor_contacts(w,&bag_instance,127,bag_floor_contact,1,supports);assert(supported);(void)supported;
    bool attached=swat_world_attach(w,owner,supports,1);assert(attached);(void)attached;
    assert(bag_validate(w));bag_bind(w);
}
