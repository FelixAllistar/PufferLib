// Small native graphics correctness check. Only the transformer's two embedded
// images are loaded; the surrounding level uses its ordinary graybox fallback.
#include "environment_art.h"
#include "lighting.h"
#include "protocol.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SwatSim sim;
static SwatWorld before;
static SwatEnvironmentArt art;
static SwatLighting lighting;
static void environment(const char* key,const char* value){
#ifdef _WIN32
    assert(_putenv_s(key,value)==0);
#else
    assert(setenv(key,value,1)==0);
#endif
}
static void piece(const SwatWorld* world,const SwatObject* o,bool shadow){
    if(swat_environment_motel_draw(&art,world,o,shadow,false)||!o->active)return;
    rlPushMatrix();rlTranslatef(o->center.x,o->center.y,o->center.z);swat_environment_object_rotate(o);
    if(!shadow&&!o->fractured&&!o->cuts&&swat_environment_art_draw(&art,o)){rlPopMatrix();return;}
    if(o->fractured||o->cuts)swat_environment_fragment_draw(o);
    else DrawCubeV((Vector3){0},(Vector3){2*o->half.x,2*o->half.y,2*o->half.z},shadow?WHITE:GRAY);
    rlPopMatrix();
}
static void shadows(void* context,const SwatSim* s,bool cutaway){
    (void)context;(void)cutaway;art.shadow_room=lighting.shadow_room;
    for(int i=0;i<swat_world_geometry_count(&s->world);i++)piece(&s->world,swat_world_geometry(&s->world,i),true);
    art.shadow_room=-1;
}
static void scene(const char* directory,const char* name){
    Camera3D camera={{14.2f,2.2f,-3.2f},{10.8f,.67f,-7.6f},{0,1,0},58,CAMERA_PERSPECTIVE};
    before=sim.world;lighting.prepared=false;
    swat_lighting_prepare_context(&lighting,&sim,camera.position,false,shadows,NULL);
    assert(lighting.enabled&&lighting.prepared);
    RenderTexture2D target=LoadRenderTexture(960,640);assert(target.id);
    BeginTextureMode(target);ClearBackground(MAGENTA);swat_lighting_sky(&lighting,camera,960,640);
    BeginMode3D(camera);swat_lighting_begin(&lighting,&art,&sim.world,camera.position);
    swat_environment_motel_cores_begin(&art,&sim.world);
    for(int i=0;i<swat_world_geometry_count(&sim.world);i++)piece(&sim.world,swat_world_geometry(&sim.world,i),false);
    swat_environment_motel_cores_end(&art);swat_lighting_end(&lighting,&art);EndMode3D();EndTextureMode();
    Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);char path[4096];snprintf(path,sizeof(path),"%s/%s.png",directory,name);assert(ExportImage(image,path));
    UnloadImage(image);UnloadRenderTexture(target);assert(!memcmp(&before,&sim.world,sizeof(before)));
}
static void silhouette(const char* directory,int part,int side,bool fallback){
    int first=SWAT_MOTEL_TRANSFORMER_FIRST;SwatObject* o=&sim.world.objects[first+part];SwatMotelInstance p;assert(swat_motel_transformer(&sim.world,first+part,&p));Model source=art.motel[p.asset];
    if(fallback)art.motel[p.asset]=(Model){0};
    Camera3D camera={{10.8f,.67f,-3.6f},{10.8f,.67f,-7.6f},{0,1,0},2.6f,CAMERA_ORTHOGRAPHIC};
    if(side)camera=(Camera3D){{14.0f,1.8f,-3.6f},{10.8f,.67f,-7.6f},{0,1,0},2.6f,CAMERA_ORTHOGRAPHIC};
    RenderTexture2D target=LoadRenderTexture(384,384);assert(target.id);
    before=sim.world;BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    assert(swat_environment_motel_draw(&art,&sim.world,o,false,false));EndMode3D();EndTextureMode();
    Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);Color* pixels=LoadImageColors(image);
    int count=b3Body_GetShapeCount(o->body);b3ShapeId shapes[32];assert(b3Body_GetShapes(o->body,shapes,32)==count);
    int checked=0,filled=0;
    for(int y=4;y<380;y+=8)for(int x=4;x<380;x+=8){
        bool hit=false,stable=true;const float offsets[][2]={{0,0},{-1.5f,0},{1.5f,0},{0,-1.5f},{0,1.5f}};
        for(int k=0;k<5;k++){
            Ray r=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[k][0],y+.5f+offsets[k][1]},camera,384,384);
            bool sample=false;for(int j=0;j<count;j++)sample|=b3Shape_RayCast(shapes[j],(b3Pos){r.position.x,r.position.y,r.position.z},swat_mul(swat_v(r.direction.x,r.direction.y,r.direction.z),8)).hit;
            if(!k)hit=sample;else stable&=sample==hit;
        }
        if(!stable)continue;
        Color p=pixels[y*384+x];bool visible=p.r!=255||p.g!=0||p.b!=255;
        if(visible!=hit)fprintf(stderr,"Transformer raster part%d side%d fallback%d pixel%d,%d visible%d hit%d\n",part,side,fallback,x,y,visible,hit);
        assert(visible==hit);checked++;filled+=visible;
    }
    char path[4096];snprintf(path,sizeof(path),"%s/part-%d-side-%d-%s.png",directory,part,side,fallback?"fallback":"art");assert(ExportImage(image,path));
    assert(checked>1000 && filled>25);assert(!memcmp(&before,&sim.world,sizeof(before)));
    printf("PASS transformer raster part%d side%d fallback%d: %d stable pixels, %d filled\n",part,side,fallback,checked,filled);
    UnloadImageColors(pixels);UnloadImage(image);UnloadRenderTexture(target);art.motel[p.asset]=source;
}
int main(int argc,char** argv){
    assert(argc==3);environment("SWAT_ENVIRONMENT_ASSETS",argv[2]);environment("SWAT_IBL","0");environment("SWAT_CONTACT_SHADOWS","0");
    environment("SWAT_ENVIRONMENT_ART","1");environment("SWAT_LIGHTING","1");environment("SWAT_EXPOSURE","1.1");
    SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(384,384,"SWAT transformer correctness");assert(IsWindowReady());
    swat_environment_art_init(&art);
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;swat_sim_init(&sim,cfg,81);
    swat_environment_art_prepare_location(&art,&sim.world);
    for(int i=0;i<SWAT_MOTEL_TRANSFORMER_ASSET_FIRST;i++)assert(!art.motel[i].meshCount);
    const int counts[]={2824,688,1164,108};
    for(int i=0;i<4;i++){
        const int asset=SWAT_MOTEL_TRANSFORMER_ASSET_FIRST+i;
        Model m=art.motel[asset];assert(m.meshCount==4&&m.materialCount==2);
        int total=0;for(int j=0;j<4;j++){assert(m.meshes[j].triangleCount==counts[j]);total+=m.meshes[j].triangleCount;}assert(total==4784);
        const SwatMotelAsset* a=swat_motel_asset(asset);const Mesh* mesh=&m.meshes[i];assert(mesh->triangleCount==a->triangle_count);
        for(int t=0;t<a->triangle_count;t++)for(int k=0;k<3;k++){
            int v=mesh->indices?mesh->indices[t*3+k]:t*3+k;
            b3Vec3 original=b3Add(a->center,a->vertices[a->indices[t*3+k]]);
            assert(b3Length(b3Sub(original,swat_v(mesh->vertices[v*3],mesh->vertices[v*3+1],mesh->vertices[v*3+2])))<1e-6f);
        }
        const Material* material=&m.materials[1],*shared=&art.motel[SWAT_MOTEL_TRANSFORMER_ASSET_FIRST].materials[1];
        assert(material->maps[MATERIAL_MAP_ALBEDO].texture.id==shared->maps[MATERIAL_MAP_ALBEDO].texture.id);
        assert(material->maps[MATERIAL_MAP_ROUGHNESS].texture.id==shared->maps[MATERIAL_MAP_ROUGHNESS].texture.id);
        assert(material->maps[MATERIAL_MAP_ROUGHNESS].value==a->materials[0].roughness&&material->maps[MATERIAL_MAP_METALNESS].value==a->materials[0].metalness);
        for(int side=0;side<2;side++)for(int fallback=0;fallback<2;fallback++)silhouette(argv[1],i,side,fallback);
    }
    assert(swat_art_texture_stats().textures==2);
    puts("PASS transformer native art: four original primitives, 4784 oriented triangles, one opaque PBR material, two shared maps, source/fallback silhouettes match physical owners");
    swat_lighting_init(&lighting);scene(argv[1],"scene-intact");
    assert(swat_world_damage(&sim.world,SWAT_MOTEL_TRANSFORMER_FIRST+2,100000));scene(argv[1],"scene-left-panel-removed");
    swat_lighting_close(&lighting);swat_sim_close(&sim);swat_environment_art_close(&art);assert(!swat_art_texture_stats().textures);CloseWindow();
    puts("PASS transformer native placement: production lighting, graybox maintenance-yard context, panel removal and immutable render authority; all textures released");
}
