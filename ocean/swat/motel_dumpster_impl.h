// Original static storage pose: one steel assembly and two molded lids.
// The existing rubber response approximates the molded lids in game space;
// neither these sections nor their health certify a fabrication strength.
static const SwatMotelInstance dumpster_instances[]={
    {60,{-4.9f,-.08f,-8.4f},{1,1,1},SWAT_PI,SWAT_STEEL,false,false},
    {61,{-4.9f,-.08f,-8.4f},{1,1,1},SWAT_PI,SWAT_RUBBER,false,false},
    {62,{-4.9f,-.08f,-8.4f},{1,1,1},SWAT_PI,SWAT_RUBBER,false,false}
};
static const float dumpster_sections[]={.017f,.020f,.020f};
bool swat_motel_dumpster(const SwatWorld* w,int owner,SwatMotelInstance* out) {
    int index=owner-SWAT_MOTEL_DUMPSTER_FIRST;
    if(!w->motel || w->count<SWAT_MOTEL_DUMPSTER_END || index<0 || index>=SWAT_MOTEL_DUMPSTER_PARTS)return false;
    *out=dumpster_instances[index];return true;
}
static float dumpster_health(int index) {
    const SwatMaterialDef* m=swat_material(dumpster_instances[index].material);
    return m->fracture_health*dumpster_sections[index]/m->reference_thickness;
}
static b3Pos dumpster_point(b3Vec3 point) {
    const SwatMotelInstance* p=&dumpster_instances[0];
    return b3OffsetPos(p->origin,b3RotateVector(b3MakeQuatFromAxisAngle(swat_v(0,1,0),p->yaw),point));
}
static bool dumpster_supports(const SwatWorld* w,int index,int supports[SWAT_MAX_SUPPORTS]) {
    for(int k=0;k<SWAT_MAX_SUPPORTS;k++)supports[k]=-1;
    if(index){supports[0]=SWAT_MOTEL_DUMPSTER_FIRST;return true;}
    // All eight authored skid-bottom corners lie on the original closed court
    // foundation. Validate actual contact height and footprint before binding.
    const SwatObject* floor=&w->objects[0];
    if(floor->door || floor->fractured || floor->pitch!=0 || floor->yaw!=0 ||
       floor->part!=SWAT_PART_SOLID || floor->material!=SWAT_CONCRETE || floor->max_health!=0)return false;
    for(int i=0;i<8;i++) {
        b3Pos point=dumpster_point(dumpster_skid_contacts[i]);
        if(fabs(point.x-floor->center.x)>floor->half.x || fabs(point.z-floor->center.z)>floor->half.z ||
           fabs(point.y-floor->center.y-floor->half.y)>.0001)return false;
    }
    supports[0]=0;return true;
}
static bool dumpster_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_DUMPSTER_FIRST)return true;
    if(w->count!=SWAT_MOTEL_DUMPSTER_END && w->count!=SWAT_MOTEL_ROADSIDE_END && w->count!=SWAT_MOTEL_OBJECTS)return false;
    for(int i=0;i<SWAT_MOTEL_DUMPSTER_PARTS;i++) {
        const SwatObject* o=&w->objects[SWAT_MOTEL_DUMPSTER_FIRST+i];const SwatMotelAsset* a=&dumpster_data[i];
        int supports[SWAT_MAX_SUPPORTS];
        if(!dumpster_supports(w,i,supports) || memcmp(supports,o->supports,sizeof(supports)) ||
           b3Distance(o->center,dumpster_point(a->center))>1e-4f || b3Length(b3Sub(o->half,a->half))>1e-5f ||
           o->yaw!=SWAT_PI || o->pitch!=0 || o->door || o->fractured || o->wall_group || o->part!=SWAT_PART_FIXTURE ||
           o->material!=dumpster_instances[i].material || o->structural_thickness!=dumpster_sections[i] || o->max_health!=dumpster_health(i))return false;
    }
    return true;
}
static void dumpster_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_DUMPSTER_END)return;
    for(int i=0;i<SWAT_MOTEL_DUMPSTER_PARTS;i++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_DUMPSTER_FIRST+i];if(!o->active)continue;
        b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        bool bound=contact_meshes_bind(w,o,&dumpster_instances[i],&def);assert(bound);(void)bound;
    }
}
static void dumpster_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_DUMPSTER_FIRST);
    for(int i=0;i<SWAT_MOTEL_DUMPSTER_PARTS;i++) {
        const SwatMotelAsset* a=&dumpster_data[i];
        int owner=swat_world_box(w,dumpster_point(a->center),a->half,dumpster_instances[i].material,dumpster_health(i));
        assert(owner==SWAT_MOTEL_DUMPSTER_FIRST+i);SwatObject* o=&w->objects[owner];
        o->part=SWAT_PART_FIXTURE;o->structural_thickness=dumpster_sections[i];swat_world_place(o,SWAT_PI);
        int supports[SWAT_MAX_SUPPORTS];bool supported=dumpster_supports(w,i,supports);assert(supported);(void)supported;
        bool attached=swat_world_attach(w,owner,supports,1);assert(attached);(void)attached;
    }
    assert(dumpster_validate(w));dumpster_bind(w);
}
