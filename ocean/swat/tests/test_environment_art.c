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
int main(int argc,char** argv) {
    const char* directory=argc>1 ? argv[1] : "build/swat";
    char path[4096];
    environment("SWAT_ENVIRONMENT_ART",NULL); environment("SWAT_ENVIRONMENT_ASSETS",NULL);
    SwatView view={0}; swat_view_init(&view,true); assert(IsWindowReady());
    assert(view.environment.plaster.id && view.environment.wood.id && view.environment.door.meshCount);
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
    environment("SWAT_ENVIRONMENT_ASSETS",NULL); environment("SWAT_ENVIRONMENT_ART","0");
    swat_environment_art_init(&view.environment); assert(!view.environment.wood.id);
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
