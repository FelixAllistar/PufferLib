// Explicit graphics check: requires a working display/OpenGL context.
#include "environment_art.h"
#include "render.h"
#include "protocol.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void environment(const char* key,const char* value) {
#ifdef _WIN32
    assert(_putenv_s(key,value ? value : "")==0);
#else
    if(value) assert(setenv(key,value,1)==0); else assert(unsetenv(key)==0);
#endif
}
static SwatSim sim;
static SwatWorld before;
static SwatWorld prop_world;

static void fixture_u32(FILE* file,uint32_t value) {
    for(int i=0;i<4;i++) assert(fputc((int)((value>>(8*i))&255),file)!=EOF);
}
static void write_prop_fixture(const char* path,float bottom,float top) {
    // Valid GLB triangle: exercise geometry contract rejection, not parse errors.
    char json[1024];
    snprintf(json,sizeof(json),"{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"buffers\":[{\"byteLength\":36}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[-0.01,%.9g,-0.01],\"max\":[0.01,%.9g,0.01]}]}",bottom,top);
    uint32_t length=(uint32_t)strlen(json),padded=(length+3u)&~3u;
    FILE* file=fopen(path,"wb"); assert(file);
    fixture_u32(file,0x46546c67u); fixture_u32(file,2); fixture_u32(file,12+8+padded+8+36);
    fixture_u32(file,padded); fixture_u32(file,0x4e4f534au);
    assert(fwrite(json,1,length,file)==length);
    for(uint32_t i=length;i<padded;i++) assert(fputc(' ',file)!=EOF);
    fixture_u32(file,36); fixture_u32(file,0x004e4942u);
    float vertices[9]={-.01f,bottom,-.01f,.01f,bottom,.01f,0,top,0};
    for(int i=0;i<9;i++) { uint32_t bits; memcpy(&bits,&vertices[i],4); fixture_u32(file,bits); }
    assert(!fclose(file));
}

static size_t prop_pixels(SwatEnvironmentArt* art,const SwatEnvironmentProp* prop,const char* capture) {
    memcpy(&prop_world,&sim.world,sizeof(prop_world));
    for(int i=0;i<prop_world.count;i++) if(i!=prop->support) prop_world.objects[i].active=false;
    SwatEnvironmentArt selected=*art;
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) if(i!=(int)prop->kind) selected.props[i]=(Model){0};
    const SwatObject* support=&prop_world.objects[prop->support];
    Vector3 target={(float)support->center.x+prop->local.x,(float)support->center.y+prop->local.y,
        (float)support->center.z+prop->local.z};
    Camera3D camera={{target.x+.5f,target.y+.5f,target.z+.5f},target,{0,1,0},.4f,CAMERA_ORTHOGRAPHIC};
    RenderTexture2D texture=LoadRenderTexture(384,384);
    BeginTextureMode(texture); ClearBackground(MAGENTA); BeginMode3D(camera);
    swat_environment_art_draw_props(&selected,&prop_world,&sim.layout);
    EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(texture.texture); ImageFlipVertical(&image);
    if(capture) assert(ExportImage(image,capture));
    Color* colors=LoadImageColors(image); size_t pixels=0;
    for(int i=0;i<image.width*image.height;i++)
        if(colors[i].r!=MAGENTA.r || colors[i].g!=MAGENTA.g || colors[i].b!=MAGENTA.b) pixels++;
    UnloadImageColors(colors); UnloadImage(image); UnloadRenderTexture(texture);
    return pixels;
}

