// Real GPU regression: shadow occlusion, immediate door/destruction invalidation,
// rotated/scaled model and immediate-mode equivalence, immutable authority.
#include "lighting.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SwatSim sim;
static SwatWorld before;
static void environment(const char* key,const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(key,value ? value : ""));
#else
    if(value) assert(!setenv(key,value,1)); else assert(!unsetenv(key));
#endif
}
static void scene(const SwatSim* s,bool cutaway) {
    (void)cutaway;
    for(int i=0;i<s->world.count;i++) {
        const SwatObject* o=&s->world.objects[i]; if(!o->active) continue;
        rlPushMatrix(); rlTranslatef(o->center.x,o->center.y,o->center.z);
        rlRotatef(o->yaw/SWAT_RAD,0,1,0); rlRotatef(o->pitch/SWAT_RAD,0,0,1);
        DrawCubeV((Vector3){0},(Vector3){2*o->half.x,2*o->half.y,2*o->half.z},i ? GRAY : WHITE);
        rlPopMatrix();
    }
}
static Image capture(SwatLighting* light,SwatEnvironmentArt* art,Camera3D camera,Model* model) {
    RenderTexture2D target=LoadRenderTexture(512,512); assert(target.id);
    BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera);
    swat_lighting_begin(light,art,&sim.world,camera.position);
    if(model) {
        for(int i=0;i<model->materialCount;i++) model->materials[i].shader=light->mesh.shader;
        const SwatObject* o=&sim.world.objects[1];
        rlPushMatrix(); rlTranslatef(o->center.x,o->center.y,o->center.z);
        rlRotatef(o->yaw/SWAT_RAD,0,1,0); rlRotatef(o->pitch/SWAT_RAD,0,0,1);
        DrawModelEx(*model,(Vector3){0},(Vector3){0,1,0},0,
            (Vector3){2*o->half.x,2*o->half.y,2*o->half.z},GRAY);
        rlPopMatrix();
    } else scene(&sim,false);
    swat_lighting_end(light,art); EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image);
    UnloadRenderTexture(target); return image;
}

