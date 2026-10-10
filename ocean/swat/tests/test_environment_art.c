// Explicit graphics check: requires a working display/OpenGL context.
#include "environment_art.h"
#include "render.h"
#include "protocol.h"
#include "rlgl.h"
#include "raymath.h"
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
static bool omit_personal_shadow;

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
static void motel_piece(SwatEnvironmentArt* art,const SwatWorld* world,const SwatObject* o,bool shadow,bool cutaway) {
    if((!o->active && !swat_motel_fence_proxy(world,o) && swat_motel_surroundings_part(world,o)!=1) || swat_environment_motel_draw(art,world,o,shadow,cutaway))return;
    rlPushMatrix();rlTranslatef(o->center.x,o->center.y,o->center.z);rlRotatef(o->yaw/SWAT_RAD,0,1,0);
    if(shadow)swat_environment_fragment_draw(o);
    else if(!swat_environment_art_draw(art,o))DrawCubeV((Vector3){0},(Vector3){2*o->half.x,2*o->half.y,2*o->half.z},GRAY);
    rlPopMatrix();
}
static void room101_shadow(void* context,const SwatSim* s,bool cutaway) {
    SwatView* view=context;SwatEnvironmentArt* art=&view->environment;art->shadow_room=view->lighting.shadow_room;
    Model glasses=art->motel_personal[1];
    if(omit_personal_shadow)art->motel_personal[1]=(Model){0};
    for(int i=1;i<s->world.count;i++) motel_piece(art,&s->world,&s->world.objects[i],true,cutaway);
    art->motel_personal[1]=glasses;
    art->shadow_room=-1;
}
static Image room101_capture_size(SwatView* view,Camera3D camera,bool lit,int width,int height) {
    SwatEnvironmentArt* art=&view->environment; SwatLighting* light=&view->lighting;
    light->prepared=false;
    if(lit) swat_lighting_prepare_context(light,&sim,camera.position,false,room101_shadow,view);
    if(lit) swat_lighting_contact(light,&sim,camera,width,height,room101_shadow,view);
    RenderTexture2D target=LoadRenderTexture(width,height); assert(target.id);
    BeginTextureMode(target); ClearBackground(MAGENTA);
    if(lit)swat_lighting_sky(light,camera,width,height);
    BeginMode3D(camera);
    if(lit) swat_lighting_begin(light,art,&sim.world,camera.position);
    swat_environment_motel_cores_begin(art,&sim.world);
    if(lit) {
        const SwatObject* floor=&sim.world.objects[0];
        if(!swat_environment_motel_draw(art,&sim.world,floor,false,false))
            DrawCubeV((Vector3){floor->center.x,floor->center.y,floor->center.z},(Vector3){2*floor->half.x,2*floor->half.y,2*floor->half.z},(Color){110,123,125,255});
    }
    for(int i=1;i<sim.world.count;i++) motel_piece(art,&sim.world,&sim.world.objects[i],false,false);
    swat_environment_art_transparent(art,&sim.world,camera.position,false);
    swat_environment_motel_cores_end(art);
    if(lit) swat_lighting_end(light,art);
    EndMode3D(); EndTextureMode();
    Image image=LoadImageFromTexture(target.texture); ImageFlipVertical(&image); UnloadRenderTexture(target);
    return image;
}
static Image room101_capture(SwatView* view,Camera3D camera,bool lit) {return room101_capture_size(view,camera,lit,960,800);}
static void personal_shadow_review(SwatView* view,const char* directory) {
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.hostile_fire=false;
    swat_sim_init(&sim,config,73);swat_environment_art_prepare_location(&view->environment,&sim.world);
    before=sim.world;
    Camera3D camera={{1.3f,1.30f,-1.7f},{.65f,.78f,-2.15f},{0,1,0},40,CAMERA_PERSPECTIVE};
    const char* names[]={"baseline","no-glasses-caster","no-lamp-shadow","no-contact","shifted","unlit","no-personal-props"};
    bool contact=view->lighting.contact_enabled;
    for(int i=0;i<7;i++) {
        omit_personal_shadow=i==1;view->lighting.lamp_shadows=i!=2;view->lighting.contact_enabled=contact && i!=3;
        Camera3D c=camera;if(i==4){c.position.x+=.15f;c.target.x+=.15f;}
        Model personal[2]={view->environment.motel_personal[0],view->environment.motel_personal[1]};
        if(i==6)for(int j=0;j<2;j++)view->environment.motel_personal[j]=(Model){0};
        Image image=room101_capture_size(view,c,i!=5,1440,810);char path[4096];
        for(int j=0;j<2;j++)view->environment.motel_personal[j]=personal[j];
        snprintf(path,sizeof(path),"%s/personal-%s.png",directory,names[i]);assert(ExportImage(image,path));UnloadImage(image);
    }
    omit_personal_shadow=false;view->lighting.lamp_shadows=true;view->lighting.contact_enabled=contact;
    assert(!memcmp(&before,&sim.world,sizeof(before)));swat_sim_close(&sim);
    puts("PASS personal shadow diagnostics: fixed camera/geometry, caster/lamp/contact isolation, shifted view, immutable authority");
}
static int room101_owner_pixels(SwatEnvironmentArt* art,SwatObject* o,bool cutaway) {
    float d=b3Length(o->half)*2+1;
    Camera3D camera={{o->center.x+d,o->center.y+d,o->center.z+d},{o->center.x,o->center.y,o->center.z},{0,1,0},50,CAMERA_PERSPECTIVE};
    RenderTexture2D target=LoadRenderTexture(256,256);BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    assert(swat_environment_motel_draw(art,&sim.world,o,false,cutaway));EndMode3D();EndTextureMode();
    Image frame=LoadImageFromTexture(target.texture);Color* pixels=LoadImageColors(frame);int count=0;
    for(int i=0;i<256*256;i++)count+=pixels[i].r!=MAGENTA.r || pixels[i].g!=MAGENTA.g || pixels[i].b!=MAGENTA.b;
    UnloadImageColors(pixels);UnloadImage(frame);UnloadRenderTexture(target);return count;
}
static void masonry_silhouette(SwatView* view,const char* directory) {
    SwatObject* piece=NULL;
    for(int i=SWAT_MOTEL_INSTANCES+1;i<sim.world.count && !piece;i++) {
        SwatObject* o=&sim.world.objects[i];int parent=swat_motel_wall_parent(&sim.world,o);
        if(parent<0 || swat_motel_instance(parent-1)->asset!=1 || !o->fractured || o->half.y<.2f || o->half.z<.2f)continue;
        for(int e=0;e<4;e++)if(fabsf(fabsf(o->corners[e][0])-o->half.y)>.04f || fabsf(fabsf(o->corners[e][1])-o->half.z)>.04f)piece=o;
    }
    assert(piece);int checked=0,filled=0,empty=0;
    bool prepared=view->lighting.prepared;view->lighting.prepared=false;
    for(int side=-1;side<=1;side+=2) {
        Vector3 center={(float)piece->center.x,(float)piece->center.y,(float)piece->center.z};
        Camera3D camera={Vector3Add(center,(Vector3){side*2*cosf(piece->yaw),0,-side*2*sinf(piece->yaw)}),center,{0,1,0},2*fmaxf(piece->half.y,piece->half.z)+.2f,CAMERA_ORTHOGRAPHIC};
        RenderTexture2D target=LoadRenderTexture(512,512);BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
        assert(swat_environment_motel_draw(&view->environment,&sim.world,piece,false,false));
        EndMode3D();EndTextureMode();Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);
        Color* pixels=LoadImageColors(image);
        for(int y=5;y<507;y+=5)for(int x=5;x<507;x+=5) {
            bool center_hit=false,same=true;
            const int offsets[5][2]={{0,0},{-2,0},{2,0},{0,-2},{0,2}};
            for(int j=0;j<5;j++) {
                Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[j][0],y+.5f+offsets[j][1]},camera,512,512);
                bool hit=b3Shape_RayCast(piece->shape,(b3Pos){ray.position.x,ray.position.y,ray.position.z},swat_v(5*ray.direction.x,5*ray.direction.y,5*ray.direction.z)).hit;
                if(j==0)center_hit=hit;else same &= hit==center_hit;
            }
            if(!same)continue; // Raster coverage within two pixels of the true boundary.
            Color p=pixels[y*512+x];bool visible=p.r!=MAGENTA.r || p.g!=MAGENTA.g || p.b!=MAGENTA.b;
            if(visible!=center_hit)printf("Masonry coverage mismatch side%d pixel%d,%d visible%d collision%d\n",side,x,y,visible,center_hit);
            assert(visible==center_hit);checked++;filled+=visible;empty+=!visible;
        }
        char path[4096];snprintf(path,sizeof(path),"%s/masonry-fragment-%s.png",directory,side<0?"back":"front");assert(ExportImage(image,path));
        UnloadImageColors(pixels);UnloadImage(image);UnloadRenderTexture(target);
    }
    view->lighting.prepared=prepared;assert(filled>1000 && empty>1000);
    printf("PASS masonry silhouette: %d native GPU samples match exact convex collision on both faces (%d solid/%d empty)\n",checked,filled,empty);
}
static void mounted_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,81);SwatEnvironmentArt* art=&view->environment;swat_environment_art_prepare_location(art,&sim.world);
    Model model=art->motel[44];assert(model.meshCount==1&&model.meshes[0].triangleCount==1464&&model.materialCount==2);
    Material m=model.materials[1];assert(m.maps[MATERIAL_MAP_ALBEDO].texture.id&&m.maps[MATERIAL_MAP_NORMAL].texture.id&&m.maps[MATERIAL_MAP_ROUGHNESS].texture.id);
    bool prepared=view->lighting.prepared;view->lighting.prepared=false;int checked=0,filled=0,empty=0;
    for(int i=0;i<SWAT_MOTEL_MOUNTED_INSTANCES;i++)for(int fallback=0;fallback<2;fallback++) {
        SwatObject* o=&sim.world.objects[SWAT_MOTEL_MOUNTED_FIRST+i];float c=cosf(o->yaw),s=sinf(o->yaw);
        b3ShapeId shapes[8];assert(b3Body_GetShapes(o->body,shapes,8)==8);
        art->motel[44]=fallback?(Model){0}:model;
        Camera3D camera={{o->center.x+s,o->center.y,o->center.z+c},{o->center.x,o->center.y,o->center.z},{0,1,0},.68f,CAMERA_ORTHOGRAPHIC};
        RenderTexture2D target=LoadRenderTexture(512,512);BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
        assert(swat_environment_motel_draw(art,&sim.world,o,false,false));EndMode3D();EndTextureMode();
        Image img=LoadImageFromTexture(target.texture);Color* pixels=LoadImageColors(img);
        for(int y=4;y<508;y+=4)for(int x=4;x<508;x+=4) {
            bool center_hit=false,same=true;const int offsets[][2]={{0,0},{-2,0},{2,0},{0,-2},{0,2}};
            for(int j=0;j<5;j++) {
                Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[j][0],y+.5f+offsets[j][1]},camera,512,512);
                bool hit=false;for(int part=0;part<8;part++)hit|=b3Shape_RayCast(shapes[part],(b3Pos){ray.position.x,ray.position.y,ray.position.z},swat_mul(swat_v(ray.direction.x,ray.direction.y,ray.direction.z),1.05f)).hit;
                if(!j)center_hit=hit;else same&=hit==center_hit;
            }
            if(!same)continue;
            Color p=pixels[(511-y)*512+x];bool visible=p.r!=MAGENTA.r||p.g!=MAGENTA.g||p.b!=MAGENTA.b;
            assert(visible==center_hit);checked++;filled+=visible;empty+=!visible;
        }
        char file[4096];ImageFlipVertical(&img);snprintf(file,sizeof(file),"%s/junction-%d-%s.png",directory,i,fallback?"fallback":"art");assert(ExportImage(img,file));
        UnloadImageColors(pixels);UnloadImage(img);UnloadRenderTexture(target);
    }
    art->motel[44]=model;view->lighting.prepared=prepared;assert(filled>300&&empty>1000);
    Camera3D cameras[]={{{-11.15f,1.55f,-7.5f},{-11.50f,1.35f,-6.13f},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{8.35f,1.58f,1.4f},{7.90f,1.35f,.13f},{0,1,0},65,CAMERA_PERSPECTIVE}};
    for(int i=0;i<2;i++) {
        Image img=room101_capture_size(view,cameras[i],true,960,540);char file[4096];snprintf(file,sizeof(file),"%s/junction-scene-%d.png",directory,i);assert(ExportImage(img,file));UnloadImage(img);
    }
    int owner=SWAT_MOTEL_MOUNTED_FIRST,support=sim.world.objects[owner].supports[0];assert(swat_world_damage(&sim.world,support,10000));
    assert(!room101_owner_pixels(art,&sim.world.objects[owner],false));
    swat_sim_close(&sim);printf("PASS mounted graphics: original PBR maps, %d stable ray/raster samples across both placements and exact missing-art fallback, native lit/shadow captures and authoritative support removal\n",checked);
}
static void ground_graphics(SwatView* view,const char* directory) {
    SwatEnvironmentArt* art=&view->environment;Model source=art->motel_ground;
    assert(source.meshCount==SWAT_GROUND_PARTS && source.materialCount==3);
    before=sim.world;int checked=0;
    for(int k=0;k<SWAT_GROUND_PARTS;k++) {
        Mesh mesh=source.meshes[k];assert(mesh.triangleCount==12);
        BoundingBox bounds=GetMeshBoundingBox(mesh);const SwatMotelAsset* a=swat_motel_ground_asset(k);
        assert(fabsf(bounds.min.x-(a->center.x-a->half.x))<.0001f && fabsf(bounds.max.x-(a->center.x+a->half.x))<.0001f);
        assert(fabsf(bounds.min.z-(a->center.z-a->half.z))<.0001f && fabsf(bounds.max.z-(a->center.z+a->half.z))<.0001f);
        for(int m=1;m<source.materialCount;m++)assert(source.materials[m].maps[MATERIAL_MAP_NORMAL].texture.id && source.materials[m].maps[MATERIAL_MAP_ALBEDO].texture.id && source.materials[m].maps[MATERIAL_MAP_ROUGHNESS].texture.id);
    }
    Camera3D cameras[]={{{0,1.65f,42},{0,1,-2},{0,1,0},70,CAMERA_PERSPECTIVE},
        {{38,12,48},{0,0,8},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{31,1.65f,11},{25,-.1f,8},{0,1,0},65,CAMERA_PERSPECTIVE}};
    const char* names[]={"road","context","join"};char path[4096];
    for(int i=0;i<3;i++) {
        Image image=room101_capture_size(view,cameras[i],true,1440,810);
        snprintf(path,sizeof(path),"%s/ground-%s.png",directory,names[i]);assert(ExportImage(image,path));UnloadImage(image);
    }
    // Native raster coverage checks the identity/world-space owner association
    // and exact missing-art fallback independently of the scene-wide draw.
    bool prepared=view->lighting.prepared;view->lighting.prepared=false;
    const int parts[]={0,6,14,40,53};
    for(unsigned p=0;p<sizeof(parts)/sizeof(parts[0]);p++)for(int fallback=0;fallback<2;fallback++) {
        int k=parts[p];const SwatMotelAsset* a=swat_motel_ground_asset(k);SwatObject* o=&sim.world.objects[SWAT_MOTEL_GROUND_FIRST+k];
        art->motel_ground=fallback?(Model){0}:source;
        Camera3D camera={{a->center.x,5,a->center.z},{a->center.x,0,a->center.z},{0,0,-1},fmaxf(a->half.x,a->half.z)*2.4f,CAMERA_ORTHOGRAPHIC};
        RenderTexture2D target=LoadRenderTexture(384,384);BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
        assert(swat_environment_motel_draw(art,&sim.world,o,false,false));EndMode3D();EndTextureMode();
        Image image=LoadImageFromTexture(target.texture);Color* pixels=LoadImageColors(image);int filled=0,empty=0;
        for(int y=5;y<379;y+=5)for(int x=5;x<379;x+=5) {
            Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f,y+.5f},camera,384,384);
            bool hit=b3Shape_RayCast(o->shape,(b3Pos){ray.position.x,ray.position.y,ray.position.z},swat_mul(swat_v(ray.direction.x,ray.direction.y,ray.direction.z),10)).hit;
            Color c=pixels[(383-y)*384+x];bool visible=c.r!=MAGENTA.r || c.g!=MAGENTA.g || c.b!=MAGENTA.b;
            assert(hit==visible);checked++;filled+=visible;empty+=!visible;
        }
        assert(filled>20 && empty>20);UnloadImageColors(pixels);UnloadImage(image);UnloadRenderTexture(target);
        o->active=false;target=LoadRenderTexture(64,64);BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
        assert(swat_environment_motel_draw(art,&sim.world,o,false,false));EndMode3D();EndTextureMode();image=LoadImageFromTexture(target.texture);pixels=LoadImageColors(image);
        for(int i=0;i<64*64;i++)assert(pixels[i].r==MAGENTA.r && pixels[i].g==MAGENTA.g && pixels[i].b==MAGENTA.b);
        UnloadImageColors(pixels);UnloadImage(image);UnloadRenderTexture(target);o->active=true;
    }
    art->motel_ground=source;view->lighting.prepared=prepared;
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    printf("PASS ground graphics: 54 native owner/bounds matches, original PBR maps, three engine captures, %d collision/raster samples, independent removal and exact fallback\n",checked);
}
static void surroundings_graphics(SwatView* view,const char* directory) {
    SwatEnvironmentArt* art=&view->environment;Model shoulder=art->motel_surroundings[0],bank=art->motel_surroundings[1];
    assert(shoulder.meshCount==1 && bank.meshCount==4 && shoulder.materialCount==2 && bank.materialCount==5);
    int triangles=0;for(int i=0;i<bank.meshCount;i++)triangles+=bank.meshes[i].triangleCount;assert(triangles==12256 && shoulder.meshes[0].triangleCount==192);
    for(int m=1;m<bank.materialCount;m++)assert(bank.materials[m].maps[MATERIAL_MAP_NORMAL].texture.id && bank.materials[m].maps[MATERIAL_MAP_ALBEDO].texture.id);
    before=sim.world;
    Camera3D cameras[]={{{22.5f,1.57f,2},{28,.6f,0},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{-22.5f,1.57f,-2},{-28,.6f,0},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{34,10,25},{5,1,-1},{0,1,0},65,CAMERA_PERSPECTIVE}};
    const char* names[]={"east","west","context"};
    for(int i=0;i<3;i++) {
        Image image=room101_capture_size(view,cameras[i],true,1440,810);char path[4096];
        snprintf(path,sizeof(path),"%s/surroundings-%s.png",directory,names[i]);assert(ExportImage(image,path));UnloadImage(image);
    }
    int owner=SWAT_MOTEL_SURROUNDINGS_FIRST+SWAT_SURROUNDINGS_PARTS+1;
    int rock=owner+1;float closest=1e9f;
    for(int i=1;i<=13;i++) {
        b3Pos c=sim.world.objects[owner+i].center;
        Vector2 screen=GetWorldToScreenEx((Vector3){c.x,c.y,c.z},cameras[0],960,540);
        float score=Vector2DistanceSqr(screen,(Vector2){480,270});if(score<closest){closest=score;rock=owner+i;}
    }
    Image intact=room101_capture_size(view,cameras[0],true,960,540);
    // Test independent rock removal and bank/foliage support without erasing
    // neighboring rocks. Restore authority flags before continuing the suite.
    sim.world.objects[rock].active=false;
    Image cut=room101_capture_size(view,cameras[0],true,960,540);
    Color* a=LoadImageColors(intact),*b=LoadImageColors(cut);int changed=0;
    for(int i=0;i<960*540;i++)changed+=abs(a[i].r-b[i].r)+abs(a[i].g-b[i].g)+abs(a[i].b-b[i].b)>20;
    assert(changed>20);UnloadImageColors(a);UnloadImageColors(b);UnloadImage(intact);UnloadImage(cut);
    sim.world.objects[owner].active=false;
    Image image=room101_capture_size(view,cameras[0],true,1440,810);char path[4096];
    snprintf(path,sizeof(path),"%s/surroundings-bank-removed.png",directory);assert(ExportImage(image,path));UnloadImage(image);
    sim.world.objects[owner].active=sim.world.objects[rock].active=true;
    art->motel_surroundings[0]=art->motel_surroundings[1]=(Model){0};
    image=room101_capture_size(view,cameras[0],true,1440,810);
    snprintf(path,sizeof(path),"%s/surroundings-fallback.png",directory);assert(ExportImage(image,path));UnloadImage(image);
    art->motel_surroundings[0]=shoulder;art->motel_surroundings[1]=bank;
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    printf("PASS surroundings graphics: original 62240 triangles/25 intact material draws, PBR maps, east/west/context views, independent rock visibility (%d changed pixels), bank-owned foliage and exact collision fallback\n",changed);
}
static void room101_graphics(SwatView* view,const char* directory) {
    SwatEnvironmentArt* art=&view->environment;
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.hostile_fire=false;
    swat_sim_init(&sim,config,73); swat_environment_art_prepare_location(art,&sim.world);
    surroundings_graphics(view,directory);
    ground_graphics(view,directory);
    masonry_silhouette(view,directory);
    Texture2D asphalt_maps[]={art->motel_asphalt.color,art->motel_asphalt.normal,art->motel_asphalt.roughness};
    const int coordinates[4][2]={{0,0},{127,311},{1023,1023},{512,512}};
    // Independent PNG16 decode retained in the asphalt handoff: GPU upload
    // preserves normalized channel values, reverses rows once, never gamma-decodes data maps.
    const int expected[3][4][3]={{{72,73,73},{65,67,68},{89,87,84},{65,66,67}},
        {{118,126,253},{139,132,253},{125,121,250},{117,131,253}},{{167,167,167},{162,162,162},{182,182,182},{154,154,154}}};
    for(int m=0;m<3;m++) {
        assert(asphalt_maps[m].id && asphalt_maps[m].width==1024 && asphalt_maps[m].mipmaps==11);
        Image image=LoadImageFromTexture(asphalt_maps[m]);assert(image.data);Color* pixels=LoadImageColors(image);
        for(int i=0;i<4;i++) {
            Color p=pixels[(1023-coordinates[i][1])*1024+coordinates[i][0]];
            assert(abs(p.r-expected[m][i][0])<=1 && abs(p.g-expected[m][i][1])<=1 && abs(p.b-expected[m][i][2])<=1);
        }
        UnloadImageColors(pixels);UnloadImage(image);
    }
    assert(art->motel_asphalt.tile.x==2.1f && sim.world.objects[0].material==SWAT_CONCRETE);
    puts("PASS asphalt: PNG16 normalized GPU samples, flipped rows, linear data maps, 2.1 m repeat and original ground physics");
    Model roadside=art->motel_roadside;assert(roadside.meshCount==7);int roadside_triangles=0;
    for(int i=0;i<roadside.meshCount;i++)roadside_triangles+=roadside.meshes[i].triangleCount;
    assert(roadside_triangles==319);
    BoundingBox roadside_bounds=GetModelBoundingBox(roadside),old_roadside_bounds=GetModelBoundingBox(art->motel[14]);
    assert(Vector3Distance(roadside_bounds.min,old_roadside_bounds.min)<.0001f && Vector3Distance(roadside_bounds.max,old_roadside_bounds.max)<.0001f);
    assert(room101_owner_pixels(art,&sim.world.objects[140],false)>50);
    sim.world.objects[140].active=false;assert(!room101_owner_pixels(art,&sim.world.objects[140],false));sim.world.objects[140].active=true;
    assert(art->room101_ready);
    assert(art->room101_v3_ready && art->room101_v4_ready);
    assert(art->masonry_edge.meshCount==4);int edge_triangles=0;
    for(int i=0;i<art->masonry_edge.meshCount;i++)edge_triangles+=art->masonry_edge.meshes[i].triangleCount;
    assert(edge_triangles==164);
    assert(art->motel_reception.meshCount==18);
    BoundingBox sign_bounds=GetModelBoundingBox(art->motel_reception);
    assert(fabsf(sign_bounds.min.y+.3f)<1e-5f && fabsf(sign_bounds.max.y)<1e-5f);
    assert(2.4f+sign_bounds.min.y>1.8288f); // Standing controller clears the panel.
    int sign_triangles=0;for(int i=0;i<art->motel_reception.meshCount;i++)sign_triangles+=art->motel_reception.meshes[i].triangleCount;
    assert(sign_triangles==36);
    assert(room101_owner_pixels(art,&sim.world.objects[5],false)>10);
    assert(room101_owner_pixels(art,&sim.world.objects[5],true)==0);
    sim.world.objects[5].active=false;assert(room101_owner_pixels(art,&sim.world.objects[5],false)==0);sim.world.objects[5].active=true;
    for(int room=0;room<3;room++) {
        Model number=art->motel_numbers[room];assert(number.meshCount==4);
        int triangles=0;for(int m=0;m<number.meshCount;m++)triangles+=number.meshes[m].triangleCount;
        assert(triangles==132);
        int owner=43+24*room;assert(swat_motel_instance(owner-1)->asset==17);
        assert(room101_owner_pixels(art,&sim.world.objects[owner],true)>10);
        sim.world.objects[owner].active=false;assert(room101_owner_pixels(art,&sim.world.objects[owner],true)==0);sim.world.objects[owner].active=true;
    }
    const int v4_originals[]={25,36,24,29,1,22,4,10},v4_owners[]={23,28,22,29,14,20,9,17};
    before=sim.world;
    for(int i=0;i<SWAT_ROOM101_V4_ASSETS;i++) {
        Model model=art->room101_v4[i];assert(model.meshCount);
        BoundingBox a=GetModelBoundingBox(model),b=GetModelBoundingBox(art->motel[v4_originals[i]]);
        assert(a.min.x>=b.min.x-.001f && a.min.y>=b.min.y-.001f && a.min.z>=b.min.z-.001f);
        assert(a.max.x<=b.max.x+.001f && a.max.y<=b.max.y+.001f && a.max.z<=b.max.z+.001f);
        SwatObject* o=&sim.world.objects[v4_owners[i]];
        if(!o->active) {
            assert(o->wall_group && !room101_owner_pixels(art,o,false));
            SwatObject* piece=&sim.world.objects[o->wall_group-1];
            assert(room101_owner_pixels(art,piece,false)>50);
            piece->active=false;assert(!room101_owner_pixels(art,piece,false));piece->active=true;
        } else {
        assert(room101_owner_pixels(art,o,false)>50);
        o->active=false;assert(!room101_owner_pixels(art,o,false));o->active=true;
        }
        for(int m=1;m<model.materialCount;m++)assert(model.materials[m].maps[MATERIAL_MAP_ALBEDO].color.a==255);
    }
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    puts("PASS v4: eight bounded replacements, original ownership/removal, opaque surfaces and immutable authority");
    art->room101_v4_ready=false;
    for(int i=0;i<SWAT_ROOM101_V3_ASSETS;i++)assert(art->room101_v3[i].meshCount);
    const int original_assets[10]={10,18,17,4,2,2,22,5,25,36};
    for(int i=0;i<10;i++) {
        // Node hierarchy and original nonuniform instance scale remain intact;
        // every replacement stays within its source geometry's local envelope.
        BoundingBox a=GetModelBoundingBox(art->room101_v3[i]),b=GetModelBoundingBox(art->motel[original_assets[i]]);
        assert(a.min.x>=b.min.x-.001f && a.min.y>=b.min.y-.001f && a.min.z>=b.min.z-.001f);
        assert(a.max.x<=b.max.x+.001f && a.max.y<=b.max.y+.001f && a.max.z<=b.max.z+.001f);
    }
    int ao_materials=0;
    for(int m=0;m<art->room101_v3[3].materialCount;m++) {
        Material material=art->room101_v3[3].materials[m];
        if(material.maps[MATERIAL_MAP_OCCLUSION].texture.id) {
            assert(fabsf(material.maps[MATERIAL_MAP_OCCLUSION].value-.30f)<1e-5f);
            assert(fabsf(art->room101_v3_normal_scale[3][m]-.18f)<1e-5f);ao_materials++;
        }
    }
    assert(ao_materials==1);
    const int owners[10]={17,18,19,9,105,106,20,10,23,28};
    for(int i=0;i<10;i++) {
        SwatObject* o=&sim.world.objects[owners[i]];
        if(!o->active) {assert(o->wall_group && !room101_owner_pixels(art,o,false));continue;}
        assert(room101_owner_pixels(art,o,false)>50);
        o->active=false;assert(!room101_owner_pixels(art,o,false));o->active=true;
        if(i==7)assert(!room101_owner_pixels(art,o,true));
    }
    puts("PASS v3 ownership: all ten replacements visible, children removed with parent, roof hidden in cutaway");
    art->room101_v3_ready=false;
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
    Camera3D inside={{-7.2f,1.62f,-2.1f},{-6.45f,1.22f,-5.2f},{0,1,0},64,CAMERA_PERSPECTIVE};
    Image room=room101_capture(view,inside,true);
    snprintf(path,sizeof(path),"%s/room101-interior.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    art->room101_v3_ready=true;
    room=room101_capture(view,camera,true);snprintf(path,sizeof(path),"%s/room101-v3.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    room=room101_capture(view,inside,true);snprintf(path,sizeof(path),"%s/room101-v3-interior.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    Color* a=LoadImageColors(captures[0]),*b=LoadImageColors(captures[1]); int changed=0;
    for(int i=0;i<960*800;i++) changed+=abs(a[i].r-b[i].r)+abs(a[i].g-b[i].g)+abs(a[i].b-b[i].b)>12;
    assert(changed>5000);
    UnloadImageColors(a); UnloadImageColors(b); UnloadImage(captures[0]); UnloadImage(captures[1]);
    // Actual motel row from outside, with every authored entrance open.
    for(int i=1;i<sim.world.count;i++)if(sim.world.objects[i].door) {
        SwatObject* o=&sim.world.objects[i];o->yaw=o->closed_yaw+100*SWAT_RAD;
        o->center=b3OffsetPos(o->hinge,swat_v(sinf(o->yaw)*o->half.z,0,cosf(o->yaw)*o->half.z));
    }
    Camera3D row={{-4,1.64f,8},{-4,1.35f,-3},{0,1,0},54,CAMERA_PERSPECTIVE};
    room=room101_capture_size(view,row,true,1440,810);snprintf(path,sizeof(path),"%s/motel-all-room-shadows.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    Camera3D bed={{-7.15f,1.62f,-.67f},{-5.74f,1.05f,-2.93f},{0,1,0},59.863f,CAMERA_PERSPECTIVE};
    Camera3D window={{-6.35f,1.55f,-2.6f},{-5.45f,1.45f,0},{0,1,0},45.781f,CAMERA_PERSPECTIVE};
    room=room101_capture(view,bed,true);snprintf(path,sizeof(path),"%s/room101-v3-bed.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    room=room101_capture(view,window,true);snprintf(path,sizeof(path),"%s/room101-v3-window.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    art->room101_v4_ready=true;
    Model desk=art->room101_desk;assert(desk.meshCount);
    BoundingBox desk_box=GetModelBoundingBox(desk),original_box=GetModelBoundingBox(art->motel[26]);
    assert(fabsf(desk_box.min.x-original_box.min.x)<1e-5f && fabsf(desk_box.max.x-original_box.max.x)<1e-5f);
    assert(fabsf(desk_box.min.y-original_box.min.y)<1e-5f && fabsf(desk_box.max.y-(original_box.max.y-.0023f))<1e-4f);
    assert(fabsf(desk_box.min.z-original_box.min.z)<1e-5f && fabsf(desk_box.max.z-original_box.max.z)<1e-5f);
    int desk_triangles=0;for(int m=0;m<desk.meshCount;m++)desk_triangles+=desk.meshes[m].triangleCount;
    assert(desk_triangles==544 && desk.materialCount==7);
    assert(fabsf(art->room101_desk_normal_scale[5]-.3f)<1e-5f);
    assert(desk.materials[5].maps[MATERIAL_MAP_NORMAL].texture.id && desk.materials[5].maps[MATERIAL_MAP_METALNESS].value==0);
    before=sim.world;
    Camera3D desk_views[]={
        {{-6.6f,1.35f,-1.7f},{-7.49f,.85f,-2.28f},{0,1,0},45,CAMERA_PERSPECTIVE},bed};
    for(int candidate=0;candidate<2;candidate++) {
        art->room101_desk=candidate?desk:(Model){0};
        for(int i=0;i<2;i++) {
            room=room101_capture(view,desk_views[i],true);
            snprintf(path,sizeof(path),"%s/desk-w2-%s-%s.png",directory,i?"room":"detail",candidate?"after":"before");
            assert(ExportImage(room,path));UnloadImage(room);
        }
    }
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    SwatObject* desk_owner=&sim.world.objects[24];assert(room101_owner_pixels(art,desk_owner,false)>50);
    desk_owner->active=false;assert(!room101_owner_pixels(art,desk_owner,false));desk_owner->active=true;
    puts("PASS desk W2: 544 triangles, ridge removal with original authority, 0.3 normal scale, matched engine captures and support removal");
    Model guest_desk=art->motel_guest_desk;assert(guest_desk.meshCount && guest_desk.materialCount==art->motel[26].materialCount);
    int guest_triangles=0;for(int m=0;m<guest_desk.meshCount;m++)guest_triangles+=guest_desk.meshes[m].triangleCount;
    assert(guest_triangles==496);
    BoundingBox guest_box=GetModelBoundingBox(guest_desk);
    assert(fabsf(guest_box.max.y-(original_box.max.y-.0023f))<1e-4f);
    before=sim.world;
    Camera3D guest_view={{1.3f,1.30f,-1.7f},{.65f,.78f,-2.15f},{0,1,0},40,CAMERA_PERSPECTIVE};
    for(int candidate=0;candidate<2;candidate++) {
        art->motel_guest_desk=candidate?guest_desk:(Model){0};
        room=room101_capture_size(view,guest_view,true,1440,810);
        snprintf(path,sizeof(path),"%s/guest-desk-%s.png",directory,candidate?"after":"before");assert(ExportImage(room,path));UnloadImage(room);
    }
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    for(int owner=48;owner<=96;owner+=24) {
        assert(swat_motel_instance(owner-1)->asset==26);
        SwatObject* o=&sim.world.objects[owner];assert(room101_owner_pixels(art,o,false)>50);
        o->active=false;assert(!room101_owner_pixels(art,o,false));o->active=true;
    }
    puts("PASS original guest desk: 496 triangles, explicit owners 48/72/96, unchanged authority/support, original fallback and matched native captures");
    for(int i=SWAT_MOTEL_BASE_ASSETS;i<42;i++) {
        Model model=art->motel[i];assert(model.meshCount);
        for(int m=0;m<model.meshCount;m++)assert(model.meshes[m].texcoords2 && model.meshes[m].vboId[5]);
        for(int m=1;m<model.materialCount;m++)assert(art->motel_occlusion_uv[i][m]==1);
    }
    for(int i=SWAT_MOTEL_BASE_INSTANCES+1;i<=SWAT_MOTEL_UTILITY_INSTANCES;i++) {
        SwatObject* o=&sim.world.objects[i];assert(room101_owner_pixels(art,o,false)>50);
        o->active=false;assert(!room101_owner_pixels(art,o,false));o->active=true;
    }
    Camera3D utility_views[]={
        {{-5.55f,.9f,-1.40f},{-4.55f,.25f,-.65f},{0,1,0},45,CAMERA_PERSPECTIVE},
        {{-6.8f,.85f,-3.45f},{-7.70f,.15f,-3.3f},{0,1,0},38,CAMERA_PERSPECTIVE},
        {{-6.25f,1.62f,-3.65f},{-6,.5f,-.6f},{0,1,0},65,CAMERA_PERSPECTIVE}};
    const char* utility_names[]={"rack","basket","room"};
    for(int i=0;i<3;i++) {
        room=room101_capture(view,utility_views[i],true);
        snprintf(path,sizeof(path),"%s/utility-%s.png",directory,utility_names[i]);assert(ExportImage(room,path));UnloadImage(room);
    }
    puts("PASS utility props: original metre-scale meshes, second UV buffers/material binding and per-instance removal");
    Camera3D additions[]={
        {{6,1.64f,11},{8,1,0},{0,1,0},68,CAMERA_PERSPECTIVE},
        {{10.5f,1.5f,4.5f},{12.5f,.85f,3},{0,1,0},58,CAMERA_PERSPECTIVE},
        {{11.4f,.35f,7},{12.5f,.7f,2},{0,1,0},58,CAMERA_PERSPECTIVE},
        {{1.3f,1.30f,-1.7f},{.65f,.78f,-2.15f},{0,1,0},40,CAMERA_PERSPECTIVE},
        {{1.5f,1.62f,-.6f},{.6f,.85f,-2.3f},{0,1,0},62,CAMERA_PERSPECTIVE}};
    const char* addition_names[]={"east-court","fence-detail","fence-grazing","room103-personal-detail","room103-personal-room"};
    for(int i=0;i<5;i++) {
        room=room101_capture_size(view,additions[i],true,1440,810);
        snprintf(path,sizeof(path),"%s/%s.png",directory,addition_names[i]);assert(ExportImage(room,path));UnloadImage(room);
    }
    const int personal_triangles[]={1428,1400};
    for(int i=0;i<2;i++) {
        Model model=art->motel_personal[i];assert(model.meshCount);int triangles=0;
        for(int m=0;m<model.meshCount;m++)triangles+=model.meshes[m].triangleCount;
        assert(triangles==personal_triangles[i]);
        BoundingBox bounds=GetModelBoundingBox(model);
        assert(fabsf(bounds.min.y)<1e-5f && bounds.max.x<.065f && bounds.min.x>-.065f);
        float yaw=(i?-12:8)*SWAT_RAD,minimum=100,maximum=-100;
        for(int m=0;m<model.meshCount;m++)for(int v=0;v<model.meshes[m].vertexCount;v++) {
            float* p=&model.meshes[m].vertices[3*v];
            float x=.8f+cosf(yaw)*p[0]+sinf(yaw)*p[2];minimum=fminf(minimum,x);maximum=fmaxf(maximum,x);
        }
        // Tray is really +90 degrees: its local Z supplies the east edge.
        BoundingBox tray=GetModelBoundingBox(art->motel_dressing[3]);
        float clearance=minimum-(.51f+tray.max.z);
        assert(clearance>.065f && maximum<.925f);
        printf("Room103 prop%d: tray clearance %.5fm, desktop edge %.5fm\n",i,clearance,.925f-maximum);
    }
    SwatObject* personal_owner=&sim.world.objects[72];
    assert(room101_owner_pixels(art,personal_owner,false)>50);
    personal_owner->active=false;assert(!room101_owner_pixels(art,personal_owner,false));personal_owner->active=true;
    puts("PASS east fence and Room 103: native context/detail captures, original personal prop scale/topology");
    for(int i=0;i<SWAT_MOTEL_DRESSING_ASSETS;i++) {
        Model model=art->motel_dressing[i];assert(model.meshCount);
        for(int m=1;m<model.materialCount;m++)assert(art->motel_dressing_occlusion_uv[i][m]==1);
        for(int m=0;m<model.meshCount;m++)assert(model.meshes[m].texcoords2);
    }
    Camera3D dressing_views[]={
        {{-6.15f,1.3f,-4.5f},{-5.35f,1.2f,-5.9f},{0,1,0},55,CAMERA_PERSPECTIVE},
        {{-6.6f,1.35f,-1.7f},{-7.49f,.85f,-2.28f},{0,1,0},45,CAMERA_PERSPECTIVE},
        {{-6.3f,1.6f,-2.25f},{-5.4f,1.65f,-3.94f},{0,1,0},48,CAMERA_PERSPECTIVE},
        {{-7.22f,1.60f,.45f},{-7.2f,1.60f,.0345f},{0,1,0},45,CAMERA_PERSPECTIVE},
        {{-7.35f,1.25f,-1.5f},{-7.89f,.99f,-.95f},{0,1,0},45,CAMERA_PERSPECTIVE},
        {{-6.9f,1.38f,-1.88f},{-7.67f,.758f,-1.88f},{0,1,0},38,CAMERA_PERSPECTIVE}};
    const char* dressing_names[]={"bathroom","desk","print","viewer","bumper","folder"};
    SwatObject saved_door=sim.world.objects[16];sim.world.objects[16].yaw=sim.world.objects[16].closed_yaw;
    for(int i=0;i<6;i++) {
        room=room101_capture(view,dressing_views[i],true);
        snprintf(path,sizeof(path),"%s/dressing-%s.png",directory,dressing_names[i]);assert(ExportImage(room,path));UnloadImage(room);
    }
    sim.world.objects[16]=saved_door;
    assert(art->motel_dressing[8].meshCount==4);
    int folder_triangles=0;for(int i=0;i<art->motel_dressing[8].meshCount;i++)folder_triangles+=art->motel_dressing[8].meshes[i].triangleCount;
    assert(folder_triangles==432);
    puts("PASS dressing: nine embedded PBR models, second UV AO, metre-scale supported mounts and 432-triangle guest folder");
    Camera3D lamp_view={{-5.25f,1.48f,-3.2f},{-4.65f,1.34f,-3.83f},{0,1,0},40,CAMERA_PERSPECTIVE};
    room=room101_capture(view,lamp_view,true);snprintf(path,sizeof(path),"%s/reading-lamp.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    SwatMotelInstance lamp_mount;b3Pos bulb;int lamp_owner=swat_motel_dressing(&sim.world,7,&lamp_mount);
    assert(lamp_owner>0 && swat_motel_lamp(&sim.world,1,&bulb));
    Image lamp_on=room101_capture(view,bed,true);sim.world.room_light_off_mask=1u<<1;
    Image lamp_off=room101_capture(view,bed,true);
    snprintf(path,sizeof(path),"%s/reading-lamp-room-on.png",directory);assert(ExportImage(lamp_on,path));
    snprintf(path,sizeof(path),"%s/reading-lamp-room-off.png",directory);assert(ExportImage(lamp_off,path));
    Color* on_pixels=LoadImageColors(lamp_on);Color* off_pixels=LoadImageColors(lamp_off);int lighting_changed=0;
    for(int i=0;i<lamp_on.width*lamp_on.height;i++)lighting_changed+=abs(on_pixels[i].r-off_pixels[i].r)>12;
    assert(lighting_changed>1000);sim.world.room_light_off_mask=0;
    sim.world.objects[lamp_owner].active=false;
    assert(!swat_motel_lamp(&sim.world,1,&bulb));
    Image removed=room101_capture(view,bed,true);Color* removed_pixels=LoadImageColors(removed);int removal_changed=0;
    for(int i=0;i<lamp_on.width*lamp_on.height;i++)removal_changed+=abs(on_pixels[i].r-removed_pixels[i].r)>12;
    assert(removal_changed>1000);sim.world.objects[lamp_owner].active=true;
    UnloadImageColors(removed_pixels);UnloadImage(removed);
    UnloadImageColors(on_pixels);UnloadImageColors(off_pixels);UnloadImage(lamp_on);UnloadImage(lamp_off);
    puts("PASS reading lamps: supported anchors, existing room shadow slot, switches and support removal extinguish actual illumination");
    Camera3D holder_views[]={
        {{-5.88f,1.1f,-5.07f},{-5.48f,.72f,-5.83f},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{-5.12f,1.05f,-5.38f},{-5.48f,.72f,-5.83f},{0,1,0},65,CAMERA_PERSPECTIVE}};
    for(int i=0;i<2;i++) {
        room=room101_capture(view,holder_views[i],true);
        snprintf(path,sizeof(path),"%s/holder-%s.png",directory,i?"side":"seated");assert(ExportImage(room,path));UnloadImage(room);
    }
    Camera3D v4_cameras[]={camera,inside,bed,window,row};
    const char* names[]={"entrance","bathroom","bed","window","row"};
    before=sim.world;
    for(int i=0;i<5;i++) {
        room=room101_capture(view,v4_cameras[i],true);
        snprintf(path,sizeof(path),"%s/room101-v4-%s.png",directory,names[i]);assert(ExportImage(room,path));UnloadImage(room);
    }
    view->lighting.lamp_shadows=false;
    room=room101_capture(view,window,true);snprintf(path,sizeof(path),"%s/room101-v4-window-no-lamp-shadow.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    view->lighting.lamp_shadows=true;
    float scales[SWAT_ROOM101_MATERIALS];memcpy(scales,art->room101_v4_normal_scale[7],sizeof(scales));
    memset(art->room101_v4_normal_scale[7],0,sizeof(scales));
    room=room101_capture(view,window,true);snprintf(path,sizeof(path),"%s/room101-v4-window-no-normal.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    memcpy(art->room101_v4_normal_scale[7],scales,sizeof(scales));
    assert(!memcmp(&before,&sim.world,sizeof(before)));
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
    sim.world=before; // Restore the full scene after isolated door-render checks.
    for(int i=0;i<sim.world.count;i++)if(sim.world.objects[i].door)sim.world.objects[i].yaw=sim.world.objects[i].closed_yaw;
    Camera3D breach={{-5.7f,1.55f,-1.18f},{-3.1f,1.1f,-1.18f},{0,1,0},65,CAMERA_PERSPECTIVE};
    for(int stage=0;stage<2;stage++) {
        if(stage) {assert(swat_world_breach(&sim.world,sim.world.objects[106].wall_group-1,(b3Pos){-3.9885f,1.05f,-1.17f})>4);}
        room=room101_capture(view,breach,true);snprintf(path,sizeof(path),"%s/motel-breach-%d.png",directory,stage);assert(ExportImage(room,path));UnloadImage(room);
    }
    Camera3D exterior={{-6.7f,1.6f,-9.5f},{-6.7f,1.1f,-5.5f},{0,1,0},65,CAMERA_PERSPECTIVE};
    for(int stage=0;stage<2;stage++) {
        if(stage) {
            SwatHit hit=swat_world_ray(&sim.world,(b3Pos){-6.7f,1,-7},swat_v(0,0,1),2,b3_nullBodyId);
            assert(hit.hit && sim.world.objects[hit.index].material==SWAT_BRICK);
            assert(swat_world_breach(&sim.world,hit.index,hit.point)>0);
        }
        room=room101_capture(view,exterior,true);snprintf(path,sizeof(path),"%s/motel-masonry-%d.png",directory,stage);assert(ExportImage(room,path));UnloadImage(room);
    }
    Camera3D edge_close={{-6.75f,1.38f,-6.7f},{-7.3333f,1.15f,-5.99f},{0,1,0},55,CAMERA_PERSPECTIVE};
    room=room101_capture(view,edge_close,true);snprintf(path,sizeof(path),"%s/motel-masonry-edge-close.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    // Optional edge strips appear only along removed neighbors, then follow support removal.
    Model saved_edge=art->masonry_edge;
    art->masonry_edge=(Model){0};room=room101_capture(view,exterior,true);
    snprintf(path,sizeof(path),"%s/motel-masonry-base.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    art->masonry_edge=LoadModelFromMesh(GenMeshCube(.184f,.004f,1));
    art->masonry_edge.materials[0].maps[MATERIAL_MAP_ALBEDO].color=RED;
    room=room101_capture(view,exterior,false);Color* edge_pixels=LoadImageColors(room);int red=0;
    for(int i=0;i<room.width*room.height;i++)red+=edge_pixels[i].r>200 && edge_pixels[i].g<80 && edge_pixels[i].b<80;
    assert(red>100);UnloadImageColors(edge_pixels);UnloadImage(room);
    UnloadModel(art->masonry_edge);art->masonry_edge=saved_edge;
    for(int i=0;i<4;i++) {
        float x=-6.4f+4*i;Camera3D plaque={{x,1.92f,1.05f},{x,1.92f,.11f},{0,1,0},35,CAMERA_PERSPECTIVE};
        room=room101_capture(view,plaque,true);snprintf(path,sizeof(path),"%s/room-number-%d.png",directory,101+i);assert(ExportImage(room,path));UnloadImage(room);
    }
    Camera3D reception={{-10.3f,1.65f,5.2f},{-10,1.65f,0},{0,1,0},65,CAMERA_PERSPECTIVE};
    room=room101_capture(view,reception,true);snprintf(path,sizeof(path),"%s/reception-approach.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    Camera3D court={{-1,1.65f,9},{-2,.5f,0},{0,1,0},68,CAMERA_PERSPECTIVE};
    Camera3D roadside_view={{10,1.65f,18},{9.7f,2,8},{0,1,0},50,CAMERA_PERSPECTIVE};
    room=room101_capture_size(view,roadside_view,true,1440,810);snprintf(path,sizeof(path),"%s/roadside-parking.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    Camera3D grazing={{4,.35f,7},{1,0,2},{0,1,0},68,CAMERA_PERSPECTIVE};
    room=room101_capture_size(view,court,true,1440,810);snprintf(path,sizeof(path),"%s/asphalt-walking.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    room=room101_capture_size(view,grazing,true,1440,810);snprintf(path,sizeof(path),"%s/asphalt-grazing.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    Camera3D fence={{10.1f,1.5f,4.8f},{12.5f,.85f,3},{0,1,0},58,CAMERA_PERSPECTIVE};
    Image intact=room101_capture_size(view,fence,true,1440,810);
    snprintf(path,sizeof(path),"%s/fence-intact.png",directory);assert(ExportImage(intact,path));
    SwatHit rail=swat_world_ray(&sim.world,(b3Pos){12,1.42f,3},swat_v(1,0,0),1,b3_nullBodyId);
    assert(rail.hit && swat_world_breach(&sim.world,rail.index,rail.point)>0);
    Image cut=room101_capture_size(view,fence,true,1440,810);
    snprintf(path,sizeof(path),"%s/fence-cut.png",directory);assert(ExportImage(cut,path));
    Color* p0=LoadImageColors(intact),*p1=LoadImageColors(cut);int fence_changed=0;
    for(int i=0;i<1440*810;i++)fence_changed+=abs(p0[i].r-p1[i].r)+abs(p0[i].g-p1[i].g)+abs(p0[i].b-p1[i].b)>30;
    assert(fence_changed>1000);UnloadImageColors(p0);UnloadImageColors(p1);UnloadImage(intact);UnloadImage(cut);
    // Art opt-out still presents the exact surviving collision triangles.
    Model saved_fence=art->motel[42];art->motel[42]=(Model){0};
    room=room101_capture_size(view,fence,true,1440,810);snprintf(path,sizeof(path),"%s/fence-cut-fallback.png",directory);assert(ExportImage(room,path));UnloadImage(room);
    art->motel[42]=saved_fence;
    puts("PASS fence graphics: cached intact/cut material batches, surviving posts, changed shadow silhouette and exact collision-art fallback");
    swat_sim_close(&sim);
    printf("PASS Room 101: authored PBR channels/scales, transformed trim, matched captures (%d changed pixels), three door poses and removal\n",changed);
}
static void texture_owner_graphics(const char* asset) {
    char path[4096];snprintf(path,sizeof(path),"%sassets/environment/%s",GetApplicationDirectory(),asset);
    Model models[3];
    for(int n=0;n<3;n++) {
        models[n]=LoadModel(path);assert(models[n].meshCount);
        for(int m=0;m<models[n].materialCount;m++)for(int k=0;k<=MATERIAL_MAP_BRDF;k++) {
            Texture2D* t=&models[n].materials[m].maps[k].texture;
            if(t->id && t->id!=rlGetTextureIdDefault()) {GenTextureMipmaps(t);SetTextureFilter(*t,TEXTURE_FILTER_TRILINEAR);SetTextureWrap(*t,TEXTURE_WRAP_REPEAT);}
        }
        swat_art_model_share_textures(models[n],path);
    }
    SwatArtTextureStats stats=swat_art_texture_stats();assert(stats.textures>0 && stats.owners==3*stats.textures && stats.saved_bytes==2*stats.bytes);
    swat_art_model_share_textures(models[0],path);assert(swat_art_texture_stats().owners==stats.owners);
    Texture2D a=models[0].materials[1].maps[MATERIAL_MAP_ALBEDO].texture,b=models[1].materials[1].maps[MATERIAL_MAP_ALBEDO].texture;
    assert(a.id==b.id);Image pixels=LoadImageFromTexture(a);assert(pixels.data);
    for(int n=0;n<2;n++) {
        swat_art_model_close(models[n]);assert(swat_art_texture_stats().owners==(size_t)(2-n)*stats.textures);
        Image retained=LoadImageFromTexture(b);assert(retained.data && retained.format==pixels.format);
        assert(!memcmp(pixels.data,retained.data,(size_t)GetPixelDataSize(pixels.width,pixels.height,pixels.format)));UnloadImage(retained);
    }
    for(int m=0;m<models[2].materialCount;m++)for(int k=0;k<=MATERIAL_MAP_BRDF;k++)
        if(models[2].materials[m].maps[k].texture.id==b.id)models[2].materials[m].maps[k].texture=(Texture2D){0};
    swat_art_model_close(models[2]);UnloadImage(pixels);assert(!swat_art_texture_stats().textures);
    puts("PASS texture owners: three exact image copies share once per model, idempotent registration, closing two owners retains pixels, final release frees storage");
}
static void texture_sharing_graphics(SwatView* view,const char* directory) {
    texture_owner_graphics("motel_room101_v4/room_floor_4x6m_008_v4.glb");
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;swat_sim_init(&sim,cfg,42);
    Camera3D cameras[]={
        {{-6,1.62f,-1},{-6,1.4f,-5},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{0,1.62f,16},{0,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{10,1.62f,12},{6,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE}};
    Image full[3];environment("SWAT_ART_TEXTURE_SHARING","0");swat_environment_art_prepare_location(&view->environment,&sim.world);
    for(int c=0;c<3;c++)full[c]=room101_capture_size(view,cameras[c],true,640,480);
    static SwatWorld empty_world;swat_environment_art_prepare_location(&view->environment,&empty_world);
    assert(!swat_art_texture_stats().textures);environment("SWAT_ART_TEXTURE_SHARING",NULL);
    swat_environment_art_prepare_location(&view->environment,&sim.world);
    SwatArtTextureStats stats=swat_art_texture_stats();assert(stats.saved_bytes>0);
    before=sim.world;
    for(int c=0;c<3;c++) {
        Image shared=room101_capture_size(view,cameras[c],true,640,480);Color* a=LoadImageColors(full[c]),*b=LoadImageColors(shared);int changed=0;
        for(int i=0;i<640*480;i++)changed+=memcmp(&a[i],&b[i],sizeof(Color))!=0;
        if(changed) {
            printf("Shared texture camera%d changed%d\n",c,changed);fflush(stdout);
            assert(ExportImage(full[c],TextFormat("%s/texture-original.png",directory)));
            assert(ExportImage(shared,TextFormat("%s/texture-shared.png",directory)));
        }
        assert(!changed);UnloadImageColors(a);UnloadImageColors(b);UnloadImage(full[c]);UnloadImage(shared);
    }
    assert(!memcmp(&before,&sim.world,sizeof(before)));swat_sim_close(&sim);
    printf("PASS shared motel textures: pixel-identical PBR indoor/distant-room renders; %.2f MiB duplicate allocation removed, %zu textures/%zu owners\n",stats.saved_bytes/1048576.0,stats.textures,stats.owners);
}
static void roadside_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,42);swat_environment_art_prepare_location(&view->environment,&sim.world);
    SwatEnvironmentArt* art=&view->environment;assert(art->motel_road_paint.meshCount==2);
    int triangles=0;for(int i=0;i<2;i++)triangles+=art->motel_road_paint.meshes[i].triangleCount;
    assert(triangles==40 && art->motel_foliage[0].meshCount==1 && art->motel_foliage[1].meshCount==1);
    assert(art->motel_foliage[0].meshes[0].triangleCount==840 && art->motel_foliage[1].meshes[0].triangleCount==2294);
    for(int i=0;i<2;i++) {
        Material m=art->motel_road_paint.materials[art->motel_road_paint.meshMaterial[i]];
        assert(m.maps[MATERIAL_MAP_ALBEDO].texture.id && m.maps[MATERIAL_MAP_ROUGHNESS].texture.id);
        assert(m.maps[MATERIAL_MAP_ALBEDO].color.a==255);
    }
    before=sim.world;
    Camera3D cameras[]={
        {{0,1.65f,42},{0,1,-2},{0,1,0},70,CAMERA_PERSPECTIVE},
        {{38,12,48},{0,0,8},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{-40,.45f,46},{-15,-.079f,40},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{-56,1.5f,-27},{-54,0,-21},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{54,1.5f,-7},{55,0,-13},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{0,1.5f,-39},{0,0,-44},{0,1,0},65,CAMERA_PERSPECTIVE}};
    struct SwatEnvironmentBounds* bounds=art->bounds;
    for(int c=0;c<6;c++) {
        art->bounds=NULL;Image full=room101_capture_size(view,cameras[c],true,960,540);
        art->bounds=bounds;Image culled=room101_capture_size(view,cameras[c],true,960,540);
        Color* a=LoadImageColors(full),*b=LoadImageColors(culled);int changed=0;
        for(int i=0;i<960*540;i++)changed+=memcmp(&a[i],&b[i],sizeof(Color))!=0;
        printf("Road context camera%d culling differences%d\n",c,changed);fflush(stdout);assert(!changed);
        assert(ExportImage(culled,TextFormat("%s/road-context-%d.png",directory,c)));
        UnloadImageColors(a);UnloadImageColors(b);UnloadImage(full);UnloadImage(culled);
    }
    assert(!memcmp(&before,&sim.world,sizeof(before)));swat_sim_close(&sim);
    puts("PASS road context: 40 opaque paint triangles, two original grass meshes, six native road/plant views with identical full/culled lighting and shadows, immutable authority");
}
static void culling_graphics(SwatView* view,const char* directory) {
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.hostile_fire=false;
    swat_sim_init(&sim,cfg,42);swat_environment_art_prepare_location(&view->environment,&sim.world);
    struct SwatEnvironmentBounds* bounds=view->environment.bounds;assert(bounds);
    Camera3D cameras[]={
        {{-6,1.62f,-1},{-6,1.4f,-5},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{-10,1.62f,12},{-6,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{0,1.62f,16},{0,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{10,1.62f,12},{6,1.3f,-3},{0,1,0},75,CAMERA_PERSPECTIVE},
        {{-6.7f,1.6f,-9.5f},{-6.7f,1.1f,-5.5f},{0,1,0},65,CAMERA_PERSPECTIVE},
        {{0,25,12},{0,0,12},{0,0,-1},55,CAMERA_ORTHOGRAPHIC}};
    for(int stage=0;stage<2;stage++) {
        if(stage) {
            assert(swat_world_breach(&sim.world,sim.world.objects[106].wall_group-1,(b3Pos){-3.9885f,1.05f,-1.17f})>4);
            int first=sim.world.objects[13].wall_group-1;assert(first>SWAT_MOTEL_INSTANCES);
            b3Pos opening=sim.world.objects[first].center;opening.y=.6f;
            assert(swat_world_breach(&sim.world,first,opening)>0);
        }
        for(size_t c=0;c<sizeof(cameras)/sizeof(*cameras);c++) {
            before=sim.world;view->environment.bounds=NULL;
            Image full=room101_capture_size(view,cameras[c],true,640,480);
            view->environment.bounds=bounds;Image culled=room101_capture_size(view,cameras[c],true,640,480);
            Color* a=LoadImageColors(full),*b=LoadImageColors(culled);int changed=0;
            for(int i=0;i<640*480;i++)changed+=memcmp(&a[i],&b[i],sizeof(Color))!=0;
            if(changed) {
                printf("Culling stage%d camera%zu changed%d\n",stage,c,changed);fflush(stdout);
                assert(ExportImage(full,TextFormat("%s/cull-full.png",directory)));
                assert(ExportImage(culled,TextFormat("%s/cull-result.png",directory)));
            }
            assert(!changed && !memcmp(&before,&sim.world,sizeof(before)));
            UnloadImageColors(a);UnloadImageColors(b);UnloadImage(full);UnloadImage(culled);
        }
    }
    swat_sim_close(&sim);
    puts("PASS conservative culling: pixel-identical full/culled lit views, all shadow faces/contact depth, indoor/distant rooms/orthographic and breached geometry; immutable authority");
}
#include "ground_zoning_graphics.h"
#include "wall_depth_graphics.h"
#include "wall_core_batch_graphics.h"
#include "motel_props_graphics.h"
int main(int argc,char** argv) {
    const char* directory=argc>1 ? argv[1] : "build/swat";
    char path[4096];
    environment("SWAT_ENVIRONMENT_ART",NULL); environment("SWAT_ENVIRONMENT_ASSETS",NULL); environment("SWAT_PLASTER_STYLE",NULL);
    environment("SWAT_ENVIRONMENT_STYLE",NULL); environment("SWAT_ENVIRONMENT_PBR",NULL);
    environment("SWAT_MOTEL_ROOM101",NULL);
    SwatView view={0}; swat_view_init(&view,true); assert(IsWindowReady());
    if(argc>2 && !strcmp(argv[2],"wall-depth")) {wall_depth_graphics(&view,directory);swat_view_close(&view);return 0;}
    if(argc>2 && !strcmp(argv[2],"wall-batch")) {wall_core_batch_graphics(&view,directory);swat_view_close(&view);assert(!swat_art_texture_stats().textures);return 0;}
    if(argc>2 && !strcmp(argv[2],"motel-props")) {texture_owner_graphics("motel_props/motel_service_trolley.glb");texture_owner_graphics("motel_props/motel_fire_extinguisher.glb");texture_owner_graphics("motel_props/motel_reception_noticeboard.glb");motel_props_graphics(&view,directory);swat_view_close(&view);assert(!swat_art_texture_stats().textures);return 0;}
    if(argc>2 && !strcmp(argv[2],"textures")) {texture_sharing_graphics(&view,directory);swat_view_close(&view);assert(!swat_art_texture_stats().textures);return 0;}
    if(argc>2 && !strcmp(argv[2],"road-textures")) {
        texture_owner_graphics("motel_road_context/grass_tuft_low.glb");
        texture_owner_graphics("motel_road_context/dry_grass_seedheads.glb");
        texture_owner_graphics("motel_road_context/road_paint.glb");
        swat_view_close(&view);return 0;
    }
    if(argc>2 && !strcmp(argv[2],"culling")) {culling_graphics(&view,directory);swat_view_close(&view);return 0;}
    if(argc>2 && !strcmp(argv[2],"mounted")) {texture_owner_graphics("motel_mounted/motel_surface_junction.glb");mounted_graphics(&view,directory);swat_view_close(&view);assert(!swat_art_texture_stats().textures);return 0;}
    if(argc>2 && !strcmp(argv[2],"roadside")) {roadside_graphics(&view,directory);swat_view_close(&view);assert(!swat_art_texture_stats().textures);return 0;}
    if(argc>2 && (!strcmp(argv[2],"surroundings") || !strcmp(argv[2],"ground") || !strcmp(argv[2],"zoning"))) {
        SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.hostile_fire=false;
        swat_sim_init(&sim,config,73);swat_environment_art_prepare_location(&view.environment,&sim.world);
        if(!strcmp(argv[2],"ground"))ground_graphics(&view,directory);
        else if(!strcmp(argv[2],"zoning"))zoning_graphics(&view,directory);
        else surroundings_graphics(&view,directory);
        swat_sim_close(&sim);swat_view_close(&view);assert(!swat_art_texture_stats().textures);return 0;
    }
    if(argc>2 && !strcmp(argv[2],"personal")) {personal_shadow_review(&view,directory);swat_view_close(&view);return 0;}
    if(argc>2 && !strcmp(argv[2],"motel")) {room101_graphics(&view,directory);swat_view_close(&view);return 0;}
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
    assert(!view.environment.room101_v4_ready && !view.environment.room101_v4[0].meshCount);
    assert(!view.environment.room101_desk.meshCount);
    assert(!view.environment.motel_guest_desk.meshCount);
    assert(!view.environment.motel_ground.meshCount);
    assert(!view.environment.motel_surroundings[0].meshCount && !view.environment.motel_surroundings[1].meshCount);
    for(int i=0;i<3;i++)assert(!view.environment.motel_numbers[i].meshCount);
    assert(!view.environment.motel_reception.meshCount);
    assert(!view.environment.motel_roadside.meshCount);
    assert(!view.environment.motel_asphalt.color.id && !view.environment.motel_asphalt.normal.id && !view.environment.motel_asphalt.roughness.id);
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