static void capture_tabletop(SwatEnvironmentArt* art,const SwatEnvironmentProp* prop,const char* capture) {
    const SwatObject* support=&sim.world.objects[prop->support];
    const SwatPlanRoom* room=&sim.layout.rooms[prop->room];
    Vector3 target={(float)support->center.x,(float)support->center.y+support->half.y,(float)support->center.z};
    Camera3D camera={{target.x+(target.x<(room->x0+room->x1)*.5f ? 1 : -1),target.y+1.25f,
        target.z+(target.z<(room->z0+room->z1)*.5f ? 1 : -1)},target,{0,1,0},48,CAMERA_PERSPECTIVE};
    RenderTexture2D texture=LoadRenderTexture(960,640);
    BeginTextureMode(texture); ClearBackground((Color){57,76,86,255}); BeginMode3D(camera);
    for(int i=0;i<sim.world.count;i++) {
        const SwatObject* o=&sim.world.objects[i]; if(!o->active) continue;
        rlPushMatrix(); rlTranslatef(o->center.x,o->center.y,o->center.z);
        rlRotatef(o->yaw/SWAT_RAD,0,1,0); rlRotatef(o->pitch/SWAT_RAD,0,0,1);
        if(!swat_environment_art_draw(art,o)) DrawCube((Vector3){0},2*o->half.x,2*o->half.y,2*o->half.z,GRAY);
        rlPopMatrix();
    }
    swat_environment_art_draw_props(art,&sim.world,&sim.layout);
    EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(texture.texture); ImageFlipVertical(&image);
    assert(ExportImage(image,capture)); UnloadImage(image); UnloadRenderTexture(texture);
}

