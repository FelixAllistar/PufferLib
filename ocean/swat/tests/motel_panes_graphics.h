// Keep this selector as a regression for the pre-R1 map recipe.
static void pane_legacy_world(void){
    static SwatMap map;static SwatSnapshot state;
    swat_capture_map(&sim,1,&map);swat_capture_snapshot(&sim,1,&state);
    map.count=SWAT_MOTEL_WINDOWS_FIRST;state.object_count=map.count;
    const int parents[]={6,17,41,65,89};
    for(int i=0;i<5;i++)state.objects[parents[i]].active=true;
    for(int i=0;i<10;i++){int id=SWAT_MOTEL_PANES_FIRST+i;state.objects[id].active=true;state.objects[id].health=map.objects[id].max_health;}
    swat_apply_map(&sim,&map);assert(sim.world.motel && swat_apply_snapshot(&sim,&state));
}
static bool pane_ray(int parent,Ray ray) {
    int owners[]={parent,SWAT_MOTEL_PANES_FIRST+2*swat_motel_window_index(parent),SWAT_MOTEL_PANES_FIRST+2*swat_motel_window_index(parent)+1};
    for(int i=0;i<3;i++) {
        SwatObject* o=&sim.world.objects[owners[i]];if(!o->active)continue;
        b3ShapeId shapes[16];int count=b3Body_GetShapes(o->body,shapes,16);assert(count>0 && count<16);
        for(int k=0;k<count;k++)if(b3Shape_RayCast(shapes[k],(b3Pos){ray.position.x,ray.position.y,ray.position.z},swat_mul(swat_v(ray.direction.x,ray.direction.y,ray.direction.z),3)).hit)return true;
    }
    return false;
}
static Image pane_capture(SwatEnvironmentArt* art,int parent,Camera3D camera) {
    RenderTexture2D target=LoadRenderTexture(512,512);BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    assert(swat_environment_motel_draw(art,&sim.world,&sim.world.objects[parent],false,false));
    int first=SWAT_MOTEL_PANES_FIRST+2*swat_motel_window_index(parent);
    for(int k=0;k<2;k++)assert(swat_environment_motel_draw(art,&sim.world,&sim.world.objects[first+k],false,false));
    EndMode3D();EndTextureMode();Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);UnloadRenderTexture(target);return image;
}
static void motel_panes_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;cfg.randomize=false;
    swat_sim_init(&sim,cfg,81);pane_legacy_world();SwatEnvironmentArt* art=&view->environment;swat_environment_art_prepare_location(art,&sim.world);
    bool prepared=view->lighting.prepared;view->lighting.prepared=false;
    bool ready=art->room101_ready;art->room101_ready=false;int checked=0,open=0,filled=0;
    for(int bay=0;bay<2;bay++)for(int stage=0;stage<3;stage++) {
        swat_sim_reset(&sim);pane_legacy_world();int parent=bay?17:6,asset=bay?10:12,first=SWAT_MOTEL_PANES_FIRST+2*bay;
        for(int k=0;k<stage;k++)assert(swat_world_damage(&sim.world,first+k,10000));
        before=sim.world;
        Model source=art->motel[asset];
        const SwatObject* o=&sim.world.objects[parent];Vector3 center={o->center.x,o->center.y,o->center.z};
        for(int fallback=0;fallback<2;fallback++)for(int side=0;side<2;side++) {
            art->motel[asset]=fallback?(Model){0}:source;
            Camera3D camera={Vector3Add(center,(Vector3){0,0,side?-1.5f:1.5f}),center,{0,1,0},bay?2.25f:4.5f,CAMERA_ORTHOGRAPHIC};
            Image image=pane_capture(art,parent,camera);Color* colors=LoadImageColors(image);
            for(int y=4;y<508;y+=4)for(int x=4;x<508;x+=4) {
                const int offsets[][2]={{0,0},{-2,0},{2,0},{0,-2},{0,2}};bool stable=true,hit=false;
                for(int j=0;j<5;j++) {
                    Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[j][0],y+.5f+offsets[j][1]},camera,512,512);
                    bool sample=pane_ray(parent,ray);if(!j)hit=sample;else stable&=hit==sample;
                }
                if(!stable)continue;
                Color p=colors[y*512+x];bool visible=p.r!=MAGENTA.r || p.g!=MAGENTA.g || p.b!=MAGENTA.b;
                if(visible!=hit)fprintf(stderr,"Window%d stage%d fallback%d side%d pixel%d,%d raster%d ray%d\n",parent,stage,fallback,side,x,y,visible,hit);
                assert(visible==hit);checked++;filled+=hit;open+=!hit;
            }
            char path[4096];snprintf(path,sizeof(path),"%s/window-%d-stage%d-side%d-%s.png",directory,parent,stage,side,fallback?"fallback":"art");assert(ExportImage(image,path));
            UnloadImageColors(colors);UnloadImage(image);
        }
        art->motel[asset]=source;assert(!memcmp(&before,&sim.world,sizeof(before)));
    }
    art->room101_ready=ready;
    // Room 101 replacement primitives are reordered, but retain the exact
    // original 44-triangle glazing per pane. Check each replacement separately.
    const Model* variants[]={&art->room101_v3[0],&art->room101_v4[7]};
    for(int version=0;version<2;version++) {
        const Model* source=variants[version];assert(source->meshCount);int counts[2]={0};
        for(int m=0;m<source->meshCount;m++) {
            const Mesh* mesh=&source->meshes[m];
            for(int t=0;t<mesh->triangleCount;t++) {
                b3Vec3 v[3];for(int j=0;j<3;j++){int k=mesh->indices?mesh->indices[t*3+j]:t*3+j;v[j]=swat_v(mesh->vertices[k*3],mesh->vertices[k*3+1],mesh->vertices[k*3+2]);}
                int pane=swat_motel_pane_triangle(10,v[0],v[1],v[2]);if(pane>=0)counts[pane]++;
            }
        }
        assert(counts[0]==44 && counts[1]==44);
        swat_sim_reset(&sim);pane_legacy_world();bool v4=art->room101_v4_ready;art->room101_v4_ready=version==1;
        const SwatObject* o=&sim.world.objects[17];Vector3 center={o->center.x,o->center.y,o->center.z};
        Camera3D camera={Vector3Add(center,(Vector3){0,0,1.5f}),center,{0,1,0},2.25f,CAMERA_ORTHOGRAPHIC};
        Image full=pane_capture(art,17,camera);assert(swat_world_damage(&sim.world,SWAT_MOTEL_PANES_FIRST+2,10000));
        Image broken=pane_capture(art,17,camera);Color* a=LoadImageColors(full),*b=LoadImageColors(broken);int changed=0;
        for(int i=0;i<512*512;i++)changed+=memcmp(&a[i],&b[i],sizeof(Color))!=0;
        assert(changed>1000);
        char path[4096];snprintf(path,sizeof(path),"%s/window-room101-v%d-left-broken.png",directory,version+3);assert(ExportImage(broken,path));
        UnloadImageColors(a);UnloadImageColors(b);UnloadImage(full);UnloadImage(broken);art->room101_v4_ready=v4;
    }
    assert(open>1000 && filled>1000);view->lighting.prepared=prepared;swat_sim_close(&sim);
    printf("PASS motel pane graphics: %d stable original/fallback ray-raster samples, both faces and three damage states; v3/v4 preserve and remove the matching 44-triangle pane; immutable authority\n",checked);
}
