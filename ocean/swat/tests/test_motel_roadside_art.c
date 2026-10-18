// Graphics correctness only. Run on an isolated software display for CPU review;
// the same executable can later qualify native rendering on the target machine.
#include "environment_art.h"
#include "lighting.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SwatSim sim;
static SwatWorld before;
static SwatEnvironmentArt art;
static SwatLighting lighting;
static void environment(const char* key,const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(key,value));
#else
    assert(!setenv(key,value,1));
#endif
}
static void prop(int model,bool shadow) {
    int first=swat_motel_roadside_first(model),count=swat_motel_roadside_count(model);
    for(int i=0;i<count;i++)if(sim.world.objects[first+i].active)assert(swat_environment_motel_draw(&art,&sim.world,&sim.world.objects[first+i],shadow,false));
}
static Camera3D camera(int model,int side,bool perspective) {
    SwatMotelInstance p;assert(swat_motel_roadside(&sim.world,swat_motel_roadside_first(model),&p));
    Vector3 target={p.origin.x,p.origin.y+(model?1.15f:.75f),p.origin.z};
    Vector3 offset=perspective?(Vector3){model?3.6f:4.5f,model?1.3f:2.0f,model?4.6f:-5.5f}:side?(Vector3){8,0,0}:(Vector3){0,0,8};
    return (Camera3D){{target.x+offset.x,target.y+offset.y,target.z+offset.z},target,{0,1,0},perspective?52:model?3.8f:5.0f,perspective?CAMERA_PERSPECTIVE:CAMERA_ORTHOGRAPHIC};
}
static void silhouette(const char* directory,int model,int side,bool fallback,const char* state) {
    Model saved=art.roadside_props[model];if(fallback)art.roadside_props[model]=(Model){0};
    Camera3D view=camera(model,side,false);RenderTexture2D target=LoadRenderTexture(384,384);assert(target.id);
    before=sim.world;BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(view);prop(model,false);EndMode3D();EndTextureMode();
    Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);Color* pixels=LoadImageColors(image);int checked=0,filled=0;
    const float offsets[][2]={{0,0},{-1.5f,0},{1.5f,0},{0,-1.5f},{0,1.5f}};
    for(int y=4;y<380;y+=8)for(int x=4;x<380;x+=8) {
        bool hit=false,stable=true;
        for(int j=0;j<5;j++) {
            Ray r=GetScreenToWorldRayEx((Vector2){x+.5f+offsets[j][0],y+.5f+offsets[j][1]},view,384,384);bool sample=false;
            for(int part=0;part<swat_motel_roadside_count(model);part++)sample|=swat_world_object_ray(&sim.world.objects[swat_motel_roadside_first(model)+part],(b3Pos){r.position.x,r.position.y,r.position.z},swat_mul(swat_v(r.direction.x,r.direction.y,r.direction.z),20));
            if(!j)hit=sample;else stable&=hit==sample;
        }
        if(!stable)continue;
        Color p=pixels[y*384+x];bool visible=p.r!=255 || p.g!=0 || p.b!=255;
        if(visible!=hit)fprintf(stderr,"Roadside raster model%d side%d fallback%d state%s pixel%d,%d visible%d hit%d\n",model,side,fallback,state,x,y,visible,hit);
        assert(visible==hit);checked++;filled+=visible;
    }
    assert(checked>1000 && filled>25);char file[4096];snprintf(file,sizeof(file),"%s/model%d-side%d-%s-%s.png",directory,model,side,state,fallback?"fallback":"art");assert(ExportImage(image,file));
    UnloadImageColors(pixels);UnloadImage(image);UnloadRenderTexture(target);art.roadside_props[model]=saved;
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    printf("PASS roadside raster model%d side%d %s %s: %d stable physics/pixel comparisons, %d filled\n",model,side,state,fallback?"fallback":"art",checked,filled);
}
static void shadow(void* context,const SwatSim* s,bool cutaway){(void)context;(void)s;(void)cutaway;prop(0,true);prop(1,true);}
static void scene(const char* directory,int model,const char* state) {
    Camera3D view=camera(model,0,true);art.lighting=&lighting;lighting.prepared=false;before=sim.world;
    swat_lighting_prepare_context(&lighting,&sim,view.position,false,shadow,NULL);assert(lighting.enabled && lighting.prepared);
    RenderTexture2D target=LoadRenderTexture(960,640);assert(target.id);BeginTextureMode(target);ClearBackground(MAGENTA);swat_lighting_sky(&lighting,view,960,640);
    BeginMode3D(view);DrawPlane((Vector3){view.target.x,-.08f,view.target.z},(Vector2){18,18},(Color){88,90,84,255});
    swat_lighting_begin(&lighting,&art,&sim.world,view.position);prop(model,false);swat_lighting_end(&lighting,&art);EndMode3D();EndTextureMode();
    Image image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);char file[4096];snprintf(file,sizeof(file),"%s/model%d-%s-lit.png",directory,model,state);assert(ExportImage(image,file));
    UnloadImage(image);UnloadRenderTexture(target);art.lighting=NULL;assert(!memcmp(&before,&sim.world,sizeof(before)));
}
static int named(int model,const char* name){for(int i=0;i<swat_motel_roadside_count(model);i++)if(!strcmp(swat_motel_roadside_part(model,i)->name,name))return swat_motel_roadside_first(model)+i;assert(false);return -1;}
int main(int argc,char** argv) {
    assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);environment("SWAT_ENVIRONMENT_ASSETS",argv[2]);environment("SWAT_IBL","0");environment("SWAT_CONTACT_SHADOWS","0");environment("SWAT_ENVIRONMENT_ART","1");environment("SWAT_LIGHTING","1");
    SetTraceLogLevel(LOG_WARNING);SetConfigFlags(FLAG_WINDOW_HIDDEN);InitWindow(384,384,"SWAT roadside correctness");assert(IsWindowReady());
    swat_environment_art_init(&art);SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=false;cfg.hostile_fire=false;swat_sim_init(&sim,cfg,81);swat_environment_art_prepare_location(&art,&sim.world);
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++)assert(!art.motel[i].meshCount);
    for(int model=0;model<2;model++) {
        Model m=art.roadside_props[model];assert(m.meshCount==(model?3:12) && m.materialCount==(model?4:5));int triangles=0,consumed[64]={0};bool* matched[64]={0};
        for(int p=0;p<swat_motel_roadside_count(model);p++){matched[p]=calloc((size_t)swat_motel_roadside_part(model,p)->triangle_count,sizeof(bool));assert(matched[p]);}
        for(int mesh=0;mesh<m.meshCount;mesh++)for(int t=0;t<m.meshes[mesh].triangleCount;t++,triangles++) {
            const Mesh* source=&m.meshes[mesh];int owner=swat_motel_roadside_triangle(model,triangles);const SwatRoadsidePart* part=swat_motel_roadside_part(model,owner);assert(part);
            b3Vec3 actual[3];
            for(int k=0;k<3;k++) {
                int v=source->indices?source->indices[t*3+k]:t*3+k;actual[k]=swat_v(source->vertices[v*3],source->vertices[v*3+1],source->vertices[v*3+2]);
            }
            // Physical storage groups disconnected source solids, whose triangles
            // can interleave in the render primitive. Match the complete oriented
            // multiset, consuming each physical triangle exactly once.
            int found=-1;
            for(int at=0;at<part->triangle_count;at++)if(!matched[owner][at]) {
                bool equal=true;for(int k=0;k<3 && equal;k++)equal=b3Length(b3Sub(b3Add(part->center,part->vertices[part->indices[at*3+k]]),actual[k]))<1e-6f;
                if(equal){found=at;break;}
            }
            if(found<0)fprintf(stderr,"Roadside loaded triangle unmatched model%d mesh%d triangle%d global%d owner%d %s\n",model,mesh,t,triangles,owner,part->name);
            assert(found>=0);matched[owner][found]=true;consumed[owner]++;
        }
        assert(triangles==(model?7868:13124));for(int p=0;p<swat_motel_roadside_count(model);p++){assert(consumed[p]==swat_motel_roadside_part(model,p)->triangle_count);free(matched[p]);}
    }
    assert(swat_art_texture_stats().textures==10);swat_lighting_init(&lighting);
    for(int model=0;model<2;model++) {
        for(int side=0;side<2;side++)for(int fallback=0;fallback<2;fallback++)silhouette(argv[1],model,side,fallback,"intact");
        scene(argv[1],model,"intact");
    }
    assert(swat_world_damage(&sim.world,named(0,"Front_side_window_-1"),100));assert(swat_world_damage(&sim.world,named(1,"Post_Left_Front"),10000));
    assert(swat_world_damage(&sim.world,named(1,"BenchSeatSlat_01"),10000));assert(swat_world_damage(&sim.world,named(1,"Screen_Left"),10000));
    for(int model=0;model<2;model++) {
        for(int side=0;side<2;side++)for(int fallback=0;fallback<2;fallback++)silhouette(argv[1],model,side,fallback,"damaged");
        scene(argv[1],model,"damaged");
    }
    swat_lighting_close(&lighting);swat_sim_close(&sim);swat_environment_art_close(&art);assert(!swat_art_texture_stats().textures);CloseWindow();
    puts("PASS roadside art: original primitive/material inventory, 10 shared embedded maps, intact/damaged source and fallback silhouettes, production lighting, changed draw leader, immutable authority and texture release");return 0;
}