static void wall_quad(float x,bool mirrored) {
    rlBegin(RL_QUADS); rlColor4ub(180,180,180,255); rlNormal3f(0,0,1);
    rlTexCoord2f(0,0); rlVertex3f(x-.8f,-.8f,0);
    rlTexCoord2f(1,0); rlVertex3f(x+.8f,-.8f,0);
    rlTexCoord2f(1,mirrored ? -1 : 1); rlVertex3f(x+.8f,.8f,0);
    rlTexCoord2f(0,mirrored ? -1 : 1); rlVertex3f(x-.8f,.8f,0); rlEnd();
}
static Image surface_capture(SwatLighting* light,SwatEnvironmentArt* art,Texture2D normal,Texture2D roughness,bool mirrored) {
    Camera3D camera={{0,0,4},{0,0,0},{0,1,0},4,CAMERA_ORTHOGRAPHIC};
    RenderTexture2D target=LoadRenderTexture(256,256);
    BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera);
    swat_lighting_begin(light,art,&sim.world,camera.position);
    swat_lighting_surface(light,normal,roughness,(Vector3){0},(Vector2){0},false);
    wall_quad(-1,mirrored);
    // This plain right-hand wall must never inherit the previous normal map.
    swat_lighting_surface(light,(Texture2D){0},(Texture2D){0},(Vector3){0},(Vector2){0},false);
    wall_quad(1,false);
    swat_lighting_end(light,art); EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image);
    UnloadRenderTexture(target); return image;
}
static Texture2D solid_texture(Color color) {
    Image image=GenImageColor(2,2,color); Texture2D result=LoadTextureFromImage(image); UnloadImage(image); return result;
}
static void environment_basis(SwatLighting* light,SwatEnvironmentArt* art) {
    sim.world.room_count=0; sim.world.objects[0].active=sim.world.objects[1].active=false;
    swat_lighting_prepare(light,&sim,(Vector3){0,0,4},false,scene);
    Texture2D up=solid_texture((Color){128,218,218,255});
    Texture2D down=solid_texture((Color){128,37,218,255});
    Texture2D roughness=solid_texture((Color){170,170,170,255});
    Image a=surface_capture(light,art,up,roughness,false);
    Image b=surface_capture(light,art,down,roughness,false);
    Image mirrored=surface_capture(light,art,up,roughness,true);
    Color* pa=LoadImageColors(a),*pb=LoadImageColors(b),*pm=LoadImageColors(mirrored);
    int left=128*256+64,right=128*256+192;
    int brighter=pa[left].r+pa[left].g+pa[left].b-pb[left].r-pb[left].g-pb[left].b;
    int match=abs(pb[left].r-pm[left].r)+abs(pb[left].g-pm[left].g)+abs(pb[left].b-pm[left].b);
    assert(brighter>30 && match<5);
    assert(!memcmp(&pa[right],&pb[right],sizeof(Color)) && !memcmp(&pa[right],&pm[right],sizeof(Color)));
    printf("environment normal +V response=%d mirrored-basis error=%d; plain surface state restored\n",brighter,match);
    UnloadImageColors(pa); UnloadImageColors(pb); UnloadImageColors(pm);
    UnloadImage(a); UnloadImage(b); UnloadImage(mirrored);
    UnloadTexture(up); UnloadTexture(down); UnloadTexture(roughness);
}
static Image finish_capture(SwatLighting* light,SwatEnvironmentArt* art,Model* model,
                            Texture2D specular,Texture2D gloss) {
    Camera3D camera={{1.1f,3,1.1f},{0,0,0},{0,1,0},3,CAMERA_ORTHOGRAPHIC};
    RenderTexture2D target=LoadRenderTexture(256,256); assert(target.id);
    Material* material=&model->materials[0];
    material->maps[MATERIAL_MAP_ALBEDO].color=(Color){128,128,128,255};
    material->maps[MATERIAL_MAP_SPECULAR].texture=specular;
    material->maps[MATERIAL_MAP_ROUGHNESS].texture=gloss;
    material->maps[MATERIAL_MAP_ROUGHNESS].value=-1;
    BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera);
    swat_lighting_begin(light,art,&sim.world,camera.position);
    material->shader=light->mesh.shader;
    swat_lighting_material(light,*material,true); DrawModel(*model,(Vector3){0},1,WHITE);
    swat_lighting_material(light,(Material){0},false);
    swat_lighting_end(light,art); EndMode3D(); EndTextureMode();
    Image result=LoadImageFromTexture(target.texture); ImageFlipVertical(&result); UnloadRenderTexture(target);
    return result;
}
static void source_finish(SwatLighting* light,SwatEnvironmentArt* art) {
    sim.world.room_count=0;
    Model model=LoadModelFromMesh(GenMeshPlane(2,2,1,1));
    Texture2D dull=solid_texture((Color){16,16,16,255}),smooth=solid_texture((Color){240,240,240,255});
    Texture2D red=solid_texture((Color){220,20,20,255}),blue=solid_texture((Color){20,20,220,255});
    Image a=finish_capture(light,art,&model,red,dull),b=finish_capture(light,art,&model,blue,dull);
    Image shiny=finish_capture(light,art,&model,red,smooth);
    Color* pa=LoadImageColors(a),*pb=LoadImageColors(b),*ps=LoadImageColors(shiny);
    int colored=0,gloss_changed=0;
    for(int i=0;i<256*256;i++) {
        if(pa[i].r>pb[i].r+10 && pb[i].b>pa[i].b+10) colored++;
        if(abs(pa[i].r-ps[i].r)+abs(pa[i].g-ps[i].g)+abs(pa[i].b-ps[i].b)>10) gloss_changed++;
    }
    printf("source specular-color response=%d gloss-response pixels=%d\n",colored,gloss_changed);
    assert(colored>1000 && gloss_changed>1000);
    // Both source texture slots are borrowed; release each owner once.
    model.materials[0].shader=(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()}; UnloadModel(model);
    UnloadTexture(dull); UnloadTexture(smooth); UnloadTexture(red); UnloadTexture(blue);
    UnloadImageColors(pa); UnloadImageColors(pb); UnloadImageColors(ps);
    UnloadImage(a); UnloadImage(b); UnloadImage(shiny);
    puts("PASS original specular-color and linear gloss maps affect distinct rendered material responses");
}
int main(int argc,char** argv) {
    const char* directory=argc>1 ? argv[1] : ".";
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(640,480,"SWAT lighting regression"); assert(IsWindowReady());
    environment("SWAT_LIGHTING",NULL); environment("SWAT_EXPOSURE",NULL);
    SwatLighting light={0}; swat_lighting_init(&light); assert(light.enabled);
    SwatEnvironmentArt art={0};
    sim.world.count=2; sim.world.room_count=1;
    sim.world.rooms[0]=(SwatRoom){{0,1.5f,0},{4,1.5f,4},SWAT_DRYWALL,SWAT_CONCRETE};
    sim.world.objects[0]=(SwatObject){.active=true,.center={0,-.05f,0},.half={4,.05f,4},.material=SWAT_CONCRETE};
    sim.world.objects[1]=(SwatObject){.active=true,.center={0,1,0},.half={.5f,1,.5f},.material=SWAT_WOOD};
    Camera3D camera={{6,7,7},{0,0,0},{0,1,0},8,CAMERA_ORTHOGRAPHIC};
    before=sim.world;
    swat_lighting_prepare(&light,&sim,camera.position,false,scene); assert(light.updates==1);
    swat_lighting_prepare(&light,&sim,camera.position,false,scene); assert(light.updates==1);
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    Image blocked=capture(&light,&art,camera,NULL);
    char path[4096]; snprintf(path,sizeof(path),"%s/lighting-occluded.png",directory); assert(ExportImage(blocked,path));
    sim.world.objects[1].active=false;
    swat_lighting_prepare(&light,&sim,camera.position,false,scene); assert(light.updates==2);
    Image open=capture(&light,&art,camera,NULL);
    snprintf(path,sizeof(path),"%s/lighting-open.png",directory); assert(ExportImage(open,path));
    Color* a=LoadImageColors(blocked),*b=LoadImageColors(open); int shadow_pixels=0;
    // These sample locations are on the floor outside the block's own footprint.
    for(float x=.8f;x<1.6f;x+=.04f) for(float z=.5f;z<1.3f;z+=.04f) {
        Vector2 p=GetWorldToScreenEx((Vector3){x,.001f,z},camera,512,512);
        int k=(int)p.y*512+(int)p.x;
        if(b[k].r+b[k].g+b[k].b > a[k].r+a[k].g+a[k].b+30) shadow_pixels++;
    }
    printf("floor shadow sample count=%d\n",shadow_pixels); assert(shadow_pixels>50);
    UnloadImageColors(a); UnloadImageColors(b); UnloadImage(blocked); UnloadImage(open);
    sim.world.objects[1].active=true; sim.world.objects[1].center.x=1;
    sim.world.objects[1].yaw=.7f; sim.world.objects[1].pitch=.2f; sim.world.objects[1].half=(b3Vec3){.5f,1,.25f};
    swat_lighting_prepare(&light,&sim,camera.position,false,scene); assert(light.updates==3);
    sim.world.objects[0].active=false;
    Image batch=capture(&light,&art,camera,NULL);
    Model model=LoadModelFromMesh(GenMeshCube(1,1,1)); Image mesh=capture(&light,&art,camera,&model);
    a=LoadImageColors(batch); b=LoadImageColors(mesh); int mismatch=0,visible=0;
    for(int i=0;i<512*512;i++) {
        if(a[i].r+a[i].g+a[i].b) visible++;
        if(abs(a[i].r-b[i].r)+abs(a[i].g-b[i].g)+abs(a[i].b-b[i].b)>8) mismatch++;
    }
    printf("rotated model pixels=%d mismatch=%d\n",visible,mismatch);
    assert(visible>1000 && mismatch<visible/100+20);
    UnloadImageColors(a); UnloadImageColors(b); UnloadImage(batch); UnloadImage(mesh);
    model.materials[0].shader=(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()}; UnloadModel(model);
    // The large ground crosses the shadow atlas boundary. It must remain a
    // uniformly lit plane, including texel edges, with no self-shadow speckles.
    sim.world.room_count=0; sim.world.objects[0].active=true;
    sim.world.objects[0].half=(b3Vec3){20,.05f,20}; sim.world.objects[1].active=false;
    Camera3D top={{11,30,0},{11,0,0},{0,0,-1},16,CAMERA_ORTHOGRAPHIC};
    swat_lighting_prepare(&light,&sim,top.position,false,scene);
    Image ground=capture(&light,&art,top,NULL); a=LoadImageColors(ground);
    int darkest=765,brightest=0;
    for(int y=16;y<496;y++) for(int x=16;x<496;x++) {
        Color c=a[y*512+x]; int brightness=c.r+c.g+c.b;
        if(brightness<darkest) darkest=brightness;
        if(brightness>brightest) brightest=brightness;
    }
    printf("ground brightness range=%d..%d\n",darkest,brightest); assert(brightest-darkest<8 && darkest>500);
    UnloadImageColors(a); UnloadImage(ground);
    environment_basis(&light,&art);
    source_finish(&light,&art);
    swat_lighting_close(&light); assert(!light.sun.id && !light.batch.shader.id);
    environment("SWAT_LIGHTING","0"); swat_lighting_init(&light); assert(!light.enabled && !light.sun.id);
    swat_lighting_close(&light); environment("SWAT_LIGHTING",NULL);
    environment("SWAT_EXPOSURE","99"); swat_lighting_init(&light); assert(light.exposure==3);
    swat_lighting_close(&light); environment("SWAT_EXPOSURE",NULL);
    CloseWindow(); puts("PASS lighting: real shadows, cache, immediate geometry updates, transformed mesh/batch equivalence, world immutability, opt-out, exposure and GPU lifecycle");
    return 0;
}
