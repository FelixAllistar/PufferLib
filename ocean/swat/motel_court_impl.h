// Briar Court court layer (MOTEL_LAYOUT.md). Included by motel.c after the
// picnic table. Closed guest-room bays, end walls, piers and walkway posts from
// motel_court_data.h (tools/gen_motel_court.py). Each row is an ordinary static
// motel_v1 instance: a box record with the canonical recipe, replaced by the
// asset's own mesh collision exactly like the authored base instances. No row
// is a door, a room, a support or a fixture, and none can be damaged
// (max_health 0), so the closed rooms are solid to movement, bullets, sight and
// navigation and need no interior content.
static bool court_row(int owner,const SwatMotelCourtRow** out) {
    int index=owner-SWAT_MOTEL_COURT_FIRST;
    if(index<0 || index>=SWAT_MOTEL_COURT_INSTANCES)return false;
    *out=&court_rows[index];return true;
}
bool swat_motel_court(const SwatWorld* w,int owner,SwatMotelInstance* placement,int* template_owner) {
    const SwatMotelCourtRow* row;
    if(!w->motel || w->count<SWAT_MOTEL_COURT_END || !court_row(owner,&row))return false;
    if(placement)*placement=row->placement;
    if(template_owner)*template_owner=row->template_owner;
    return true;
}
bool swat_motel_art_frame(const SwatWorld* w,int owner,int* source,SwatMotelInstance* from,SwatMotelInstance* to) {
    if(!w->motel)return false;
    const SwatMotelCourtRow* row;
    if(w->count>=SWAT_MOTEL_COURT_END && court_row(owner,&row)) {
        *source=row->template_owner;*from=*swat_motel_authored_instance(row->template_owner-1);*to=row->placement;
        // Doors carry their placement at the hinge with yaw 0 (the leaf box adds
        // a quarter turn); the static leaf uses the same frame.
        return true;
    }
    if(owner<1 || owner>SWAT_MOTEL_INSTANCES)return false;
    const SwatMotelInstance* now=swat_motel_instance(owner-1),*authored=swat_motel_authored_instance(owner-1);
    if(now==authored)return false;
    *source=owner;*from=*authored;*to=*now;return true;
}
static void court_recipe(const SwatMotelInstance* p,b3Pos* center,b3Vec3* half,float* yaw) {
    const SwatMotelAsset* a=swat_motel_asset(p->asset);
    b3Vec3 local=swat_v(a->center.x*p->scale.x,a->center.y*p->scale.y,a->center.z*p->scale.z);
    float c=cosf(p->yaw),s=sinf(p->yaw);
    *center=b3OffsetPos(p->origin,swat_v(c*local.x+s*local.z,local.y,-s*local.x+c*local.z));
    *half=swat_v(fmaxf(.001f,a->half.x*p->scale.x),fmaxf(.001f,a->half.y*p->scale.y),fmaxf(.001f,a->half.z*p->scale.z));*yaw=p->yaw;
}
static bool court_validate(const SwatWorld* w) {
    if(w->count<=SWAT_MOTEL_COURT_FIRST)return true; // Previous canonical prefix ends at the picnic table.
    if(w->count!=SWAT_MOTEL_COURT_END)return false;
    for(int i=0;i<SWAT_MOTEL_COURT_INSTANCES;i++) {
        const SwatMotelInstance* p=&court_rows[i].placement;const SwatObject* o=&w->objects[SWAT_MOTEL_COURT_FIRST+i];
        b3Pos center;b3Vec3 half;float yaw;court_recipe(p,&center,&half,&yaw);
        if(b3Distance(center,o->center)>1e-4f || b3Length(b3Sub(half,o->half))>1e-4f || fabsf(swat_angle(yaw-o->yaw))>1e-5f ||
           o->pitch!=0 || o->door || o->wall_group || o->part!=SWAT_PART_SOLID || o->max_health!=0 || o->material!=p->material)return false;
    }
    return true;
}
static void court_bind(SwatWorld* w) {
    if(w->count<SWAT_MOTEL_COURT_END)return;
    for(int i=0;i<SWAT_MOTEL_COURT_INSTANCES;i++) {
        SwatObject* o=&w->objects[SWAT_MOTEL_COURT_FIRST+i];if(!o->active)continue;
        const SwatMotelInstance* p=&court_rows[i].placement;
        if(i%16==0)swat_world_build_progress(w);
        b3DestroyShape(o->shape,false);b3ShapeDef def=b3DefaultShapeDef();def.baseMaterial=swat_physics_material(o->material);
        if(!contact_meshes_bind(w,o,p,&def))o->shape=b3CreateMeshShape(o->body,&def,w->motel_meshes[p->asset],p->scale);
        assert(B3_IS_NON_NULL(o->shape));
    }
}
static void court_build(SwatWorld* w) {
    assert(w->count==SWAT_MOTEL_COURT_FIRST);
    for(int i=0;i<SWAT_MOTEL_COURT_INSTANCES;i++) {
        b3Pos center;b3Vec3 half;float yaw;court_recipe(&court_rows[i].placement,&center,&half,&yaw);
        int id=swat_world_box(w,center,half,court_rows[i].placement.material,0);assert(id==SWAT_MOTEL_COURT_FIRST+i);
        swat_world_place(&w->objects[id],yaw);
    }
    assert(court_validate(w));court_bind(w);
}
