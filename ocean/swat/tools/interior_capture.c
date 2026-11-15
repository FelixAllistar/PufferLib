// Interior art review: the real motel mission rendered by the production view
// (lighting, shadows, bloom) from fixed interior cameras, with every actor
// parked out in the lot so furniture and finishes are unobstructed.
//   interior_capture OUT_DIR [--shot N] [--width W --height H]
// Presentation-only review tool; it never feeds back into the simulation.
#include "render.h"
#include "raymath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SwatSim* sim;
static SwatView view;
static int width=1280,height=720;

static void park(int i,float x,float z) {
    SwatActor* a=&sim->actors[i]; b3Pos f=swat_body_feet_position(&a->controller.body);
    b3Body_SetTransform(a->controller.body.body,(b3Pos){x,f.y+a->controller.body.totalHeight*.5f,z},b3Quat_identity);
    b3Body_SetLinearVelocity(a->controller.body.body,swat_v(0,0,0));
    a->mind.bot=false; a->mind.order=0;
}
static void reset(void) {
    if(sim) swat_sim_close(sim); else sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.layout_seed=42;
    config.tactical_rules=true; config.squad_bots=3; config.hostile_fire=false; config.max_ticks=100000;
    swat_sim_init(sim,config,42);
    for(int i=0,k=0;i<sim->actor_count;i++) if(sim->actors[i].present) { park(i,-8.f+1.2f*k,14.f); k++; }
    for(int t=0;t<30;t++) { SwatInput in=swat_neutral_input(); swat_sim_step(sim,&in); }
}
typedef struct Shot { const char* name; float p[3],t[3],fov; } Shot;
static const Shot shots[]={
    {"r102_bed",     {-3.35f,1.62f,-.45f},{-1.4f,.75f,-3.4f},62},
    {"r103_window",  { 1.6f,1.55f,-3.75f},{ 1.4f,1.05f, .0f},64},
    {"r104_desk_tv", { 7.6f,1.55f,-.7f},  { 4.6f,.85f,-2.6f},62},
    {"r102_bath",    {-2.6f,1.62f,-4.15f},{-1.0f,.7f,-5.7f}, 70},
    {"reception",    {-10.9f,1.6f,-.6f},  {-9.6f,1.0f,-5.0f},66},
    {"laundry",      {-9.0f,1.6f,-6.6f},  {-11.2f,.8f,-9.5f},70},
    {"r101_room",    {-7.4f,1.6f,-.5f},   {-5.0f,.8f,-3.2f}, 64},
    {"r104_bath",    { 5.0f,1.6f,-4.15f}, { 7.4f,.6f,-5.6f}, 70},
};
int main(int argc,char** argv) {
    if(argc<2) { fprintf(stderr,"usage: %s OUT_DIR [--shot N]\n",argv[0]); return 2; }
    const char* out=argv[1]; int only=-1;
    for(int i=2;i<argc;i++) {
        if(!strcmp(argv[i],"--shot") && i+1<argc) only=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--width") && i+1<argc) width=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--height") && i+1<argc) height=atoi(argv[++i]);
    }
    SetTraceLogLevel(LOG_WARNING);
    reset();
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT); InitWindow(width,height,"interior capture");
    swat_view_init(&view,true); assert(view.initialized); view.actor=0;
    char path[1024];
    for(int s=0;s<(int)(sizeof(shots)/sizeof(shots[0]));s++) {
        if(only>=0 && only!=s) continue;
        const Shot* h=&shots[s];
        Camera3D cam={{h->p[0],h->p[1],h->p[2]},{h->t[0],h->t[1],h->t[2]},{0,1,0},h->fov,CAMERA_PERSPECTIVE};
        for(int t=0;t<6;t++) { BeginDrawing(); swat_view_draw_cinematic(&view,sim,cam); EndDrawing(); }
        Image im=LoadImageFromScreen(); snprintf(path,sizeof(path),"%s/%d_%s.png",out,s,h->name);
        ExportImage(im,path); UnloadImage(im); printf("wrote %s\n",path);
    }
    swat_view_close(&view); swat_sim_close(sim); free(sim); return 0;
}
