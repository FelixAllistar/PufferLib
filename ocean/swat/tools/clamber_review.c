// Clamber presentation review: an officer vaults a 0.95 m wall and climbs a
// 0.70 m crate through the real simulation; renders side-view frames across
// each move with the game's character runtime.
//   clamber_review ASSET_DIR OUT.png
#include "character_runtime.h"
#include "raymath.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
#define TILE 300
#define FRAMES 7
static SwatSim sim;
static void fixture(b3Pos box_center,b3Vec3 box_half) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.max_ticks=10000; sim.config.hostile_fire=false;
    swat_world_init(&sim.world); sim.extraction=(b3Pos){-15,0,0}; sim.actor_count=1;
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(20,.5f,20),SWAT_CONCRETE,0);
    swat_world_box(&sim.world,box_center,box_half,SWAT_WOOD,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){-2,0,0},0);
    SwatInput n=swat_neutral_input(); for(int i=0;i<20;i++) swat_sim_step(&sim,&n);
}
int main(int argc,char** argv) {
    if(argc<3) { fprintf(stderr,"usage: %s ASSET_DIR OUT.png\n",argv[0]); return 2; }
    setenv("SWAT_CHARACTER_ASSETS",argv[1],1); SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(TILE,TILE,"clamber review");
    SwatLighting light={0}; swat_lighting_init(&light); static SwatSim lsim; swat_lighting_prepare(&light,&lsim,(Vector3){3,1.7f,3},false,empty);
    SwatWeaponArt weapons={0}; swat_weapon_art_init(&weapons);
    SwatCharacterRuntime* runtime=swat_character_runtime_open(&weapons); assert(runtime);
    const b3Pos centers[2]={{0,.475f,0},{.8f,.35f,0}}; const b3Vec3 halves[2]={{.10f,.475f,1.5f},{.8f,.35f,1}};
    const char* names[2]={"vault 0.95 m wall","climb 0.70 m crate"};
    Image sheet=GenImageColor(FRAMES*TILE,2*TILE,BLACK);
    for(int c=0;c<2;c++) {
        fixture(centers[c],halves[c]); SwatActor* a=&sim.actors[0];
        SwatInput in=swat_neutral_input(); in.forward=1;
        while(swat_body_feet_position(&a->controller.body).x<-.75f) swat_sim_step(&sim,&in);
        in.jump=true; swat_sim_step(&sim,&in); in.jump=false; assert(swat_controller_mantling(&a->controller));
        int total=a->controller.mantle.total,shot=0;
        for(int t=1;t<=total+8 && shot<FRAMES;t++) {
            swat_sim_step(&sim,&in);
            if(t!=1+((total+6)*shot)/(FRAMES-1)) continue;
            swat_character_runtime_prepare(runtime,&sim);
            SwatCharacterActorPose* p=&runtime->actors[0]; assert(p->valid);
            Vector3 f={(float)p->feet.x,(float)p->feet.y,(float)p->feet.z};
            RenderTexture2D target=LoadRenderTexture(TILE,TILE); BeginTextureMode(target); ClearBackground((Color){40,44,52,255});
            Camera3D cam={{-.1f,f.y+1.2f,3.4f},{-.1f,f.y+.85f,0},{0,1,0},45,CAMERA_PERSPECTIVE};
            BeginMode3D(cam); SwatEnvironmentArt art={0}; SwatWorld world={0}; swat_lighting_begin(&light,&art,&world,cam.position);
            swat_character_runtime_draw(runtime,0,&light);
            DrawCube((Vector3){centers[c].x,centers[c].y,centers[c].z},2*halves[c].x,2*halves[c].y,1.2f,(Color){120,90,60,255});
            swat_lighting_end(&light,&art); DrawGrid(20,.5f); EndMode3D(); EndTextureMode();
            Image im=LoadImageFromTexture(target.texture); ImageFlipVertical(&im);
            const SwatMantle* m=&a->controller.mantle;
            ImageDrawText(&im,TextFormat("%s  tick %d/%d",names[c],m->active?m->tick:total,total),6,6,12,RAYWHITE);
            ImageDrawText(&im,TextFormat("feet %.2f %.2f",f.x,f.y),6,TILE-18,12,(Color){150,230,150,255});
            ImageDraw(&sheet,im,(Rectangle){0,0,TILE,TILE},(Rectangle){shot*TILE,c*TILE,TILE,TILE},WHITE);
            UnloadImage(im); UnloadRenderTexture(target); shot++;
        }
        swat_sim_close(&sim);
    }
    bool ok=ExportImage(sheet,argv[2]); UnloadImage(sheet);
    swat_character_runtime_close(runtime); swat_weapon_art_close(&weapons); swat_lighting_close(&light); CloseWindow(); return ok?0:1;
}
