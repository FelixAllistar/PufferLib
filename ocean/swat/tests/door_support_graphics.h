// Original motel leaf and detail art following the achieved physical body.
static void support_hinge_shots(int owner,int hinge) {
    SwatObject* o=&sim.world.objects[owner];b3Vec3 h=swat_door_hinge(o,hinge);
    for(int shot=0;shot<8 && (!o->motion || !(o->motion->state.broken_hinges&(1u<<hinge)));shot++) {
        int s=shot%4;
        b3Vec3 local=swat_v(-.3f,h.y+(s&1?1:-1)*.035f*(o->half.y/1.095f),h.z+(s&2?1:-1)*.008f*(o->half.z/.54f));
        b3Quat q=swat_object_rotation(o);b3Pos p=b3OffsetPos(o->center,b3RotateVector(q,local));
        swat_sim_shoot(&sim,0,p,b3RotateVector(q,swat_v(1,0,0)),(SwatShot){.fired=true,.damage=34,.energy=60,.range=2});
    }
    assert(o->motion && (o->motion->state.broken_hinges&(1u<<hinge)));
}
static void support_ticks(int count) {
    SwatInput neutral=swat_neutral_input();for(int t=0;t<count;t++)swat_sim_step(&sim,&neutral);
}
static Image support_cached_image(SwatView* view,Camera3D camera) {
    static SwatSnapshot a,b;swat_capture_snapshot(&sim,1,&a);
    swat_view_prepare_shadows(view,&sim,camera.position,false);
    Image cached=room101_capture_prepared(view,camera,true,1200,720),full=sample_capture(view,camera);
    Color* p=LoadImageColors(cached),*q=LoadImageColors(full);int mismatches=0;
    for(int i=0;i<1200*720;i++)mismatches+=abs(p[i].r-q[i].r)+abs(p[i].g-q[i].g)+abs(p[i].b-q[i].b)>3;
    assert(mismatches<10);UnloadImageColors(p);UnloadImageColors(q);UnloadImage(full);
    swat_capture_snapshot(&sim,1,&b);assert(!memcmp(&a,&b,sizeof(a)));
    return cached;
}
static void support_capture(SwatView* view,Camera3D camera,const char* directory,const char* name) {
    Image image=support_cached_image(view,camera);sample_export(image,directory,name);UnloadImage(image);
}
static Image support_leaf_image(SwatView* view,Camera3D camera,const SwatObject* door) {
    RenderTexture2D target=LoadRenderTexture(600,900);assert(target.id);
    BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    motel_piece(&view->environment,&sim.world,door,false,false);
    EndMode3D();EndTextureMode();
    Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);UnloadRenderTexture(target);return image;
}
static void support_viewer_attachment(SwatView* view,const SwatObject* door,const char* directory) {
    b3Quat q=swat_object_rotation(door);b3Vec3 normal=b3RotateVector(q,swat_v(1,0,0)),up=b3RotateVector(q,swat_v(0,1,0));
    Vector3 center={door->center.x,door->center.y,door->center.z};
    Camera3D camera={Vector3Add(center,(Vector3){-2.5f*normal.x,-2.5f*normal.y,-2.5f*normal.z}),center,{up.x,up.y,up.z},2*door->half.y+.2f,CAMERA_ORTHOGRAPHIC};
    Image shown=support_leaf_image(view,camera,door);
    Model* viewer=&view->environment.motel_dressing[5];assert(viewer->meshCount);
    BoundingBox source=GetModelBoundingBox(*viewer);Material* borrowed=viewer->materials;
    Material* hidden=calloc((size_t)viewer->materialCount,sizeof(*hidden));assert(hidden);
    for(int i=0;i<viewer->materialCount;i++) {
        hidden[i]=borrowed[i];hidden[i].maps=malloc((MATERIAL_MAP_BRDF+1)*sizeof(MaterialMap));assert(hidden[i].maps);
        memcpy(hidden[i].maps,borrowed[i].maps,(MATERIAL_MAP_BRDF+1)*sizeof(MaterialMap));hidden[i].maps[MATERIAL_MAP_ALBEDO].color.a=0;
    }
    viewer->materials=hidden;Image absent=support_leaf_image(view,camera,door);viewer->materials=borrowed;
    for(int i=0;i<viewer->materialCount;i++)free(hidden[i].maps);
    free(hidden);
    // Expected position comes directly from the frozen source viewer at
    // (X=.54,Y=1.6,Z=.0345), transformed by the achieved physical body.
    b3Pos origin=b3OffsetPos(door->center,b3RotateVector(q,swat_v(-.0345f,1.6f-door->half.y,0)));
    b3Quat detail=b3MulQuat(q,swat_rotation(-SWAT_PI*.5f,0));
    Vector2 low={INFINITY,INFINITY},high={-INFINITY,-INFINITY};
    for(int v=0;v<8;v++) {
        b3Vec3 local=swat_v(v&1?source.max.x:source.min.x,v&2?source.max.y:source.min.y,v&4?source.max.z:source.min.z);
        b3Pos p=b3OffsetPos(origin,b3RotateVector(detail,local));
        Vector2 screen=GetWorldToScreenEx((Vector3){p.x,p.y,p.z},camera,600,900);
        low.x=fminf(low.x,screen.x);low.y=fminf(low.y,screen.y);high.x=fmaxf(high.x,screen.x);high.y=fmaxf(high.y,screen.y);
    }
    Color* a=LoadImageColors(shown),*b=LoadImageColors(absent);int pixels=0;
    for(int y=0;y<900;y++)for(int x=0;x<600;x++)if(memcmp(&a[y*600+x],&b[y*600+x],sizeof(Color))) {
        pixels++;assert(x>=floorf(low.x)-2 && x<=ceilf(high.x)+2 && y>=floorf(low.y)-2 && y<=ceilf(high.y)+2);
    }
    assert(pixels>10);sample_export(shown,directory,"support-viewer-attached");
    UnloadImageColors(a);UnloadImageColors(b);UnloadImage(shown);UnloadImage(absent);
    printf("PASS fallen door viewer: %d rendered detail pixels follow the independently transformed source attachment\n",pixels);
}
static void door_support_graphics(SwatView* view,const char* directory) {
    static SwatMap map;static SwatSnapshot state;
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;cfg.max_ticks=30000;
    swat_sim_init(&sim,cfg,81);sim.extraction=(b3Pos){500,0,500};
    for(int i=1;i<sim.actor_count;i++)if(sim.actors[i].present){b3DestroyBody(sim.actors[i].controller.body.body);memset(&sim.actors[i],0,sizeof(sim.actors[i]));}
    swat_environment_art_prepare_location(&view->environment,&sim.world);
    const int owner=16;SwatObject* o=&sim.world.objects[owner];assert(o->door && o->active);
    b3BodyId body=o->body;b3Pos standing=o->center;b3Quat closed=swat_object_rotation(o);
    b3Vec3 normal=b3RotateVector(closed,swat_v(1,0,0)),side=b3RotateVector(closed,swat_v(0,0,1));
    Vector3 center={standing.x,standing.y,standing.z};
    Camera3D camera={Vector3Add(center,(Vector3){normal.x*-3.0f+side.x*.35f,.35f,normal.z*-3.0f+side.z*.35f}),center,{0,1,0},48,CAMERA_PERSPECTIVE};
    support_capture(view,camera,directory,"support-0-intact");
    assert(swat_world_break_latch(&sim.world,owner) && !o->door_open);
    support_hinge_shots(owner,0);support_capture(view,camera,directory,"support-1-two-hinges");
    support_hinge_shots(owner,1);support_ticks(150);
    assert(swat_door_dynamic(o) && !swat_door_loose(o));
    support_capture(view,camera,directory,"support-2-hanging");
    sample_gap_pixels(view,o,o->center,o->half,0,"hanging",directory);
    unsigned int builds=swat_environment_door_stats(&view->environment).builds;
    support_ticks(15);support_capture(view,camera,directory,"support-3-hanging-later");
    assert(swat_environment_door_stats(&view->environment).builds==builds);
    support_hinge_shots(owner,2);assert(swat_door_loose(o));
    support_ticks(15);support_capture(view,camera,directory,"support-4-falling");
    builds=swat_environment_door_stats(&view->environment).builds;
    support_ticks(600);assert(o->active && B3_ID_EQUALS(body,o->body) && !b3Body_IsAwake(o->body));
    Vector3 fallen={o->center.x,o->center.y,o->center.z};
    Camera3D floor_view={Vector3Add(fallen,(Vector3){normal.x*-2.5f+side.x*.8f,1.8f,normal.z*-2.5f+side.z*.8f}),fallen,{0,1,0},48,CAMERA_PERSPECTIVE};
    Image settled=support_cached_image(view,floor_view);sample_export(settled,directory,"support-5-settled");
    assert(swat_environment_door_stats(&view->environment).builds==builds);
    sample_gap_pixels(view,o,o->center,o->half,0,"fallen",directory);
    support_viewer_attachment(view,o,directory);
    // A newly joined viewer reconstructs the canonical source and the exact
    // achieved quaternion, cut geometry and mounted details from state.
    swat_capture_map(&sim,1,&map);swat_capture_snapshot(&sim,1,&state);
    swat_sim_close(&sim);swat_apply_map(&sim,&map);assert(swat_apply_snapshot(&sim,&state));
    Image replica=support_cached_image(view,floor_view);assert(!impact_pixel_difference(settled,replica));
    sample_export(replica,directory,"support-6-late-replica");UnloadImage(replica);UnloadImage(settled);
    assert(swat_environment_door_stats(&view->environment).builds==builds);
    swat_sim_close(&sim);
    puts("PASS door support graphics: original three-hinge source/maps/details, achieved hanging/falling/settled transforms, front/back ray silhouettes, seven cached/full shadow comparisons, stable meshes during motion, pixel-identical late replica and immutable authority");
}
