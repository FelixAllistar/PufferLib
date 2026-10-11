// Internal original seating recipes, compiled only in motel.c.
static const SwatMotelInstance seating_instances[]={
    {50,{2.83f,.004f,-.75f},{1,1,1},0,SWAT_WOOD,false,false},
    {51,{2.83f,.004f,-.75f},{1,1,1},0,SWAT_CLOTH,false,false},
    {52,{6.72f,.004f,-.6f},{1,1,1},SWAT_PI,SWAT_WOOD,false,false}
};
// Coarse game health sections, not certified wood/leather/construction gauges.
// Cushion uses the existing soft-cover response with a skin section; the daybed
// remains one mixed source primitive, not fabricated separate material layers.
static const float seating_thickness[]={.025f,.0015f,.025f};
static const int seating_floors[]={57,57,81};
bool swat_motel_seating(const SwatWorld* w,int owner,SwatMotelInstance* out) {
    int index=owner-SWAT_MOTEL_SEATING_FIRST;
    if(!w->motel || w->count!=SWAT_MOTEL_OBJECTS || index<0 || index>=SWAT_MOTEL_SEATING_PARTS)return false;
    *out=seating_instances[index];return true;
}
static float seating_health(int index) {
    const SwatMaterialDef* m=swat_material(seating_instances[index].material);
    return m->fracture_health*seating_thickness[index]/m->reference_thickness;
}
static void seating_recipe(int index,b3Pos* center,b3Vec3* half) {
    const SwatMotelInstance* p=&seating_instances[index];const SwatMotelAsset* a=seating_assets[index];
    *center=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*a->center.x+sinf(p->yaw)*a->center.z,a->center.y,-sinf(p->yaw)*a->center.x+cosf(p->yaw)*a->center.z));
    *half=a->half;
}
static bool seating_supports(const SwatWorld* w,int index,int supports[SWAT_MAX_SUPPORTS]) {
    if(index==1){for(int k=0;k<SWAT_MAX_SUPPORTS;k++)supports[k]=k?-1:SWAT_MOTEL_SEATING_FIRST;return true;}
    return floor_supports(w,&seating_instances[index],seating_floors[index],index?seating_feet_2:seating_feet_0,supports);
}
static bool seating_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_SEATING_FIRST)return true;
    if(w->count!=SWAT_MOTEL_OBJECTS)return false;
    for(int i=0;i<SWAT_MOTEL_SEATING_PARTS;i++) {
        const SwatObject* o=&w->objects[SWAT_MOTEL_SEATING_FIRST+i];const SwatMotelInstance* p=&seating_instances[i];
        b3Pos center;b3Vec3 half;int supports[SWAT_MAX_SUPPORTS];seating_recipe(i,&center,&half);
        if(!seating_supports(w,i,supports) || memcmp(o->supports,supports,sizeof(supports)) ||
           b3Distance(o->center,center)>1e-4f || b3Length(b3Sub(o->half,half))>1e-5f || fabsf(swat_angle(o->yaw-p->yaw))>1e-5f ||
           o->door || o->fractured || o->pitch!=0 || o->wall_group || o->part!=SWAT_PART_FIXTURE || o->material!=p->material ||
           o->structural_thickness!=seating_thickness[i] || o->max_health!=seating_health(i))return false;
    }return true;
}
static void seating_bind(SwatWorld* w) {
    if(w->count!=SWAT_MOTEL_OBJECTS)return;
    for(int i=0;i<SWAT_MOTEL_SEATING_PARTS;i++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_SEATING_FIRST+i];if(!o->active)continue;
        b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        bool bound=contact_meshes_bind(w,o,&seating_instances[i],&def);assert(bound);(void)bound;
    }
}
static void seating_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_SEATING_FIRST);
    for(int i=0;i<SWAT_MOTEL_SEATING_PARTS;i++) {
        b3Pos center;b3Vec3 half;seating_recipe(i,&center,&half);const SwatMotelInstance* p=&seating_instances[i];
        int owner=swat_world_box(w,center,half,p->material,seating_health(i));assert(owner==SWAT_MOTEL_SEATING_FIRST+i);
        SwatObject* o=&w->objects[owner];o->part=SWAT_PART_FIXTURE;o->structural_thickness=seating_thickness[i];swat_world_place(o,p->yaw);
        int supports[SWAT_MAX_SUPPORTS];bool supported=seating_supports(w,i,supports);
        if(!supported)fprintf(stderr,"Unsupported seating part%d floor%d pivot %.6f %.6f %.6f\n",i,seating_floors[i],(float)p->origin.x,(float)p->origin.y,(float)p->origin.z);
        assert(supported);(void)supported;
        bool attached=swat_world_attach(w,owner,supports,1);assert(attached);(void)attached;
    }
    assert(seating_validate(w));seating_bind(w);
}
