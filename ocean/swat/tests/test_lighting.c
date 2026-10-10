// Real GPU regression: shadow occlusion, immediate door/destruction invalidation,
// rotated/scaled model and immediate-mode equivalence, immutable authority.
#include "lighting.h"
#include "rlgl.h"
#include "raymath.h"
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
        const SwatObject* o=&s->world.objects[i]; if(!o->active || o->part==SWAT_PART_LIGHT) continue;
        rlPushMatrix(); rlTranslatef(o->center.x,o->center.y,o->center.z);
        rlRotatef(o->yaw/SWAT_RAD,0,1,0); rlRotatef(o->pitch/SWAT_RAD,0,0,1);
        if(o->fractured)swat_environment_fragment_draw(o);
        else DrawCubeV((Vector3){0},(Vector3){2*o->half.x,2*o->half.y,2*o->half.z},i ? GRAY : WHITE);
        rlPopMatrix();
    }
}
static void contact_scene(void* context,const SwatSim* s,bool cutaway) { (void)context;scene(s,cutaway); }
static void contact_checks(SwatLighting* light,const char* directory) {
    sim.world.room_count=0;sim.world.count=2;
    sim.world.objects[0]=(SwatObject){.active=true,.center={0,-.05f,0},.half={4,.05f,4},.material=SWAT_CONCRETE};
    sim.world.objects[1]=(SwatObject){.active=true,.center={0,.5f,0},.half={.5f,.5f,.5f},.material=SWAT_WOOD};
    Camera3D camera={{2.2f,2.3f,2.2f},{0,.1f,0},{0,1,0},55,CAMERA_PERSPECTIVE};
    before=sim.world;
    // Insets and test captures already have an FBO bound. Both allocation and
    // resize must return to that exact target, with its original matrices.
    RenderTexture2D outer=LoadRenderTexture(512,384);BeginTextureMode(outer);
    unsigned int bound=rlGetActiveFramebuffer();Matrix projection=rlGetMatrixProjection();
    swat_lighting_contact(light,&sim,camera,512,384,contact_scene,NULL);
    assert(light->contact_ready && rlGetActiveFramebuffer()==bound);
    Matrix restored=rlGetMatrixProjection();assert(!memcmp(&projection,&restored,sizeof(projection)));
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    Image blocked=LoadImageFromTexture(light->contact_ao.texture);
    sim.world.objects[1].active=false;
    swat_lighting_contact(light,&sim,camera,512,384,contact_scene,NULL);
    Image open=LoadImageFromTexture(light->contact_ao.texture);
    Color* a=LoadImageColors(blocked),*b=LoadImageColors(open);int changed=0,dark=0;
    for(int i=0;i<blocked.width*blocked.height;i++) {changed+=a[i].r+3<b[i].r;dark+=b[i].r<250;}
    printf("contact occlusion changed pixels=%d, plain-plane dark pixels=%d\n",changed,dark);
    assert(changed>30 && dark<blocked.width*blocked.height/50);
    char path[4096];ImageFlipVertical(&blocked);snprintf(path,sizeof(path),"%s/contact-occlusion.png",directory);assert(ExportImage(blocked,path));
    UnloadImageColors(a);UnloadImageColors(b);UnloadImage(blocked);UnloadImage(open);
    swat_lighting_contact(light,&sim,camera,256,192,contact_scene,NULL);
    assert(light->contact_depth.depth.width==128 && rlGetActiveFramebuffer()==bound);
    light->contact_enabled=false;swat_lighting_contact(light,&sim,camera,256,192,contact_scene,NULL);
    assert(!light->contact_ready);light->contact_enabled=true;
    EndTextureMode();UnloadRenderTexture(outer);
    puts("PASS contact: immediate geometry removal, clean flat plane, authority immutable, nested framebuffer and resize restored");
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
static void lamp_coverage_checks(SwatLighting* light,SwatEnvironmentArt* art,const char* directory) {
    // A marker positions a point light at the origin; it has no rendered mesh.
    // Receivers above and beside it must be shadowed, including a cube seam.
    const Vector3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,0,1}};
    light->sun_energy=(Vector3){0};
    for(int face=0;face<7;face++) {
        Vector3 d=directions[face],center=Vector3Scale(d,3),half={1.1f,1.1f,1.1f};
        if(face==6)half.z=.05f;else {if(d.x)half.x=.05f;if(d.y)half.y=.05f;if(d.z)half.z=.05f;}
        sim.world=(SwatWorld){0};sim.world.room_count=1;sim.world.count=3;sim.tick+=4;
        sim.world.rooms[0]=(SwatRoom){{0,0,0},{4,4,4},SWAT_DRYWALL,SWAT_CONCRETE};
        sim.world.objects[0]=(SwatObject){.active=true,.center={center.x,center.y,center.z},.half={half.x,half.y,half.z},.material=SWAT_CONCRETE};
        sim.world.objects[1]=(SwatObject){.active=true,.center={d.x*1.5f,d.y*1.5f,d.z*1.5f},.half={.65f,.65f,.65f},.material=SWAT_WOOD};
        sim.world.objects[2]=(SwatObject){.active=true,.part=SWAT_PART_LIGHT,.center={0,.06f,0},.half={.01f,.05f,.01f}};
        Camera3D camera={Vector3Scale(d,2.5f),center,d.y?(Vector3){0,0,1}:(Vector3){0,1,0},45,CAMERA_PERSPECTIVE};
        if(face==6)camera.position=(Vector3){3,0,2.5f};
        light->prepared=false;swat_lighting_prepare(light,&sim,camera.position,false,scene);
        Image blocked=capture(light,art,camera,NULL);Color* a=LoadImageColors(blocked);
        sim.world.objects[1].active=false;
        swat_lighting_prepare(light,&sim,camera.position,false,scene);
        Image open=capture(light,art,camera,NULL);Color* b=LoadImageColors(open);int shadowed=0;
        for(int y=224;y<288;y++)for(int x=208;x<304;x++) {
            int p=y*512+x;shadowed+=b[p].r+b[p].g+b[p].b>a[p].r+a[p].g+a[p].b+30;
        }
        printf("lamp direction %d shadowed receiver samples=%d / 6144\n",face,shadowed);fflush(stdout);
        char path[4096];snprintf(path,sizeof(path),"%s/lamp-direction-%d.png",directory,face);assert(ExportImage(blocked,path));
        assert(shadowed>6000);
        UnloadImageColors(a);UnloadImageColors(b);UnloadImage(blocked);UnloadImage(open);
    }
    puts("PASS lamp coverage: all six directions and face seam retain occlusion without material normal maps");
}
static int static_calls,actor_calls;
static bool fake_actor;
static void counted_geometry(void* context,const SwatSim* s,bool cutaway) {
    (void)context;static_calls++;scene(s,cutaway);
}
static void test_actors(void* context,const SwatSim* s,bool cutaway) {
    (void)context;(void)cutaway;actor_calls++;
    if(fake_actor && s->actors[0].present) {
        b3Pos p=b3Body_GetPosition(s->actors[0].controller.body.body);
        DrawCubeV((Vector3){p.x,p.y,p.z},(Vector3){1.2f,1.2f,1.2f},WHITE);
    }
}
static void multi_room_checks(SwatLighting* light,SwatEnvironmentArt* art,const char* directory) {
    sim.world=(SwatWorld){0};sim.world.room_count=3;sim.world.count=6;sim.tick=100;
    b3WorldDef world_def=b3DefaultWorldDef();b3WorldId actor_world=b3CreateWorld(&world_def);
    b3BodyDef body_def=b3DefaultBodyDef();body_def.position=(b3Pos){0,.6f,0};
    sim.actor_count=1;sim.actors[0]=(SwatActor){0};
    sim.actors[0].controller.body.body=b3CreateBody(actor_world,&body_def);sim.actors[0].controller.body.totalHeight=1.2f;
    for(int r=0;r<3;r++) {
        float x=(r-1)*5;
        sim.world.rooms[r]=(SwatRoom){{x,1.5f,0},{2,1.5f,2},SWAT_DRYWALL,SWAT_CONCRETE};
        sim.world.objects[r*2]=(SwatObject){.active=true,.center={x,-.05f,0},.half={2,.05f,2},.material=SWAT_CONCRETE};
        sim.world.objects[r*2+1]=(SwatObject){.active=true,.center={x,.6f,0},.half={.6f,.6f,.6f},.material=SWAT_WOOD};
    }
    Camera3D camera={{0,16,7},{0,0,0},{0,1,0},17,CAMERA_ORTHOGRAPHIC};
    light->prepared=false;static_calls=0;fake_actor=false;light->sun_energy=(Vector3){0};
    swat_lighting_prepare_split(light,&sim,(Vector3){-5,1.6f,5},false,counted_geometry,test_actors,NULL);
    assert(static_calls==1+3*SWAT_LAMP_FACES);int updates=light->room_updates;
    Image original=capture(light,art,camera,NULL);Color* a=LoadImageColors(original);
    char path[4096];snprintf(path,sizeof(path),"%s/all-room-shadows.png",directory);assert(ExportImage(original,path));
    // Walk along and back from a row of visible rooms. The fixed inspection
    // camera removes perspective/specular differences and isolates shadow state.
    const Vector3 eyes[]={{5,1.6f,5},{0,1.6f,20},{-12,1.6f,20}};
    for(int i=0;i<3;i++) {
        sim.tick+=4;static_calls=actor_calls=0;
        swat_lighting_prepare_split(light,&sim,eyes[i],false,counted_geometry,test_actors,NULL);
        assert(static_calls==0 && actor_calls==1 && light->room_updates==updates); // Cached sun geometry; empty room tiles untouched.
        Image frame=capture(light,art,camera,NULL);Color* b=LoadImageColors(frame);
        int mismatch=0;for(int p=0;p<512*512;p++)mismatch+=abs(a[p].r-b[p].r)+abs(a[p].g-b[p].g)+abs(a[p].b-b[p].b)>3;
        assert(!mismatch);UnloadImageColors(b);UnloadImage(frame);
    }
    for(int r=0;r<3;r++) {
        sim.world.objects[2*r+1].active=false;
        swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
        Image open=capture(light,art,camera,NULL);Color* b=LoadImageColors(open);int shadowed=0;
        for(float z=-.3f;z<.3f;z+=.03f) {
            Vector2 p=GetWorldToScreenEx((Vector3){(r-1)*5+.85f,.001f,z},camera,512,512);
            int index=(int)p.y*512+(int)p.x;
            shadowed+=b[index].r+b[index].g+b[index].b>a[index].r+a[index].g+a[index].b+20;
        }
        printf("room %d lamp-shadow floor samples=%d\n",r,shadowed);assert(shadowed>10);
        UnloadImageColors(b);UnloadImage(open);sim.world.objects[2*r+1].active=true;
        swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
    }
    updates=light->room_updates;
    // Removing one blocker updates exactly that room immediately.
    sim.world.objects[3].active=false;static_calls=0;
    swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
    assert(static_calls==1+SWAT_LAMP_FACES && light->room_updates==updates+1);
    Image removed=capture(light,art,camera,NULL);Color* b=LoadImageColors(removed);int changed=0;
    for(int i=0;i<512*512;i++)changed+=abs(a[i].r-b[i].r)+abs(a[i].g-b[i].g)+abs(a[i].b-b[i].b)>12;
    assert(changed>50);
    fake_actor=true;sim.actors[0].present=true;sim.tick+=4;static_calls=actor_calls=0;updates=light->room_updates;
    unsigned int actor_updates=light->actor_room_updates;
    swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
    assert(static_calls==0 && actor_calls==1+SWAT_LAMP_FACES && light->room_updates==updates && light->actor_room_updates==actor_updates+1);
    Image actor=capture(light,art,camera,NULL);Color* c=LoadImageColors(actor);changed=0;
    for(int i=0;i<512*512;i++)changed+=b[i].r+b[i].g+b[i].b>c[i].r+c[i].g+c[i].b+20;
    assert(changed>50);UnloadImageColors(c);UnloadImage(actor);
    sim.tick+=16;static_calls=actor_calls=0;
    swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
    // One final refresh after the blend settling interval, then full reuse.
    assert(light->actor_room_updates==actor_updates+2);
    sim.tick+=4;static_calls=actor_calls=0;
    swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
    assert(static_calls==0 && actor_calls==1 && light->actor_room_updates==actor_updates+2);
    // Leaving the room clears its former moving shadow without touching the
    // other rooms' depth, even though the actor remains present in the scene.
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){20,.6f,0},b3Quat_identity);sim.tick+=4;
    swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
    Image cleared=capture(light,art,camera,NULL);c=LoadImageColors(cleared);
    assert(!memcmp(b,c,512*512*sizeof(Color)));UnloadImageColors(c);UnloadImage(cleared);
    assert(light->actor_room_updates==actor_updates+3);
    // A partial hole changes the caster without changing center, bounds or
    // activity. It must invalidate both static caches on this same tick.
    Image whole=capture(light,art,camera,NULL);Color* whole_pixels=LoadImageColors(whole);
    sim.world.objects[1].fractured=true;
    const float corners[4][2]={{-.6f,-.6f},{-.6f,.6f},{0,.6f},{0,-.6f}};
    memcpy(sim.world.objects[1].corners,corners,sizeof(corners));
    static_calls=0;updates=light->room_updates;unsigned int sun_updates=light->sun_geometry_updates;
    swat_lighting_prepare_split(light,&sim,(Vector3){0,1.6f,20},false,counted_geometry,test_actors,NULL);
    assert(static_calls==1+SWAT_LAMP_FACES && light->room_updates==updates+1 && light->sun_geometry_updates==sun_updates+1);
    Image partial=capture(light,art,camera,NULL);Color* partial_pixels=LoadImageColors(partial);int opened=0;
    for(float z=-.3f;z<.3f;z+=.03f){
        Vector2 p=GetWorldToScreenEx((Vector3){-5+.85f,.001f,z},camera,512,512);int at=(int)p.y*512+(int)p.x;
        opened+=partial_pixels[at].r+partial_pixels[at].g+partial_pixels[at].b>whole_pixels[at].r+whole_pixels[at].g+whole_pixels[at].b+20;
    }
    printf("partial fracture shadow-open floor samples=%d\n",opened);assert(opened>10);
    UnloadImageColors(whole_pixels);UnloadImageColors(partial_pixels);UnloadImage(whole);UnloadImage(partial);
    UnloadImageColors(a);UnloadImageColors(b);UnloadImage(original);UnloadImage(removed);
    fake_actor=false;sim.actor_count=0;sim.actors[0].present=false;b3DestroyWorld(actor_world);
    puts("PASS all room shadows: camera-independent tiles, local immediate invalidation, cached sun geometry, empty/stationary room reuse, moving silhouette entry/exit with no trails");
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
static void second_uv_ao(SwatLighting* light,SwatEnvironmentArt* art) {
    Mesh mesh={.vertexCount=6,.triangleCount=2};
    mesh.vertices=MemAlloc(18*sizeof(float));mesh.normals=MemAlloc(18*sizeof(float));
    mesh.texcoords=MemAlloc(12*sizeof(float));mesh.texcoords2=MemAlloc(12*sizeof(float));
    const float positions[]={-1,-1,0,1,-1,0,1,1,0,-1,-1,0,1,1,0,-1,1,0};
    memcpy(mesh.vertices,positions,sizeof(positions));
    for(int i=0;i<6;i++) {
        mesh.normals[3*i]=mesh.normals[3*i+1]=0;mesh.normals[3*i+2]=1;
        mesh.texcoords[2*i]=.25f;mesh.texcoords2[2*i]=.75f;
        mesh.texcoords[2*i+1]=mesh.texcoords2[2*i+1]=.5f;
    }
    UploadMesh(&mesh,false);Model model=LoadModelFromMesh(mesh);
    Image map=GenImageColor(2,1,BLACK);ImageDrawPixel(&map,1,0,WHITE);
    Texture2D ao=LoadTextureFromImage(map);UnloadImage(map);SetTextureFilter(ao,TEXTURE_FILTER_POINT);
    Material* m=&model.materials[0];m->maps[MATERIAL_MAP_OCCLUSION].texture=ao;
    m->maps[MATERIAL_MAP_OCCLUSION].value=1;m->shader=light->mesh.shader;
    Camera3D camera={{0,0,3},{0,0,0},{0,1,0},3,CAMERA_ORTHOGRAPHIC};
    Vector3 sun=light->sun_energy;light->sun_energy=(Vector3){0};sim.world.room_count=0;
    Color samples[3];RenderTexture2D target=LoadRenderTexture(128,128);
    for(int i=0;i<3;i++) {
        BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
        swat_lighting_begin(light,art,&sim.world,camera.position);
        if(i==2)swat_lighting_material(light,*m,true); // Default call must restore UV0.
        else swat_lighting_material_uv(light,*m,true,1,i);
        DrawModel(model,(Vector3){0},1,WHITE);swat_lighting_material(light,(Material){0},false);
        swat_lighting_end(light,art);EndMode3D();EndTextureMode();
        Image frame=LoadImageFromTexture(target.texture);samples[i]=GetImageColor(frame,64,64);UnloadImage(frame);
    }
    assert(samples[1].r>samples[0].r+30 && samples[1].g>samples[0].g+30);
    assert(!memcmp(&samples[0],&samples[2],sizeof(Color)));
    m->maps[MATERIAL_MAP_OCCLUSION].texture=(Texture2D){0};
    m->shader=(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};UnloadModel(model);
    UnloadTexture(ao);UnloadRenderTexture(target);light->sun_energy=sun;
    puts("PASS second-UV AO: independent UV1 sampling and default UV0 restoration");
}
static void sky_check(SwatLighting* light,const char* directory) {
    RenderTexture2D target=LoadRenderTexture(320,180);
    Camera3D camera={{0,0,0},{0,1,.1f},{0,0,-1},60,CAMERA_PERSPECTIVE};
    BeginTextureMode(target);ClearBackground(BLACK);
    DrawRectangle(0,0,10,10,RED); // Exercise the shader-switch batch flush.
    swat_lighting_sky(light,camera,320,180);EndTextureMode();
    Image frame=LoadImageFromTexture(target.texture);Color* pixels=LoadImageColors(frame);
    int lit=0;for(int i=0;i<320*180;i++)lit+=pixels[i].r+pixels[i].g+pixels[i].b>120;
    assert(lit>320*180*9/10);
    ImageFlipVertical(&frame);char path[4096];snprintf(path,sizeof(path),"%s/hdr-sky.png",directory);assert(ExportImage(frame,path));
    UnloadImageColors(pixels);UnloadImage(frame);UnloadRenderTexture(target);
    puts("PASS HDR sky remains sampled after shader-switch batch flush");
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
    Texture2D ao=solid_texture(BLACK);
    model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture=ao;
    model.materials[0].maps[MATERIAL_MAP_OCCLUSION].value=1;
    Image occluded=finish_capture(light,art,&model,red,dull);
    model.materials[0].maps[MATERIAL_MAP_OCCLUSION].value=0;
    Image disabled=finish_capture(light,art,&model,red,dull);
    Color* po=LoadImageColors(occluded),*pd=LoadImageColors(disabled);int affected=0,still_lit=0;
    for(int i=0;i<256*256;i++) {
        affected+=pa[i].r+pa[i].g+pa[i].b>po[i].r+po[i].g+po[i].b+5;
        still_lit+=po[i].r+po[i].g+po[i].b>30;
        assert(!memcmp(&pa[i],&pd[i],sizeof(Color)));
    }
    assert(affected>1000 && still_lit>1000);
    model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture=(Texture2D){0};UnloadTexture(ao);
    UnloadImageColors(po);UnloadImageColors(pd);UnloadImage(occluded);UnloadImage(disabled);
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
    environment("SWAT_CONTACT_SHADOWS","1");
    environment("SWAT_IBL","0"); // Fixed legacy sun direction for the shadow-coordinate fixture.
    SwatLighting light={0}; swat_lighting_init(&light); assert(light.enabled);
    assert(!light.environment_atlas.id);
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
    second_uv_ao(&light,&art);
    source_finish(&light,&art);
    contact_checks(&light,directory);
    swat_lighting_close(&light); assert(!light.sun.id && !light.batch.shader.id && !light.contact_depth.id);
    environment("SWAT_IBL",NULL);swat_lighting_init(&light);
    assert(light.environment_atlas.id && light.environment_sky.id && light.environment_scale>0);
    assert(light.environment_atlas.width==256 && light.environment_atlas.height==1024);
    sky_check(&light,directory);
    swat_lighting_prepare(&light,&sim,(Vector3){1.1f,3,1.1f},false,scene);
    source_finish(&light,&art); // Original spec/gloss still works with the HDR environment.
    multi_room_checks(&light,&art,directory);
    lamp_coverage_checks(&light,&art,directory);
    swat_lighting_close(&light);assert(!light.environment_atlas.id && !light.environment_sky.id);
    environment("SWAT_LIGHTING","0"); swat_lighting_init(&light); assert(!light.enabled && !light.sun.id);
    swat_lighting_close(&light); environment("SWAT_LIGHTING",NULL);
    environment("SWAT_EXPOSURE","99"); swat_lighting_init(&light); assert(light.exposure==3);
    swat_lighting_close(&light); environment("SWAT_EXPOSURE",NULL);
    CloseWindow(); puts("PASS lighting: real shadows, cache, immediate geometry updates, transformed mesh/batch equivalence, world immutability, opt-out, exposure and GPU lifecycle");
    return 0;
}
