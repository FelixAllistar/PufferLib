// Explicit display test: full-weight GPU vs independent CPU-deformed meshes,
// corrected gameplay poses, camera reuse and authoritative reload boundaries.
#include "character_runtime.h"
#include "raymath.h"
#include "rlgl.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
static Image frame(SwatCharacterView* view,SwatLighting* light,Camera3D camera,Matrix root) {
    RenderTexture2D target=LoadRenderTexture(512,512); assert(target.id);
    BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera);
    SwatEnvironmentArt art={0}; SwatWorld world={0};
    swat_lighting_begin(light,&art,&world,camera.position);
    swat_character_view_draw(view,light,root);
    // Ordinary geometry after a GPU character must not inherit skinning/maps.
    DrawCube((Vector3){2.5f,.5f,0},.2f,.2f,.2f,WHITE);
    swat_lighting_end(light,&art); EndMode3D(); EndTextureMode();
    Image result=LoadImageFromTexture(target.texture); ImageFlipVertical(&result); UnloadRenderTexture(target); return result;
}
static void parity(const char* path,SwatLighting* light) {
    SwatCharacterView cpu={0},gpu={0}; char error[256];
    assert(swat_character_view_init(&cpu,path,error,sizeof(error)));
    assert(swat_character_view_init_gpu(&gpu,path,error,sizeof(error)));
    SwatArtInfo info=swat_character_info(cpu.asset); double duration=swat_character_clip_duration(cpu.asset,0);
    const char* clip=swat_character_clip_name(cpu.asset,0); double times[]={0,duration*.137,duration*.95,duration,0};
    Camera3D camera={{3,1.7f,3},{0,.9f,0},{0,1,0},40,CAMERA_PERSPECTIVE};
    for(size_t t=0;t<sizeof(times)/sizeof(*times);t++) {
        assert(swat_character_view_sample(&cpu,clip,times[t])); assert(swat_character_view_sample(&gpu,clip,times[t]));
        for(int i=0;i<info.meshes;i++) assert(cpu.visible[i]==gpu.visible[i]);
        Image a=frame(&cpu,light,camera,MatrixIdentity()),b=frame(&gpu,light,camera,MatrixIdentity());
        Color* x=LoadImageColors(a),*y=LoadImageColors(b); int changed=0,lit=0,max=0; long difference=0;
        for(int i=0;i<512*512;i++) {
            int delta=abs(x[i].r-y[i].r)+abs(x[i].g-y[i].g)+abs(x[i].b-y[i].b);
            if(delta>12) changed++;
            if(delta>max) max=delta;
            difference+=delta; lit+=x[i].r+x[i].g+x[i].b>0;
        }
        printf("GPU parity influences=%d time=%.6f coverage=%d changed=%d mean_rgb_error=%.6f max_rgb_sum=%d\n",info.max_influences,times[t],lit,changed,(double)difference/(512*512*3),max);
        fflush(stdout);
        if(changed>=512*512/1000 || difference>=(long)512*512*3) {
            ExportImage(a,TextFormat("%sgpu-parity-cpu.png",GetApplicationDirectory()));
            ExportImage(b,TextFormat("%sgpu-parity-gpu.png",GetApplicationDirectory()));
        }
        assert(lit>100); assert(changed<512*512/1000); assert(difference<(long)512*512*3);
        UnloadImageColors(x); UnloadImageColors(y); UnloadImage(a); UnloadImage(b);
    }
    swat_character_view_close(&gpu); swat_character_view_close(&cpu);
}
static void runtime_check(const char* directory,SwatLighting* light) {
#ifdef _WIN32
    assert(!_putenv_s("SWAT_CHARACTER_ASSETS",directory));
#else
    assert(!setenv("SWAT_CHARACTER_ASSETS",directory,1));
#endif
    SwatWeaponArt weapons={0}; swat_weapon_art_init(&weapons);
    SwatCharacterRuntime* runtime=swat_character_runtime_open(&weapons); assert(runtime);
    SwatSim* sim=calloc(1,sizeof(*sim)); SwatSim* before=malloc(sizeof(*before)); assert(sim && before);
    SwatConfig config=swat_default_config(); config.hostile_fire=false; config.mission=SWAT_ANNEX; swat_sim_init(sim,config,42);
    SwatActor* actor=&sim->actors[0];
    for(int stance=0;stance<2;stance++) for(int angle=0;angle<4;angle++) {
        actor->controller.yaw=angle*SWAT_PI/2; actor->controller.pitch=angle==3 ? 70*SWAT_RAD : 0;
        swat_body_set_crouch(&actor->controller.body,stance!=0); sim->tick++;
        actor->controller.eye_height=actor->controller.body.totalHeight-.2032f;
        memcpy(before,sim,sizeof(*before)); swat_character_runtime_prepare(runtime,sim); assert(!memcmp(before,sim,sizeof(*before)));
        assert(runtime->actors[0].valid);
        unsigned int preparations=runtime->preparations; swat_character_runtime_prepare(runtime,sim); assert(preparations==runtime->preparations);
        SwatCharacterActorPose* cached=&runtime->actors[0]; SwatCharacterView* view=&runtime->banks[cached->bank];
        assert(swat_character_restore_pose(view->asset,cached->matrices,cached->count));
        // Physical rifle stock transform exactly follows achieved pose at every
        // yaw/stance/pitch, independent of anatomical markers and source roots.
        const float* w=swat_character_node_matrix(view->asset,swat_character_find_node(view->asset,"Prop_Rifle"));
        Vector3 stock={.0012969123f,.4499999881f,.0226502232f};
        Vector3 model={w[0]*stock.x+w[4]*stock.y+w[8]*stock.z+w[12],w[1]*stock.x+w[5]*stock.y+w[9]*stock.z+w[13],w[2]*stock.x+w[6]*stock.y+w[10]*stock.z+w[14]};
        Vector3 actual=Vector3Transform(model,cached->root); SwatPose pose=swat_pose(&actor->controller,&actor->arsenal);
        assert(Vector3Distance(actual,(Vector3){pose.shoulder.x,pose.shoulder.y,pose.shoulder.z})<1e-4f);
        unsigned int draws=runtime->draws;
        Vector3 feet_camera={cached->feet.x,cached->feet.y+1,cached->feet.z};
        BeginDrawing(); BeginMode3D((Camera3D){Vector3Add(feet_camera,(Vector3){4,0,0}),Vector3Add(feet_camera,(Vector3){5,0,0}),{0,1,0},40,CAMERA_PERSPECTIVE});
        assert(swat_character_runtime_draw(runtime,0,NULL)); assert(runtime->draws==draws); EndMode3D(); EndDrawing();
        BeginDrawing(); BeginMode3D((Camera3D){Vector3Add(feet_camera,(Vector3){4,0,0}),feet_camera,{0,1,0},40,CAMERA_PERSPECTIVE});
        assert(swat_character_runtime_draw(runtime,0,NULL)); assert(runtime->draws==draws+1); EndMode3D(); EndDrawing();
        Vector3 feet={cached->feet.x,cached->feet.y,cached->feet.z};
        memcpy(view->visible,cached->visible,(size_t)view->model.meshCount);
        for(int pass=0;pass<3;pass++) {
            Image image=frame(view,light,(Camera3D){Vector3Add(feet,(Vector3){3,1.5f,3}),Vector3Add(feet,(Vector3){0,stance ? .5f : .9f,0}),{0,1,0},40,CAMERA_PERSPECTIVE},cached->root);
            if(pass==0 && angle==0 && getenv("SWAT_CHARACTER_TEST_CAPTURES")) ExportImage(image,TextFormat("%s/live-%s.png",getenv("SWAT_CHARACTER_TEST_CAPTURES"),stance ? "crouch" : "standing"));
            UnloadImage(image);
        }
        assert(!memcmp(before,sim,sizeof(*before)));
    }
    SwatWeapon* weapon=&actor->arsenal.slots[0]; weapon->magazine=0; weapon->chambered=false;
    SwatInput input=swat_neutral_input(); input.reload=true;
    swat_weapons_step(&actor->arsenal,&input,0,0,true,false); input.reload=false;
    int duration=weapon->reload_duration; assert(duration>0);
    for(int elapsed=1;elapsed<=duration;elapsed++) {
        swat_weapons_step(&actor->arsenal,&input,0,0,true,false); sim->tick++;
        memcpy(before,sim,sizeof(*before)); swat_character_runtime_prepare(runtime,sim); assert(runtime->actors[0].valid);
        assert(!memcmp(before,sim,sizeof(*before)));
        double time=runtime->actors[0].source_time;
        if(elapsed==duration/4) assert(fabs(time-.88)<1e-7 && !weapon->magazine_seated);
        if(elapsed==2*duration/3) assert(fabs(time-3.65)<1e-7 && weapon->magazine_seated);
    }
    assert(runtime->actors[0].source_time==0 && weapon->magazine_seated);
    // Cancellation after removal, then a fresh INSERT start, cannot restore A.
    weapon->magazine=0; weapon->chambered=false; input.reload=true;
    swat_weapons_step(&actor->arsenal,&input,0,0,true,false); input.reload=false;
    while(weapon->reload_stage==SWAT_RELOAD_REMOVE) swat_weapons_step(&actor->arsenal,&input,0,0,true,false);
    swat_weapons_cancel_reload(weapon); sim->tick++; swat_character_runtime_prepare(runtime,sim);
    assert(!weapon->magazine_seated); assert(runtime->actors[0].source_time==0);
    SwatCharacterView* ready=&runtime->banks[0];
    for(int m=0;m<ready->model.meshCount;m++) if(!strcmp(swat_character_mesh(ready->asset,m)->node_name,"Removed magazine")) assert(!runtime->actors[0].visible[m]);
    input.reload=true; swat_weapons_step(&actor->arsenal,&input,0,0,true,false); sim->tick++; swat_character_runtime_prepare(runtime,sim);
    assert(weapon->reload_stage==SWAT_RELOAD_INSERT && runtime->actors[0].source_time>=.88);
    printf("PASS live GPU character: yaw, crouch, pitch, immutable authority, camera reuse, reload commits/cancellation/restart\n");
    swat_sim_close(sim); free(sim); free(before); swat_character_runtime_close(runtime); swat_weapon_art_close(&weapons);
}
int main(int argc,char** argv) {
    assert(argc>=2); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(512,512,"character GPU contract"); assert(IsWindowReady());
    SwatLighting light={0}; swat_lighting_init(&light); assert(light.enabled);
    static SwatSim sim; swat_lighting_prepare(&light,&sim,(Vector3){3,1.7f,3},false,empty);
    parity(argv[1],&light); if(argc==3) runtime_check(argv[2],&light);
    swat_lighting_close(&light); CloseWindow(); return 0;
}
