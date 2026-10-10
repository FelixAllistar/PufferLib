// Submission order must preserve the established layered wall rendering.
static void wall_core_batch_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,42);SwatEnvironmentArt* art=&view->environment;
    swat_environment_art_prepare_location(art,&sim.world);
    Camera3D cameras[]={
        {{-6,1.62f,-1},{-6,1.4f,-5},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{-10,1.62f,12},{-6,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{0,1.62f,16},{0,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{10,1.62f,12},{6,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{-6.7f,1.6f,-9.5f},{-6.7f,1.1f,-5.5f},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{50,1.65f,8},{4,1,-3},{0,1,0},70,CAMERA_PERSPECTIVE},
        {{0,25,12},{0,0,12},{0,0,-1},55,CAMERA_ORTHOGRAPHIC}};
    int comparisons=0;
    Model source=art->room101_v4[0];
    for(int stage=0;stage<3;stage++) {
        if(stage==1) {
            assert(swat_world_breach(&sim.world,sim.world.objects[106].wall_group-1,(b3Pos){-3.9885f,1.05f,-1.17f})>4);
            int first=sim.world.objects[13].wall_group-1;assert(first>SWAT_MOTEL_INSTANCES);
            b3Pos opening=sim.world.objects[first].center;opening.y=.6f;
            assert(swat_world_breach(&sim.world,first,opening)>0);
        }
        if(stage==2)art->room101_v4[0]=(Model){0};
        for(int lit=0;lit<2;lit++)for(size_t c=0;c<sizeof(cameras)/sizeof(*cameras);c++) {
            before=sim.world;swat_environment_motel_core_batch(art,false);
            Image reference=room101_capture_size(view,cameras[c],lit,640,480);
            swat_environment_motel_core_batch(art,true);
            Image batched=room101_capture_size(view,cameras[c],lit,640,480);
            Color* a=LoadImageColors(reference),*b=LoadImageColors(batched);int changed=0;
            for(int i=0;i<640*480;i++)changed+=memcmp(&a[i],&b[i],sizeof(Color))!=0;
            printf("Wall core batch stage%d lit%d camera%zu differences%d\n",stage,lit,c,changed);fflush(stdout);
            if(changed) {
                assert(ExportImage(reference,TextFormat("%s/core-reference.png",directory)));
                assert(ExportImage(batched,TextFormat("%s/core-batched.png",directory)));
            }
            assert(!changed && !glIsEnabled(0x8037u) && !memcmp(&before,&sim.world,sizeof(before)));
            comparisons++;
            UnloadImageColors(a);UnloadImageColors(b);UnloadImage(reference);UnloadImage(batched);
        }
    }
    art->room101_v4[0]=source;
    swat_sim_close(&sim);
    printf("PASS wall core batching: %d pixel-identical lit/unlit camera pairs with intact/breached/missing-source geometry, native shadows/contact depth, offset reset and immutable authority\n",comparisons);
}
