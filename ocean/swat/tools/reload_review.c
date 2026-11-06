// Reload presentation review: one officer reloads through the real simulation
// (standing, crouched, walking) and the game's character runtime renders a
// contact sheet across the reload: side and front views in third person plus
// the first-person weapon view. Prints hand/magazine metrics per frame.
//   reload_review ASSET_DIR OUT.png
#include "character_runtime.h"
#include "raymath.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
#define ROWS 5
static int TILE=300,FRAMES=8; // REVIEW_TILE / REVIEW_FRAMES; REVIEW_ROW=n renders one row
static SwatSim sim;
static void fixture(void) {
    memset(&sim,0,sizeof(sim)); sim.config=swat_default_config(); sim.config.max_ticks=10000; sim.config.hostile_fire=false;
    swat_world_init(&sim.world); sim.extraction=(b3Pos){-15,0,0}; sim.actor_count=1;
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(20,.5f,20),SWAT_CONCRETE,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){0,0,0},0);
}
static int node_index(SwatCharacterAsset* a,const char* name) { int i=swat_character_find_node(a,name); assert(i>=0); return i; }
static Vector3 node_world(const SwatCharacterActorPose* p,SwatCharacterAsset* a,const char* name) {
    const float* m=p->matrices+node_index(a,name)*16;
    return Vector3Transform((Vector3){m[12],m[13],m[14]},p->root);
}
int main(int argc,char** argv) {
    if(argc<3) { fprintf(stderr,"usage: %s ASSET_DIR OUT.png\n",argv[0]); return 2; }
    setenv("SWAT_CHARACTER_ASSETS",argv[1],1); SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(300,300,"reload review");
    SwatLighting light={0}; swat_lighting_init(&light); static SwatSim lsim; swat_lighting_prepare(&light,&lsim,(Vector3){3,1.7f,3},false,empty);
    SwatWeaponArt weapons={0}; swat_weapon_art_init(&weapons);
    SwatCharacterRuntime* runtime=swat_character_runtime_open(&weapons); assert(runtime);
    if(getenv("REVIEW_TILE")) TILE=atoi(getenv("REVIEW_TILE")); if(getenv("REVIEW_FRAMES")) FRAMES=atoi(getenv("REVIEW_FRAMES"));
    int only=getenv("REVIEW_ROW") ? atoi(getenv("REVIEW_ROW")) : -1;
    const char* names[ROWS]={"stand side","stand front","crouch side","walk side","first person"};
    Image sheet=GenImageColor(FRAMES*TILE,(only>=0?1:ROWS)*TILE,BLACK);
    for(int row=0;row<ROWS;row++) {
        if(only>=0 && row!=only) continue; int out=only>=0?0:row;
        fixture(); SwatActor* a=&sim.actors[0];
        SwatInput in=swat_neutral_input(); in.crouch=row==2;
        for(int i=0;i<40;i++) swat_sim_step(&sim,&in);
        in.fire=true; for(int i=0;i<10;i++) { in.fire=i%2==0; swat_sim_step(&sim,&in); } in.fire=false;
        in.reload=true; swat_sim_step(&sim,&in); in.reload=false;
        const SwatWeapon* w=&a->arsenal.slots[a->arsenal.active]; int total=w->reload_duration; assert(total>0);
        if(row==3) in.forward=.6f;
        for(int t=1,shot=0;t<=total+20 && shot<FRAMES;t++) {
            swat_sim_step(&sim,&in);
            if(t!=1+((total+16)*shot)/(FRAMES-1)) continue;
            swat_character_runtime_prepare(runtime,&sim);
            SwatCharacterActorPose* p=&runtime->actors[0]; assert(p->valid);
            SwatCharacterAsset* asset=runtime->banks[p->bank].asset;
            Vector3 f={(float)p->feet.x,(float)p->feet.y,(float)p->feet.z};
            Vector3 lh=node_world(p,asset,"mixamorig:LeftHand"),rh=node_world(p,asset,"mixamorig:RightHand");
            Vector3 ma=node_world(p,asset,"Prop_Magazine_A"),mb=node_world(p,asset,"Prop_Magazine_B");
            int vis_removed=-1,vis_fresh=-1;
            for(int m=0;m<swat_character_info(asset).meshes;m++) {
                const char* n=swat_character_mesh(asset,m)->node_name;
                if(!strcmp(n,"Removed magazine")) vis_removed=p->visible[m];
                if(!strcmp(n,"Fresh magazine")) vis_fresh=p->visible[m];
            }
            printf("%-12s t%3d/%d time %.2f stage %d seated %d  L-magA %.2f L-magB %.2f  vis removed %d fresh %d\n",names[row],t,total,p->source_time,
                w->reload_stage,w->magazine_seated,Vector3Distance(lh,ma),Vector3Distance(lh,mb),vis_removed,vis_fresh);
            (void)rh;
            RenderTexture2D target=LoadRenderTexture(TILE,TILE); BeginTextureMode(target); ClearBackground((Color){40,44,52,255});
            SwatEnvironmentArt art={0}; SwatWorld world={0};
            if(row<4) {
                Vector3 eye=row==1 ? (Vector3){f.x+2.2f,f.y+1.35f,f.z+.6f} : (Vector3){f.x+.2f,f.y+(row==2?.9f:1.25f),f.z+2.4f};
                Camera3D cam={eye,{f.x+.25f,f.y+(row==2?.7f:1.05f),f.z},{0,1,0},38,CAMERA_PERSPECTIVE};
                BeginMode3D(cam); swat_lighting_begin(&light,&art,&world,cam.position);
                swat_character_runtime_draw(runtime,0,&light);
                swat_lighting_end(&light,&art); DrawGrid(20,.5f); EndMode3D();
            } else {
                SwatPose achieved=swat_pose(&a->controller,&a->arsenal);
                SwatPose pose=swat_weapon_view_pose(&achieved,&a->arsenal,a->controller.ads,-.055f,.075f,.12f);
                b3Pos e=achieved.eye; b3Pos look=b3OffsetPos(e,achieved.forward);
                Camera3D cam={{(float)e.x,(float)e.y,(float)e.z},{(float)look.x,(float)look.y,(float)look.z},{0,1,0},
                    2*atanf(tanf(31*SWAT_RAD)/1.7f)/SWAT_RAD,CAMERA_PERSPECTIVE};
                BeginMode3D(cam); swat_lighting_begin(&light,&art,&world,cam.position);
                swat_character_runtime_draw_first_person(runtime,0,&light,&pose);
                swat_lighting_end(&light,&art); EndMode3D();
            }
            EndTextureMode();
            Image im=LoadImageFromTexture(target.texture); ImageFlipVertical(&im);
            ImageDrawText(&im,TextFormat("%s  %.2fs clip %.2f",names[row],t/60.0f,p->source_time),6,6,12,RAYWHITE);
            ImageDrawText(&im,TextFormat("stage %d seated %d",w->reload_stage,w->magazine_seated),6,TILE-18,12,(Color){150,230,150,255});
            ImageDraw(&sheet,im,(Rectangle){0,0,TILE,TILE},(Rectangle){shot*TILE,out*TILE,TILE,TILE},WHITE);
            UnloadImage(im); UnloadRenderTexture(target); shot++;
        }
        swat_sim_close(&sim);
    }
    bool ok=ExportImage(sheet,argv[2]); UnloadImage(sheet);
    swat_character_runtime_close(runtime); swat_weapon_art_close(&weapons); swat_lighting_close(&light); CloseWindow(); return ok?0:1;
}