static void prop_graphics(SwatEnvironmentArt* art,const char* directory) {
    unsigned seen=0; char path[4096];
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) assert(art->props[i].meshCount);
    SwatConfig config=swat_default_config(); config.mission=SWAT_GENERATED; config.hostile_fire=false;
    for(int seed=0;seed<32 && seen!=(1u<<SWAT_ENV_PROP_KINDS)-1;seed++) {
        config.layout_seed=(uint32_t)seed; swat_sim_init(&sim,config,42);
        SwatEnvironmentProp props[SWAT_ENV_PROP_MAX];
        int count=swat_environment_props(&sim.world,&sim.layout,props);
        for(int i=0;i<count;i++) if(!(seen&(1u<<props[i].kind))) {
            const SwatEnvironmentProp* p=&props[i];
            snprintf(path,sizeof(path),"%s/environment-prop-%d.png",directory,p->kind);
            assert(prop_pixels(art,p,path)>100);
            snprintf(path,sizeof(path),"%s/environment-tabletop-%d.png",directory,p->kind/2);
            capture_tabletop(art,p,path);
            // Omitting just this model does not substitute opaque fake clutter.
            SwatEnvironmentArt missing=*art; missing.props[p->kind]=(Model){0};
            assert(prop_pixels(&missing,p,NULL)==0);
            // Removing the real support hides its child art in the very next draw.
            bool active=sim.world.objects[p->support].active;
            sim.world.objects[p->support].active=false;
            assert(prop_pixels(art,p,NULL)==0);
            sim.world.objects[p->support].active=active;
            seen|=1u<<p->kind;
        }
        swat_sim_close(&sim);
    }
    assert(seen==(1u<<SWAT_ENV_PROP_KINDS)-1);
    puts("PASS prop graphics: all six imported models rendered at metre scale, generated tabletop captures, per-asset fallback and support visibility");
}
static size_t object_pixels(SwatEnvironmentArt* art,SwatObject* object,const char* capture) {
    RenderTexture2D target=LoadRenderTexture(512,512);
    Camera3D camera={{3,2,3},{0,0,0},{0,1,0},3,CAMERA_ORTHOGRAPHIC};
    BeginTextureMode(target); ClearBackground(MAGENTA); BeginMode3D(camera);
    swat_environment_art_draw(art,object);
    EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image);
    if(capture) assert(ExportImage(image,capture));
    Color* pixels=LoadImageColors(image); size_t visible=0;
    for(int i=0;i<image.width*image.height;i++)
        if(pixels[i].r!=MAGENTA.r || pixels[i].g!=MAGENTA.g || pixels[i].b!=MAGENTA.b) visible++;
    UnloadImageColors(pixels); UnloadImage(image); UnloadRenderTexture(target);
    return visible;
}
static size_t wall_pixels(SwatEnvironmentArt* art,const SwatWorld* wall,const char* capture) {
    RenderTexture2D target=LoadRenderTexture(640,640);
    Camera3D camera={{5,1.35f,0},{0,1.35f,0},{0,1,0},3.5f,CAMERA_ORTHOGRAPHIC};
    BeginTextureMode(target); ClearBackground(MAGENTA); BeginMode3D(camera);
    for(int i=0;i<wall->count;i++) {
        const SwatObject* o=&wall->objects[i]; if(!o->active) continue;
        rlPushMatrix(); rlTranslatef(o->center.x,o->center.y,o->center.z);
        rlRotatef(o->yaw/SWAT_RAD,0,1,0); rlRotatef(o->pitch/SWAT_RAD,0,0,1);
        assert(swat_environment_art_draw(art,o)); rlPopMatrix();
    }
    EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image);
    assert(ExportImage(image,capture)); Color* pixels=LoadImageColors(image); size_t holes=0;
    for(int i=0;i<image.width*image.height;i++)
        if(pixels[i].r==MAGENTA.r && pixels[i].g==MAGENTA.g && pixels[i].b==MAGENTA.b) holes++;
    UnloadImageColors(pixels); UnloadImage(image); UnloadRenderTexture(target);
    return holes;
}
static void destruction_pixels(SwatEnvironmentArt* art,const char* directory) {
    SwatWorld wall={0}; swat_world_init(&wall);
    swat_build_framed_wall(&wall,(b3Pos){0},0,3,2.7f,0,0,0,0,true);
    char path[4096]; snprintf(path,sizeof(path),"%s/environment-wall-intact.png",directory);
    size_t intact=wall_pixels(art,&wall,path);
    for(int side=1;side>=-1;side-=2) {
        for(int i=0;i<wall.count;i++) {
            SwatObject* o=&wall.objects[i];
            if(o->part==SWAT_PART_SKIN && side*o->center.x>0 && fabs(o->center.z)<.7 && o->center.y>.8 && o->center.y<1.9)
                assert(swat_world_damage(&wall,i,10000));
        }
        snprintf(path,sizeof(path),"%s/environment-wall-%s.png",directory,side==1 ? "front-removed" : "breached");
        size_t holes=wall_pixels(art,&wall,path);
        if(side==1) assert(holes==intact); // Opposite face still owns real cover.
        else assert(holes>intact+1000); // Hole appears only after both faces break.
    }
    for(int i=0;i<wall.count;i++) if(wall.objects[i].part==SWAT_PART_FRAME) assert(wall.objects[i].active);
    swat_world_close(&wall);
}
static void glass_graphics(SwatEnvironmentArt* art,const char* directory) {
    static SwatWorld world; swat_world_init(&world);swat_storefront_build(&world);
    swat_environment_art_prepare_location(art,&world);assert(art->location==2);
    RenderTexture2D target=LoadRenderTexture(256,256);assert(target.id);
    Camera3D camera={{1,1.5f,3},{1,1.5f,-1},{0,1,0},40,CAMERA_PERSPECTIVE};
    for(int late=0;late<2;late++) {
        BeginTextureMode(target);ClearBackground(BLACK);BeginMode3D(camera);
        for(int i=1;i<world.count;i++) assert(swat_environment_storefront_draw(art,&world,&world.objects[i],false,false));
        if(!late) DrawCube((Vector3){1,1.5f,-1},1,1,1,RED);
        swat_environment_art_transparent(art,&world,camera.position,false);
        // A late opaque probe behind the window must pass depth testing: the
        // blended pass must neither write pane depth nor leave writes disabled.
        if(late) {
            DrawCube((Vector3){1,1.5f,-1},1,1,1,RED);
            // Depth writes must be restored: this farther opaque probe must
            // remain hidden behind the red probe that was just drawn.
            DrawCube((Vector3){1,1.5f,-2},1,1,1,BLUE);
        }
        EndMode3D();EndTextureMode();
        Image image=LoadImageFromTexture(target.texture);Color pixel=GetImageColor(image,128,128);
        printf("glass interior probe late=%d rgb=%d,%d,%d\n",late,pixel.r,pixel.g,pixel.b);
        assert(pixel.r>pixel.g+80 && pixel.r>pixel.b+80);
        char path[4096];snprintf(path,sizeof(path),"%s/environment-glass-%d.png",directory,late);assert(ExportImage(image,path));UnloadImage(image);
    }
    UnloadRenderTexture(target);swat_world_close(&world);
}
static void room101_shadow(void* context,const SwatSim* s,bool cutaway) {
    SwatEnvironmentArt* art=context;
    for(int i=1;i<s->world.count;i++) swat_environment_motel_draw(art,&s->world,&s->world.objects[i],true,cutaway);
}
static Image room101_capture(SwatView* view,Camera3D camera,bool lit) {
    SwatEnvironmentArt* art=&view->environment; SwatLighting* light=&view->lighting;
    light->prepared=false;
    if(lit) swat_lighting_prepare_context(light,&sim,camera.position,false,room101_shadow,art);
    RenderTexture2D target=LoadRenderTexture(960,800); assert(target.id);
    BeginTextureMode(target); ClearBackground(MAGENTA); BeginMode3D(camera);
    if(lit) swat_lighting_begin(light,art,&sim.world,camera.position);
    for(int i=1;i<sim.world.count;i++) assert(swat_environment_motel_draw(art,&sim.world,&sim.world.objects[i],false,false));
    swat_environment_art_transparent(art,&sim.world,camera.position,false);
    if(lit) swat_lighting_end(light,art);
    EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image); UnloadRenderTexture(target);
    return image;
}
static void room101_graphics(SwatView* view,const char* directory) {
    SwatEnvironmentArt* art=&view->environment;
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.hostile_fire=false;
    swat_sim_init(&sim,config,73); swat_environment_art_prepare_location(art,&sim.world);
    assert(art->room101_ready);
    for(int i=0;i<SWAT_ROOM101_ASSETS;i++) assert(art->room101[i].meshCount);
    assert(fabsf(art->room101_normal_scale[0][1]-.35f)<1e-6f);
    assert(art->room101_normal_scale[2][1]==.25f);
    Material paint=art->room101[1].materials[1],metal=art->room101[1].materials[2];
    assert(paint.maps[MATERIAL_MAP_NORMAL].texture.id && paint.maps[MATERIAL_MAP_ROUGHNESS].texture.id);
    assert(paint.maps[MATERIAL_MAP_METALNESS].value==0 && metal.maps[MATERIAL_MAP_METALNESS].value==1);
    assert(fabsf(metal.maps[MATERIAL_MAP_ROUGHNESS].value-.30f)<1e-6f);
    assert(fabsf(powf(metal.maps[MATERIAL_MAP_ALBEDO].color.r/255.0f,2.2f)-.5f)<.006f);
    BoundingBox trim=GetModelBoundingBox(art->room101[5]);
    assert(trim.min.x< -1.7f && trim.max.x>-.6f && trim.max.y>2.2f); // Node transforms were baked exactly once.
    // Match the source camera after Z-up -> Y-up conversion; close the authored
    // 100-degree door only for this before/after inspection.
    SwatObject* door=&sim.world.objects[16]; door->yaw=SWAT_PI*.5f;
    Camera3D camera={{-6.7f,1.64f,4.2f},{-6.95f,1.26f,0},{0,1,0},45,CAMERA_PERSPECTIVE};
    before=sim.world; Image captures[2]; char path[4096];
    for(int candidate=0;candidate<2;candidate++) {
        art->room101_ready=candidate;
        captures[candidate]=room101_capture(view,camera,true);
        snprintf(path,sizeof(path),"%s/room101-%s.png",directory,candidate ? "after" : "before");
        assert(ExportImage(captures[candidate],path));
        assert(!memcmp(&before,&sim.world,sizeof(before)));
    }
    Color* a=LoadImageColors(captures[0]),*b=LoadImageColors(captures[1]); int changed=0;
    for(int i=0;i<960*800;i++) changed+=abs(a[i].r-b[i].r)+abs(a[i].g-b[i].g)+abs(a[i].b-b[i].b)>12;
    assert(changed>5000);
    UnloadImageColors(a); UnloadImageColors(b); UnloadImage(captures[0]); UnloadImage(captures[1]);
    // The entire moving overlay follows the door and vanishes with its parent.
    for(int i=1;i<sim.world.count;i++) sim.world.objects[i].active=i==16;
    double centroid[3]={0}; const float angles[]={0,45,100};
    for(int pose=0;pose<4;pose++) {
        if(pose==3) door->active=false; else door->yaw=SWAT_PI*.5f+angles[pose]*SWAT_RAD;
        Image image=room101_capture(view,camera,false); Color* pixels=LoadImageColors(image); int visible=0;
        for(int y=0;y<800;y++) for(int x=0;x<960;x++) {
            Color p=pixels[y*960+x];
            if(p.r!=MAGENTA.r || p.g!=MAGENTA.g || p.b!=MAGENTA.b) { visible++; if(pose<3) centroid[pose]+=x; }
        }
        if(pose==3) assert(!visible); else { assert(visible>1000); centroid[pose]/=visible; }
        snprintf(path,sizeof(path),"%s/room101-door-%d.png",directory,pose); assert(ExportImage(image,path));
        UnloadImageColors(pixels); UnloadImage(image);
    }
    assert(fabs(centroid[0]-centroid[2])>25);
    swat_sim_close(&sim);
    printf("PASS Room 101: authored PBR channels/scales, transformed trim, matched captures (%d changed pixels), three door poses and removal\n",changed);
}
int main(int argc,char** argv) {
    const char* directory=argc>1 ? argv[1] : "build/swat";
    char path[4096];
    environment("SWAT_ENVIRONMENT_ART",NULL); environment("SWAT_ENVIRONMENT_ASSETS",NULL); environment("SWAT_PLASTER_STYLE",NULL);
    environment("SWAT_ENVIRONMENT_STYLE",NULL); environment("SWAT_ENVIRONMENT_PBR",NULL);
    environment("SWAT_MOTEL_ROOM101",NULL);
    SwatView view={0}; swat_view_init(&view,true); assert(IsWindowReady());
    assert(view.environment.plaster.id && view.environment.wood.id && view.environment.door.meshCount);
    assert(view.environment.location==0 && !view.environment.motel[0].meshCount && !view.environment.storefront[0].meshCount);
    room101_graphics(&view,directory);
    static SwatWorld location;location.motel=true;
    swat_environment_art_prepare_location(&view.environment,&location);
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) {
        const SwatMotelAsset* source=swat_motel_asset(i); Model model=view.environment.motel[i];
        assert(model.meshCount>0 && model.materialCount==source->material_count+1);
        for(int m=0;m<source->material_count;m++) {
            assert(model.materials[m+1].maps[MATERIAL_MAP_ROUGHNESS].value==source->materials[m].roughness);
            assert(model.materials[m+1].maps[MATERIAL_MAP_METALNESS].value==source->materials[m].metalness);
        }
    }
    unsigned int mesh=view.environment.motel[0].meshes[0].vaoId;
    swat_environment_art_prepare_location(&view.environment,&location);assert(view.environment.motel[0].meshes[0].vaoId==mesh);
    location.motel=false;location.storefront=true;swat_environment_art_prepare_location(&view.environment,&location);
    assert(!view.environment.motel[0].meshCount && view.environment.location==2);
    assert(!view.environment.room101_ready && !view.environment.room101[0].meshCount);
    for(int i=0;i<SWAT_STOREFRONT_ASSETS;i++) {
        const SwatMotelAsset* source=swat_storefront_asset(i);Model model=view.environment.storefront[i];
        assert(model.meshCount>0 && model.materialCount==source->material_count+1);
        for(int m=0;m<source->material_count;m++) {
            assert(model.materials[m+1].maps[MATERIAL_MAP_ROUGHNESS].value==source->materials[m].roughness);
            assert(model.materials[m+1].maps[MATERIAL_MAP_METALNESS].value==source->materials[m].metalness);
        }
    }
    assert(view.environment.storefront[SWAT_STOREFRONT_ASSETS-1].meshCount==8);
    glass_graphics(&view.environment,directory);
    location.storefront=false;swat_environment_art_prepare_location(&view.environment,&location);
    assert(view.environment.location==0 && !view.environment.storefront[0].meshCount);
    static const int door_materials[5]={2,2,1,3,4};
    assert(view.environment.door.meshCount==5 && view.environment.door.materialCount==5);
    for(int i=0;i<5;i++) assert(view.environment.door.meshMaterial[i]==door_materials[i]);
    assert(view.environment.plaster_tile_metres==1 && view.environment.plaster.width==1254);
    for(int i=0;i<SWAT_SURFACE_COUNT;i++) {
        SwatSurfaceMaps* m=&view.environment.surfaces[i];
        assert(m->color.id && m->color.width==512 && m->normal.id && m->roughness.id);
    }
    environment("SWAT_ENVIRONMENT_PBR","0");
    SwatEnvironmentArt color_only={0}; swat_environment_art_init(&color_only);
    for(int i=0;i<SWAT_SURFACE_COUNT;i++) assert(color_only.surfaces[i].color.id && !color_only.surfaces[i].normal.id && !color_only.surfaces[i].roughness.id);
    swat_environment_art_close(&color_only); environment("SWAT_ENVIRONMENT_PBR",NULL);
    environment("SWAT_ENVIRONMENT_STYLE","legacy");
    SwatEnvironmentArt legacy={0}; swat_environment_art_init(&legacy);
    for(int i=0;i<SWAT_SURFACE_COUNT;i++) assert(!legacy.surfaces[i].color.id);
    swat_environment_art_close(&legacy); environment("SWAT_ENVIRONMENT_STYLE",NULL);
    environment("SWAT_PLASTER_STYLE","weathered");
    SwatEnvironmentArt weathered={0}; swat_environment_art_init(&weathered);
    assert(weathered.plaster.id && weathered.plaster.width==256 && weathered.plaster_tile_metres==1.8f);
    swat_environment_art_close(&weathered); environment("SWAT_PLASTER_STYLE",NULL);
    prop_graphics(&view.environment,directory);
    destruction_pixels(&view.environment,directory);
    SwatObject door={0}; door.active=true; door.door=true; door.material=SWAT_WOOD;
    door.half=swat_v(.022f,1.01f,.56f); door.health=door.max_health=120;
    snprintf(path,sizeof(path),"%s/environment-door.png",directory);
    size_t pixels=object_pixels(&view.environment,&door,path); assert(pixels>1000);
    door.active=false; assert(object_pixels(&view.environment,&door,NULL)==0);
    door.active=true; door.door=false; door.material=SWAT_DRYWALL; door.half=swat_v(.00625f,.45f,.3f);
    assert(object_pixels(&view.environment,&door,NULL)>1000);
    SwatConfig config=swat_default_config(); config.mission=SWAT_GENERATED; config.layout_seed=42;
    config.hostile_fire=false; swat_sim_init(&sim,config,42);
    memcpy(&before,&sim.world,sizeof(before)); view.planning=true;
    for(int i=0;i<3;i++) { BeginDrawing(); swat_view_draw(&view,&sim,false,75); EndDrawing(); }
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    snprintf(path,sizeof(path),"%s/environment-generated.png",directory);
    Image scene=LoadImageFromScreen(); assert(ExportImage(scene,path)); UnloadImage(scene);
    // Real authority removes one skin; no cached mesh or object ID outlives it.
    int broken=-1;
    for(int i=0;i<sim.world.count;i++) if(sim.world.objects[i].part==SWAT_PART_SKIN) { broken=i; break; }
    assert(broken>=0 && swat_world_damage(&sim.world,broken,10000));
    assert(!swat_environment_art_draw(&view.environment,&sim.world.objects[broken]));
    swat_environment_art_close(&view.environment);
    environment("SWAT_ENVIRONMENT_ASSETS","/swat-deliberately-missing-assets");
    swat_environment_art_init(&view.environment);
    assert(!view.environment.plaster.id && !view.environment.wood.id && !view.environment.door.meshCount);
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) assert(!view.environment.props[i].meshCount);
    assert(!swat_environment_art_draw(&view.environment,&door));
    for(int i=0;i<3;i++) { BeginDrawing(); swat_view_draw(&view,&sim,false,75); EndDrawing(); }
    snprintf(path,sizeof(path),"%s/environment-missing-fallback.png",directory);
    scene=LoadImageFromScreen(); assert(ExportImage(scene,path)); UnloadImage(scene);
    swat_environment_art_close(&view.environment);
    char corrupt[4096]; snprintf(corrupt,sizeof(corrupt),"%s/environment-corrupt-fixture",directory);
    MakeDirectory(corrupt);
    snprintf(path,sizeof(path),"%s/environment-corrupt-fixture/door_leaf.glb",directory);
    FILE* invalid=fopen(path,"wb"); assert(invalid); assert(fputs("invalid GLB fixture",invalid)>=0); assert(!fclose(invalid));
    environment("SWAT_ENVIRONMENT_ASSETS",corrupt);
    for(int i=0;i<2;i++) {
        swat_environment_art_init(&view.environment);
        assert(!view.environment.door.meshCount);
        assert(!swat_environment_art_draw(&view.environment,&door));
        swat_environment_art_close(&view.environment);
        assert(!view.environment.door.materials && !view.environment.door.materialCount);
    }
    assert(remove(path)==0);
    snprintf(path,sizeof(path),"%s/environment-corrupt-fixture/prop_chipped_coffee_mug.glb",directory);
    invalid=fopen(path,"wb"); assert(invalid); assert(fputs("invalid GLB fixture",invalid)>=0); assert(!fclose(invalid));
    swat_environment_art_init(&view.environment);
    assert(!view.environment.props[SWAT_ENV_PROP_MUG].meshCount);
    swat_environment_art_close(&view.environment); assert(remove(path)==0);
    for(int fixture=0;fixture<2;fixture++) {
        write_prop_fixture(path,fixture ? .01f : 0,fixture ? .03f : 1);
        Model valid=LoadModel(path); assert(valid.meshCount==1); UnloadModel(valid);
        swat_environment_art_init(&view.environment);
        assert(!view.environment.props[SWAT_ENV_PROP_MUG].meshCount);
        swat_environment_art_close(&view.environment); assert(remove(path)==0);
    }
    environment("SWAT_ENVIRONMENT_ASSETS",NULL); environment("SWAT_ENVIRONMENT_ART","0");
    swat_environment_art_init(&view.environment); assert(!view.environment.wood.id);
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) assert(!view.environment.props[i].meshCount);
    swat_environment_art_close(&view.environment); environment("SWAT_ENVIRONMENT_ART",NULL);
    // Repeated initialization is idempotent; close/reopen owns no stale GPU IDs.
    for(int i=0;i<2;i++) {
        swat_environment_art_init(&view.environment);
        unsigned int id=view.environment.wood.id; assert(id);
        swat_environment_art_init(&view.environment); assert(view.environment.wood.id==id);
        swat_environment_art_close(&view.environment);
    }
    swat_sim_close(&sim); swat_view_close(&view);
    printf("PASS environment graphics: GLB and surfaces rendered (%zu door pixels), inactive hidden, layered breach with surviving studs, immutable world, generated view, missing/corrupt assets, opt-out and repeated lifecycle\n",pixels);
    return 0;
}
