// Cut a closed outline through every member of one authored wall assembly.
// This fixture isolates support topology; ordinary input coverage is separate.
static void wall_support_ring(SwatWorld* w,int group,float centre_y,float centre_z,float half_y,float half_z) {
    const float bands[4][4]={{centre_y-half_y,centre_z,.055f,half_z+.1f},
        {centre_y+half_y,centre_z,.055f,half_z+.1f},
        {centre_y,centre_z-half_z,half_y+.1f,.055f},{centre_y,centre_z+half_z,half_y+.1f,.055f}};
    swat_world_support_begin(w);
    for(int band=0;band<4;band++)for(int i=0;i<w->count;i++) {
        SwatObject* o=&w->objects[i];if(!o->active || o->wall_group!=group || o->support_role!=SWAT_SUPPORT_MEMBER)continue;
        b3Pos point={o->center.x,bands[band][0],bands[band][1]};
        b3Vec3 p=b3InvRotateVector(swat_object_rotation(o),b3SubPos(point,o->center));
        assert(swat_world_cut(w,i,(SwatCut){.y=p.y,.z=p.z,.height=bands[band][2],.width=bands[band][3],.seed=(unsigned)band,.kind=SWAT_CUT_BREACH})>=0);
    }
    swat_world_support_end(w);
}
