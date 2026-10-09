// Source art and collision must agree through the trolley's open bays.
static void motel_props_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);SwatEnvironmentArt* art=&view->environment;
    swat_environment_art_prepare_location(art,&sim.world);before=sim.world;
    Model source=art->motel[45];assert(source.meshCount==1 && source.meshes[0].triangleCount==2416 && source.materialCount==2);
    Material material=source.materials[1];
    assert(material.maps[MATERIAL_MAP_ALBEDO].texture.width==512 && material.maps[MATERIAL_MAP_ROUGHNESS].texture.width==512);
    SwatObject* o=&sim.world.objects[SWAT_MOTEL_PROPS_FIRST];
    int parts=b3Body_GetShapeCount(o->body);b3ShapeId shapes[64];assert(parts>1 && parts<64 && b3Body_GetShapes(o->body,shapes,64)==parts);
    bool prepared=view->lighting.prepared;view->lighting.prepared=false;
    const Vector3 directions[]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0}};
    int checked=0,filled=0,empty=0;
    for(int fallback=0;fallback<2;fallback++)for(int side=0;side<5;side++) {
        art->motel[45]=fallback?(Model){0}:source;
        Vector3 center={o->center.x,o->center.y,o->center.z};
        Camera3D camera={Vector3Add(center,Vector3Scale(directions[side],1.5f)),center,side==4?(Vector3){0,0,-1}:(Vector3){0,1,0},1.12f,CAMERA_ORTHOGRAPHIC};
        RenderTexture2D target=LoadRenderTexture(512,512);
        BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
        assert(swat_environment_motel_draw(art,&sim.world,o,false,false));EndMode3D();EndTextureMode();
        Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);Color* pixels=LoadImageColors(image);
        for(int y=4;y<508;y+=4)for(int x=4;x<508;x+=4) {
            bool hit=false,stable=true;const int offsets[][2]={{0,0},{-2,0},{2,0},{0,-2},{0,2}};
            for(int j=0;j<5;j++) {
                Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[j][0],y+.5f+offsets[j][1]},camera,512,512);
                bool sample=false;
                for(int part=0;part<parts;part++)sample|=b3Shape_RayCast(shapes[part],(b3Pos){ray.position.x,ray.position.y,ray.position.z},swat_mul(swat_v(ray.direction.x,ray.direction.y,ray.direction.z),3)).hit;
                if(!j)hit=sample;else stable&=sample==hit;
            }
            if(!stable)continue;
            Color p=pixels[y*512+x];bool visible=p.r!=MAGENTA.r || p.g!=MAGENTA.g || p.b!=MAGENTA.b;
            if(visible!=hit)fprintf(stderr,"trolley raster side%d fallback%d pixel%d,%d visible%d hit%d\n",side,fallback,x,y,visible,hit);
            assert(visible==hit);checked++;filled+=visible;empty+=!visible;
        }
        char path[4096];snprintf(path,sizeof(path),"%s/trolley-%d-%s.png",directory,side,fallback?"fallback":"art");assert(ExportImage(image,path));
        UnloadImageColors(pixels);UnloadImage(image);UnloadRenderTexture(target);
    }
    art->motel[45]=source;view->lighting.prepared=prepared;
    assert(filled>1000 && empty>1000 && !memcmp(&before,&sim.world,sizeof(before)));
    Camera3D camera={{-10.2f,1.5f,-7.65f},{-8.75f,.48f,-9.35f},{0,1,0},65,CAMERA_PERSPECTIVE};
    Image image=room101_capture_size(view,camera,true,1440,810);char path[4096];
    snprintf(path,sizeof(path),"%s/trolley-utility-scene.png",directory);assert(ExportImage(image,path));UnloadImage(image);
    assert(room101_owner_pixels(art,o,false)>100);
    assert(swat_world_damage(&sim.world,o->tag.index,10000));assert(!room101_owner_pixels(art,o,false));
    swat_sim_close(&sim);
    printf("PASS trolley graphics: %d ray/raster samples on five faces with original art and exact fallback, two 512px PBR maps, native utility lighting/shadows and authoritative removal\n",checked);
}
