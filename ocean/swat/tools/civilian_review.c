// Civilian presentation review: drives one civilian through idle, walk,
// surrender (transition + loop), restraint (transition + kneel), rising and
// escort, and renders a timeline contact sheet through the game's runtime.
//   civilian_review OUT.png
#include "character_runtime.h"
#include "raymath.h"
#include "rlgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void empty(const SwatSim* sim,bool cutaway) { (void)sim; (void)cutaway; }
#define TILE 300
static const char* loop_names[SWAT_CIV_LOOPS]={"idle","walk","surrender","restrained kneel","restrained stand","escort"};

int main(int argc,char** argv) {
    if(argc<2) { fprintf(stderr,"usage: %s OUT.png\n",argv[0]); return 2; }
    SetTraceLogLevel(LOG_WARNING); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(TILE,TILE,"civilian review");
    SwatLighting light={0}; swat_lighting_init(&light); static SwatSim lsim; swat_lighting_prepare(&light,&lsim,(Vector3){3,1.7f,3},false,empty);
    SwatCivilianRuntime* r=swat_civilian_runtime_open(); assert(r);
    SwatSim* sim=calloc(1,sizeof(*sim)); SwatConfig config=swat_default_config(); config.hostile_fire=false; config.mission=SWAT_ANNEX;
    swat_sim_init(sim,config,42);
    int civ=-1; for(int i=0;i<sim->actor_count;i++) if(sim->actors[i].present && sim->actors[i].role==SWAT_CIVILIAN) { civ=i; break; }
    assert(civ>=0); SwatActor* a=&sim->actors[civ];
    // Phases: (label, seconds, surrendered, restrained, crouched, speed)
    const struct { const char* label; double seconds; bool surrendered,restrained,crouched; float speed; } phases[]={
        {"idle",1,0,0,0,0},{"walk",1.6,0,0,0,1.2f},{"surrender",5,1,0,1,0},{"restrain",3.5,1,1,1,0},
        {"rise",5,1,1,0,0},{"escort",2,1,1,0,.6f}};
    const int phase_count=sizeof(phases)/sizeof(*phases),frames=6;
    Image sheet=GenImageColor(frames*TILE,phase_count*TILE,BLACK);
    double clock=0; b3Pos feet=swat_body_feet_position(&a->controller.body);
    for(int ph=0;ph<phase_count;ph++) {
        a->gear.surrendered=phases[ph].surrendered; a->gear.restrained=phases[ph].restrained;
        a->controller.body.crouched=phases[ph].crouched; a->controller.yaw=0;
        int steps=(int)(phases[ph].seconds*30),shot=0;
        for(int s=0;s<=steps;s++) {
            double dt=1/30.0; clock+=dt; sim->tick+=2;
            feet.x+=phases[ph].speed*dt;
            b3Body_SetTransform(a->controller.body.body,b3OffsetPos(feet,swat_v(0,a->controller.body.totalHeight*.5f,0)),b3Quat_identity);
            b3Body_SetLinearVelocity(a->controller.body.body,swat_v(phases[ph].speed,0,0));
            swat_civilian_runtime_prepare(r,sim,clock);
            if(s==(steps*shot)/(frames-1) && shot<frames) {
                SwatCivilianPose* p=&r->actors[civ]; assert(p->valid);
                Vector3 f={(float)p->feet.x,(float)p->feet.y,(float)p->feet.z};
                RenderTexture2D t=LoadRenderTexture(TILE,TILE); BeginTextureMode(t); ClearBackground((Color){40,44,52,255});
                Camera3D cam={Vector3Add(f,(Vector3){1.6f,1.2f,2.6f}),Vector3Add(f,(Vector3){0,.8f,0}),{0,1,0},40,CAMERA_PERSPECTIVE};
                BeginMode3D(cam); SwatEnvironmentArt art={0}; SwatWorld world={0}; swat_lighting_begin(&light,&art,&world,cam.position);
                swat_civilian_runtime_draw(r,civ,&light); swat_lighting_end(&light,&art); DrawGrid(20,.5f); EndMode3D(); EndTextureMode();
                Image im=LoadImageFromTexture(t.texture); ImageFlipVertical(&im);
                const char* state=p->transition>=0 ? "transition" : loop_names[p->loop];
                ImageDrawText(&im,TextFormat("%s t+%.1fs",phases[ph].label,s/30.0),6,6,14,RAYWHITE);
                ImageDrawText(&im,state,6,TILE-20,14,(Color){150,230,150,255});
                ImageDraw(&sheet,im,(Rectangle){0,0,TILE,TILE},(Rectangle){shot*TILE,ph*TILE,TILE,TILE},WHITE);
                UnloadImage(im); UnloadRenderTexture(t); shot++;
            }
        }
    }
    bool ok=ExportImage(sheet,argv[1]); UnloadImage(sheet);
    swat_civilian_runtime_close(r); swat_lighting_close(&light); CloseWindow(); return ok?0:1;
}
