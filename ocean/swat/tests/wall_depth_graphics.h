// Same native scene/camera, isolated projection and lighting diagnostics.
static void wall_depth_graphics(SwatView* view,const char* directory) {
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.hostile_fire=false;
    swat_sim_init(&sim,config,73);swat_environment_art_prepare_location(&view->environment,&sim.world);before=sim.world;
    Camera3D camera={{50,1.65f,8},{4,1,-3},{0,1,0},70,CAMERA_PERSPECTIVE};
    double original_near=rlGetCullDistanceNear(),original_far=rlGetCullDistanceFar();
    const float planes[]={.01f,.05f,.1f,.2f,.5f,1};
    for(int lit=0;lit<2;lit++)for(int i=0;i<6;i++) {
        rlSetClipPlanes(planes[i],120);
        Image image=room101_capture_size(view,camera,lit,1440,810);char file[4096];
        snprintf(file,sizeof(file),"%s/wall-depth-%s-%d.png",directory,lit?"lit":"unlit",i);assert(ExportImage(image,file));UnloadImage(image);
    }
    rlSetClipPlanes(original_near,original_far);
    assert(!memcmp(&before,&sim.world,sizeof(before)));swat_sim_close(&sim);
    puts("PASS wall depth diagnostics: same camera/authority, six near-plane values with unlit/lit isolation; projection restored");
}
