// Submission order must preserve the established layered wall rendering.
static void wall_batch_graphics(SwatView* view,const char* directory,bool faces) {
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
    for(int stage=0;stage<(faces?4:3);stage++) {
        if(stage==1) {
            assert(swat_world_breach(&sim.world,sim.world.objects[106].wall_group-1,(b3Pos){-3.9885f,1.05f,-1.17f})>4);
            int first=sim.world.objects[13].wall_group-1;assert(first>SWAT_MOTEL_INSTANCES);
            b3Pos opening=sim.world.objects[first].center;opening.y=.6f;
            assert(swat_world_breach(&sim.world,first,opening)>0);
        }
        if(stage==2)art->room101_v4[0]=(Model){0};
        if(stage==3) {
            art->room101_v4[0]=source;
            int group=sim.world.objects[13].wall_group;
            bool modified=false;
            for(int i=group-1;i<sim.world.count && sim.world.objects[i].wall_group==group;i++)if(sim.world.objects[i].active && !sim.world.objects[i].cuts) {
                SwatObject* o=&sim.world.objects[i];float corners[4][2];
                for(int v=0;v<4;v++)for(int axis=0;axis<2;axis++)corners[v][axis]=.9f*o->corners[v][axis];
                assert(swat_world_fragment(o,corners));modified=true;break;
            }
            assert(modified); // Legacy corner edits cannot replace compound cuts.
        }
        for(int lit=0;lit<2;lit++)for(size_t c=0;c<sizeof(cameras)/sizeof(*cameras);c++) {
            before=sim.world;
            if(faces)swat_environment_motel_face_batch(art,false);else swat_environment_motel_core_batch(art,false);
            Image reference=room101_capture_size(view,cameras[c],lit,640,480);
            if(faces)swat_environment_motel_face_batch(art,true);else swat_environment_motel_core_batch(art,true);
            Image batched=room101_capture_size(view,cameras[c],lit,640,480);
            Color* a=LoadImageColors(reference),*b=LoadImageColors(batched);int changed=0;
            for(int i=0;i<640*480;i++)changed+=memcmp(&a[i],&b[i],sizeof(Color))!=0;
            printf("Wall %s batch stage%d lit%d camera%zu differences%d\n",faces?"face":"core",stage,lit,c,changed);fflush(stdout);
            if(changed) {
                assert(ExportImage(reference,TextFormat("%s/core-reference.png",directory)));
                assert(ExportImage(batched,TextFormat("%s/core-batched.png",directory)));
            }
            assert(!changed && !glIsEnabled(0x8037u) && !memcmp(&before,&sim.world,sizeof(before)));
            if(faces && !lit && !c) {
                SwatWallFaceStats cached=swat_environment_motel_face_stats(art);
                Image again=room101_capture_size(view,cameras[c],lit,640,480);
                assert(swat_environment_motel_face_stats(art).builds==cached.builds && cached.attribute_bytes<4*1024*1024);
                Color* pixels=LoadImageColors(again);
                assert(!memcmp(b,pixels,640*480*sizeof(Color)));UnloadImageColors(pixels);UnloadImage(again);
                printf("Cached wall faces: %u builds, %zu vertices, %zu attribute bytes; unchanged pass creates no meshes\n",cached.builds,cached.vertices,cached.attribute_bytes);
            }
            comparisons++;
            UnloadImageColors(a);UnloadImageColors(b);UnloadImage(reference);UnloadImage(batched);
        }
    }
    art->room101_v4[0]=source;
    swat_sim_close(&sim);
    printf("PASS wall %s batching: %d pixel-identical lit/unlit camera pairs with intact/breached/missing-source%s geometry, native shadows/contact depth, offset reset and immutable authority\n",faces?"face":"core",comparisons,faces?"/partial-shape":"");
}
static void wall_core_batch_graphics(SwatView* view,const char* directory) {wall_batch_graphics(view,directory,false);}
static void wall_face_batch_graphics(SwatView* view,const char* directory) {wall_batch_graphics(view,directory,true);}
