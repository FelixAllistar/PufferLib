// Production raster depth layering vs an independent higher-precision control.
// The control changes only projection depth; the actual fix keeps both scene
// and viewmodel projection untouched.
#if defined(_WIN32)
__declspec(dllimport) unsigned char __stdcall glIsEnabled(unsigned int cap);
#else
extern unsigned char glIsEnabled(unsigned int cap);
#endif
static void wall_depth_graphics(SwatView* view,const char* directory) {
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.hostile_fire=false;
    swat_sim_init(&sim,config,73);swat_environment_art_prepare_location(&view->environment,&sim.world);before=sim.world;
    double original_near=rlGetCullDistanceNear(),original_far=rlGetCullDistanceFar();
    bool original_offset=view->environment.wall_depth_offset;
    int reproduced=0;
    for(int lit=0;lit<2;lit++)for(int distance=0;distance<2;distance++) {
        Camera3D camera={{distance?50:28,1.65f,8},{4,1,-3},{0,1,0},70,CAMERA_PERSPECTIVE};
        Image images[3];const char* names[]={"old","precision-control","layered"};
        for(int i=0;i<3;i++) {
            view->environment.wall_depth_offset=i==2;rlSetClipPlanes(i==1?.2:original_near,original_far);
            images[i]=room101_capture_size(view,camera,lit,1440,810);char file[4096];
            snprintf(file,sizeof(file),"%s/wall-depth-%s-%d-%s.png",directory,lit?"lit":"unlit",distance,names[i]);assert(ExportImage(images[i],file));
            assert(!glIsEnabled(0x8037u)); // No leakage into art, sky or later gun draws.
        }
        Color* old=LoadImageColors(images[0]),*control=LoadImageColors(images[1]),*fixed=LoadImageColors(images[2]);
        int checked=0,bad_before=0,bad_after=0;
        rlSetClipPlanes(original_near,original_far);
        for(int y=300;y<500;y++)for(int x=600;x<950;x++) {
            Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f,y+.5f},camera,1440,810);
            float t=(8.09f-ray.position.x)/ray.direction.x;if(t<=0)continue;
            Vector3 hit=Vector3Add(ray.position,Vector3Scale(ray.direction,t));
            if(hit.z< -5.4f||hit.z>-.6f||hit.y<.6f||hit.y>2.35f)continue;
            int p=y*1440+x;int a=abs(old[p].r-control[p].r)+abs(old[p].g-control[p].g)+abs(old[p].b-control[p].b);
            int b=abs(fixed[p].r-control[p].r)+abs(fixed[p].g-control[p].g)+abs(fixed[p].b-control[p].b);
            bad_before+=a>15;bad_after+=b>15;checked++;
        }
        printf("wall depth lit%d distance%d: %d face pixels, mismatches before%d after%d\n",lit,distance,checked,bad_before,bad_after);fflush(stdout);
        assert(checked>100 && bad_after<=bad_before/10);
        reproduced+=bad_before;
        UnloadImageColors(old);UnloadImageColors(control);UnloadImageColors(fixed);for(int i=0;i<3;i++)UnloadImage(images[i]);
    }
    assert(reproduced>10); // At least one production-distance view must reproduce the defect.
    view->environment.wall_depth_offset=original_offset;rlSetClipPlanes(original_near,original_far);
    assert(!memcmp(&before,&sim.world,sizeof(before)));swat_sim_close(&sim);
    puts("PASS wall depth layering: lit/unlit distance views match precision control, core-only offset resets, scene/viewmodel projection and authority unchanged");
}
