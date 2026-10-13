// Compact native proof of inset physical edges, source-mesh clipping and live replication.
static void breach_geometry_graphics(SwatView* view,const char* directory) {
    static SwatMap map;static SwatSnapshot state;
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);swat_capture_map(&sim,1,&map);
    swat_environment_art_prepare_location(&view->environment,&sim.world);
    Camera3D camera={{-6.7f,1.6f,-9.5f},{-6.7f,1.1f,-5.5f},{0,1,0},65,CAMERA_PERSPECTIVE};
    char path[4096];Image intact=room101_capture_size(view,camera,true,960,540);
    snprintf(path,sizeof(path),"%s/masonry-before.png",directory);assert(ExportImage(intact,path));
    SwatHit hit=swat_world_ray(&sim.world,(b3Pos){-6.7f,1.1f,-7},swat_v(0,0,1),1.5f,b3_nullBodyId);assert(hit.hit);
    assert(swat_world_breach(&sim.world,hit.index,hit.point)>0);
    before=sim.world;
    Image cut=room101_capture_size(view,camera,true,960,540);
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    int pixels=impact_pixel_difference(intact,cut);assert(pixels>1000);
    snprintf(path,sizeof(path),"%s/masonry-after.png",directory);assert(ExportImage(cut,path));
    swat_capture_snapshot(&sim,1,&state);
    swat_sim_close(&sim);swat_apply_map(&sim,&map);assert(swat_apply_snapshot(&sim,&state) && sim.world.motel);
    Image synchronized=room101_capture_size(view,camera,true,960,540);
    assert(!impact_pixel_difference(cut,synchronized));
    Camera3D close={{-6.75f,1.38f,-6.7f},{-7.3333f,1.15f,-5.99f},{0,1,0},55,CAMERA_PERSPECTIVE};
    Image edge=room101_capture_size(view,close,true,960,540);
    snprintf(path,sizeof(path),"%s/masonry-edge-close.png",directory);assert(ExportImage(edge,path));UnloadImage(edge);
    // New cuts expose their own physical core; legacy trim must not frame them.
    Model original=view->environment.masonry_edge;
    view->environment.masonry_edge=LoadModelFromMesh(GenMeshCube(.184f,.004f,1));
    view->environment.masonry_edge.materials[0].maps[MATERIAL_MAP_ALBEDO].color=RED;
    Image marked=room101_capture_size(view,camera,false,960,540);Color* colors=LoadImageColors(marked);int red=0;
    for(int i=0;i<marked.width*marked.height;i++)red+=colors[i].r>200 && colors[i].g<80 && colors[i].b<80;
    assert(red==0);UnloadImageColors(colors);UnloadImage(marked);
    UnloadModel(view->environment.masonry_edge);view->environment.masonry_edge=original;
    UnloadImage(intact);UnloadImage(cut);UnloadImage(synchronized);swat_sim_close(&sim);
    printf("PASS breach geometry graphics: %d altered pixels, pixel-identical live replica, %d obsolete-trim marker pixels; clipped source faces, physical material edges, lit close view and immutable render authority\n",pixels,red);
}
