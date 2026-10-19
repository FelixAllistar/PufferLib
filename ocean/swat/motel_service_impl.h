// Original full hose/bracket/fittings separated by authored atlas regions.
// Static storage pose and game attachments; no plumbing or deformable hose.
static const SwatMotelInstance service_instances[]={
    {55,{-8.7f,1.45f,-6.091f},{1,1,1},SWAT_PI,SWAT_STEEL,false,false},
    {56,{-8.7f,1.45f,-6.091f},{1,1,1},SWAT_PI,SWAT_RUBBER,false,false},
    {57,{-8.7f,1.45f,-6.091f},{1,1,1},SWAT_PI,SWAT_STEEL,false,false},
    {58,{-9.022f,1.34483493f,-6.091f},{1,1,1},SWAT_PI,SWAT_STEEL,false,false}
};
// The backplate is authored 8 mm. Other health sections and metal profiles are
// explicit game approximations, not measured polymer/brass/pipe strength.
static const float service_sections[]={.008f,.003f,.003f,.004f};
static const b3Vec3 service_bracket_anchors[]={{0,-.077f,0},{0,.077f,0}};
static const b3Vec3 service_bib_anchors[]={{-.044f,.062665071f,0},{.044f,.062665071f,0},{-.044f,.147665071f,0},{.044f,.147665071f,0}};
bool swat_motel_service(const SwatWorld* w,int owner,SwatMotelInstance* out) {
    int index=owner-SWAT_MOTEL_SERVICE_FIRST;
    if(!w->motel || w->count<SWAT_MOTEL_SERVICE_END || index<0 || index>=SWAT_MOTEL_SERVICE_PARTS)return false;
    *out=service_instances[index];return true;
}
static float service_health(int index) {
    const SwatMaterialDef* m=swat_material(service_instances[index].material);
    return m->fracture_health*service_sections[index]/m->reference_thickness;
}
static void service_recipe(int index,b3Pos* center,b3Vec3* half) {
    const SwatMotelInstance* p=&service_instances[index];const SwatMotelAsset* a=service_assets[index];
    *center=b3OffsetPos(p->origin,swat_v(cosf(p->yaw)*a->center.x+sinf(p->yaw)*a->center.z,a->center.y,-sinf(p->yaw)*a->center.x+cosf(p->yaw)*a->center.z));*half=a->half;
}
static bool service_supports(const SwatWorld* w,int index,int supports[SWAT_MAX_SUPPORTS]) {
    if(index==0)return wall_supports(w,&service_instances[index],7,service_bracket_anchors,2,supports);
    if(index==3)return wall_supports(w,&service_instances[index],7,service_bib_anchors,4,supports);
    for(int k=0;k<SWAT_MAX_SUPPORTS;k++)supports[k]=k?-1:SWAT_MOTEL_SERVICE_FIRST+(index==2?1:0);
    return true;
}
static bool service_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_SERVICE_FIRST)return true;
    if(w->count!=SWAT_MOTEL_SERVICE_END && w->count!=SWAT_MOTEL_SCREWDRIVER_END && w->count!=SWAT_MOTEL_DUMPSTER_END && w->count!=SWAT_MOTEL_ROADSIDE_END && w->count!=SWAT_MOTEL_OBJECTS)return false;
    for(int i=0;i<SWAT_MOTEL_SERVICE_PARTS;i++) {
        const SwatObject* o=&w->objects[SWAT_MOTEL_SERVICE_FIRST+i];b3Pos center;b3Vec3 half;int supports[SWAT_MAX_SUPPORTS];service_recipe(i,&center,&half);
        if(!service_supports(w,i,supports)||memcmp(supports,o->supports,sizeof(supports))||b3Distance(center,o->center)>1e-4f||b3Length(b3Sub(half,o->half))>1e-5f||
            o->yaw!=service_instances[i].yaw||o->pitch!=0||o->door||o->fractured||o->wall_group||o->part!=SWAT_PART_FIXTURE||
            o->material!=service_instances[i].material||o->structural_thickness!=service_sections[i]||o->max_health!=service_health(i))return false;
    }
    return true;
}
static void service_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_SERVICE_END)return;
    for(int i=0;i<SWAT_MOTEL_SERVICE_PARTS;i++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_SERVICE_FIRST+i];if(!o->active)continue;
        b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        bool bound=contact_meshes_bind(w,o,&service_instances[i],&def);assert(bound);(void)bound;
    }
}
static void service_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_SERVICE_FIRST);
    for(int i=0;i<SWAT_MOTEL_SERVICE_PARTS;i++) {
        b3Pos center;b3Vec3 half;service_recipe(i,&center,&half);
        int owner=swat_world_box(w,center,half,service_instances[i].material,service_health(i));assert(owner==SWAT_MOTEL_SERVICE_FIRST+i);
        SwatObject* o=&w->objects[owner];o->yaw=service_instances[i].yaw;o->part=SWAT_PART_FIXTURE;o->structural_thickness=service_sections[i];
        b3Body_SetTransform(o->body,center,b3MakeQuatFromAxisAngle(swat_v(0,1,0),o->yaw));
        int supports[SWAT_MAX_SUPPORTS];bool supported=service_supports(w,i,supports);assert(supported);(void)supported;
        int n=0;while(n<SWAT_MAX_SUPPORTS&&supports[n]>=0)n++;
        bool attached=swat_world_attach(w,owner,supports,n);assert(attached);(void)attached;
    }
    assert(service_validate(w));service_bind(w);
}
