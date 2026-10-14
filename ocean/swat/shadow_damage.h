// Exact regional refresh for appended or growing bullet damage. Other geometry/light
// change falls back to the ordinary full depth pass. Rendering keeps its
// original projection and viewport; a cropped matrix is used only for culling.
#ifndef SWAT_SHADOW_DAMAGE_H
#define SWAT_SHADOW_DAMAGE_H
static uint32_t shadow_damage_base(const SwatLighting* light,const SwatWorld* world) {
    uint32_t h=hash_bytes(2166136261u,&world->count,sizeof(world->count));
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];
        h=hash_bytes(h,&o->active,sizeof(o->active));h=hash_bytes(h,&o->material,sizeof(o->material));
        h=hash_bytes(h,&o->center,sizeof(o->center));h=hash_bytes(h,&o->half,sizeof(o->half));
        h=hash_bytes(h,&o->yaw,sizeof(o->yaw));h=hash_bytes(h,&o->pitch,sizeof(o->pitch));
        h=hash_bytes(h,&o->door,sizeof(o->door));h=hash_bytes(h,&o->latch_broken,sizeof(o->latch_broken));
        h=hash_bytes(h,&o->fractured,sizeof(o->fractured));
        if(o->fractured)h=hash_bytes(h,o->corners,sizeof(o->corners));
    }
    h=hash_bytes(h,world->rooms,(size_t)world->room_count*sizeof(*world->rooms));
    for(int i=0;i<world->room_count;i++) {Vector3 p=room_light_origin(world,i);h=hash_bytes(h,&p,sizeof(p));}
    return hash_bytes(h,&light->sun_direction,sizeof(light->sun_direction));
}
static uint32_t shadow_cut_prefix(const SwatObject* o,int count) {
    return count?hash_bytes(2166136261u,o->cuts->cuts,(size_t)count*sizeof(SwatCut)):2166136261u;
}
static bool shadow_damage_bounds(const SwatLighting* light,const SwatWorld* world,uint32_t base,BoundingBox* box) {
    if(base!=light->damage_base || world->count!=light->damage_objects)return false;
    *box=(BoundingBox){{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};bool changed=false;
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];int old=light->damage_cuts[i],count=o->cuts?o->cuts->count:0;
        if(count<old)return false;
        bool appended=shadow_cut_prefix(o,old)==light->damage_hash[i];
        for(int c=appended?old:0;c<count;c++) {
            const SwatCut* cut=&o->cuts->cuts[c];if(appended && cut->kind==SWAT_CUT_BREACH)return false;
            b3Quat rotation=swat_rotation(o->yaw,o->pitch);
            // Joining retained polygons can change source triangulation away
            // from the newest hit. The unchanged owner's full bounds contain
            // both old and new geometry, including a growing/replaced patch.
            bool entire=o->door || cut->kind>=SWAT_CUT_PATCH || !appended;
            float y=entire?0:cut->y,z=entire?0:cut->z;
            float hy=entire?o->half.y:cut->height*1.03f,hz=entire?o->half.z:cut->width*1.03f;
            for(int v=0;v<8;v++) {
                b3Vec3 local=swat_v((v&1?1:-1)*(o->half.x+.1f),
                    y+(v&2?1:-1)*(hy+.002f),z+(v&4?1:-1)*(hz+.002f));
                b3Pos p=b3OffsetPos(o->center,b3RotateVector(rotation,local));
                box->min=Vector3Min(box->min,(Vector3){p.x,p.y,p.z});
                box->max=Vector3Max(box->max,(Vector3){p.x,p.y,p.z});
            }
            changed=true;
        }
    }
    return changed;
}
static void shadow_damage_remember(SwatLighting* light,const SwatWorld* world,uint32_t base) {
    light->damage_base=base;light->damage_objects=world->count;
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];int count=o->cuts?o->cuts->count:0;
        light->damage_cuts[i]=count;light->damage_hash[i]=shadow_cut_prefix(o,count);
    }
}
// Pixel rectangle in one unchanged shadow face. Expand for numerical error,
// raster coverage and PCF. A box crossing the eye safely refreshes the full face.
static Rectangle shadow_damage_rect(Matrix m,const BoundingBox* box,int width,int height) {
    Rectangle full={0,0,width,height};if(!box)return full;
    float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
    for(int v=0;v<8;v++) {
        Vector3 p={v&1?box->max.x:box->min.x,v&2?box->max.y:box->min.y,v&4?box->max.z:box->min.z};
        float w=m.m3*p.x+m.m7*p.y+m.m11*p.z+m.m15;if(w<=.00001f)return full;
        float x=(m.m0*p.x+m.m4*p.y+m.m8*p.z+m.m12)/w;
        float y=(m.m1*p.x+m.m5*p.y+m.m9*p.z+m.m13)/w;
        x0=fminf(x0,x);x1=fmaxf(x1,x);y0=fminf(y0,y);y1=fmaxf(y1,y);
    }
    x0=Clamp(floorf((x0*.5f+.5f)*width)-4,0,width);x1=Clamp(ceilf((x1*.5f+.5f)*width)+4,0,width);
    y0=Clamp(floorf((y0*.5f+.5f)*height)-4,0,height);y1=Clamp(ceilf((y1*.5f+.5f)*height)+4,0,height);
    return (Rectangle){x0,y0,x1-x0,y1-y0};
}
#endif
