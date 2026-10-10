static Image window_r1_capture(SwatEnvironmentArt* art,int parent,Camera3D camera){
    RenderTexture2D target=LoadRenderTexture(384,384);BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    assert(swat_environment_motel_draw(art,&sim.world,&sim.world.objects[parent],false,false));
    int first=swat_motel_window_first(parent),count=swat_motel_window_model(parent==6?1:0)->part_count;
    for(int i=0;i<count;i++)assert(swat_environment_motel_draw(art,&sim.world,&sim.world.objects[first+i],false,false));
    EndMode3D();EndTextureMode();Image result=LoadImageFromTexture(target.texture);ImageFlipVertical(&result);UnloadRenderTexture(target);return result;
}
static void motel_windows_graphics(SwatView* view,const char* directory){
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;swat_sim_init(&sim,cfg,81);
    SwatEnvironmentArt* art=&view->environment;swat_environment_art_prepare_location(art,&sim.world);bool prepared=view->lighting.prepared;view->lighting.prepared=false;
    int checked=0,filled=0,empty=0;
    for(int kind=0;kind<2;kind++){
        int parent=kind?6:17,first=swat_motel_window_first(parent);const SwatWindowModel* physical=swat_motel_window_model(kind);Model source=art->motel_windows[kind];
        assert(source.meshCount==(kind?8:7) && source.materialCount==(kind?6:7));int triangles=0;
        for(int m=0;m<source.meshCount;m++)triangles+=source.meshes[m].triangleCount;
        assert(triangles==physical->triangle_count);
        for(int state=0;state<3;state++){
            swat_sim_reset(&sim);
            if(state==1){int piece=swat_motel_window_find(&sim.world,parent,kind?"lobby_glass_left_lower":"room_glass_left",SWAT_OPAQUE_GLASS);assert(piece>=0 && swat_world_damage(&sim.world,piece,10000));}
            if(state==2){int piece=swat_motel_window_find(&sim.world,parent,kind?"middle_post":"left_jamb",SWAT_ALUMINUM);assert(piece>=0 && swat_world_damage(&sim.world,piece,10000));}
            before=sim.world;
            const SwatObject* anchor=&sim.world.objects[parent];Vector3 center={anchor->center.x,anchor->center.y,anchor->center.z};float yaw=swat_motel_instance(parent-1)->yaw;
            Vector3 normal={sinf(yaw),0,cosf(yaw)},tangent={cosf(yaw),0,-sinf(yaw)};
            for(int fallback=0;fallback<2;fallback++)for(int side=0;side<3;side++){
                art->motel_windows[kind]=fallback?(Model){0}:source;
                Vector3 direction=side==0?normal:side==1?Vector3Negate(normal):Vector3Normalize(Vector3Add(normal,Vector3Scale(tangent,.65f)));
                Camera3D camera={Vector3Add(center,Vector3Scale(direction,3)),center,{0,1,0},kind?4.5f:2.4f,CAMERA_ORTHOGRAPHIC};
                Image image=window_r1_capture(art,parent,camera);Color* pixels=LoadImageColors(image);
                for(int y=4;y<380;y+=8)for(int x=4;x<380;x+=8){
                    bool hit=false,stable=true;const int offsets[][2]={{0,0},{-2,0},{2,0},{0,-2},{0,2}};
                    for(int j=0;j<5;j++){
                        Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[j][0],y+.5f+offsets[j][1]},camera,384,384);bool sample=false;
                        for(int i=0;i<physical->part_count;i++){
                            const SwatObject* o=&sim.world.objects[first+i];if(!o->active)continue;b3ShapeId shapes[64];int n=b3Body_GetShapes(o->body,shapes,64);assert(n<64);
                            for(int k=0;k<n;k++)if(b3Shape_RayCast(shapes[k],(b3Pos){ray.position.x,ray.position.y,ray.position.z},swat_mul(swat_v(ray.direction.x,ray.direction.y,ray.direction.z),6)).hit){sample=true;break;}
                            if(sample)break;
                        }
                        if(!j)hit=sample;else stable&=sample==hit;
                    }
                    if(!stable)continue;
                    Color c=pixels[y*384+x];bool visible=c.r!=MAGENTA.r || c.g!=MAGENTA.g || c.b!=MAGENTA.b;
                    if(visible!=hit)fprintf(stderr,"R1 kind%d state%d side%d fallback%d x%d y%d visible%d hit%d\n",kind,state,side,fallback,x,y,visible,hit);
                    assert(visible==hit);checked++;filled+=visible;empty+=!visible;
                }
                if(side==0){char path[4096];snprintf(path,sizeof(path),"%s/window-%d-state-%d-%s.png",directory,kind,state,fallback?"fallback":"art");assert(ExportImage(image,path));}
                UnloadImageColors(pixels);UnloadImage(image);assert(!memcmp(&before,&sim.world,sizeof(before)));
            }
            art->motel_windows[kind]=source;
        }
    }
    view->lighting.prepared=prepared;swat_sim_reset(&sim);
    Camera3D camera={{-5.42f,1.64f,2.3f},{-6,1.56f,0},{0,1,0},62,CAMERA_PERSPECTIVE};Image image=room101_capture_size(view,camera,true,1440,810);
    char path[4096];snprintf(path,sizeof(path),"%s/windows-scene.png",directory);assert(ExportImage(image,path));UnloadImage(image);
    int pane=swat_motel_window_find(&sim.world,17,"room_glass_left",SWAT_OPAQUE_GLASS);assert(swat_world_damage(&sim.world,pane,10000));
    image=room101_capture_size(view,camera,true,1440,810);snprintf(path,sizeof(path),"%s/windows-broken-scene.png",directory);assert(ExportImage(image,path));UnloadImage(image);
    assert(filled>1000 && empty>1000);swat_sim_close(&sim);
    printf("PASS revised-window graphics: %d stable ray/raster samples, original/fallback front/back/oblique, independent pane and support loss, source mesh/material counts, immutable authority, production lighting/shadows\n",checked);
}
